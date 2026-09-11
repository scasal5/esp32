#include "splash_gif.h"
#include "boot_splash.h"
#include "home_screen.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "lvgl.h"

static const char *TAG = "splash_gif";

#define SPLASH_PATH  BSP_SPIFFS_MOUNT_POINT "/splash.gif"

/* El archivo entero vive en PSRAM mientras se decodifica el frame 0. */
#define SPLASH_MAX_BYTES  (1024 * 1024)

/* 8 KB: montar SPIFFS y decodificar el frame 0 corren en esta task. El log
   final informa cuanto sobro, para ajustarlo con datos. */
#define SPLASH_TASK_STACK  8192

/* Por debajo de la task de LVGL de esp_lvgl_port (prioridad 4): leer la flash
   y procesar el fondo no le quitan CPU al render. */
#define SPLASH_TASK_PRIO   2

/* Fondo de la pantalla de inicio a partir del frame 0. */
#define BG_SCALE_NUM       3    /* 3/5: 240x234 queda en 144x140 */
#define BG_SCALE_DEN       5
#define BG_BLUR_RADIUS     3    /* px sobre la imagen ya reducida: desenfoque moderado */
#define BG_DIM_PERCENT     60   /* brillo que queda: el texto blanco se lee sobre la calavera */

/* lv_gif guarda el puntero al descriptor y al buffer sin copiarlos: los dos
   tienen que vivir tanto como el objeto. Por eso son estaticos. */
static lv_image_dsc_t s_dsc;
static uint8_t *s_buf = NULL;

/*
 * Monta la particion assets con la misma etiqueta, punto de montaje y
 * max_files que el BSP. No usa bsp_spiffs_mount(): con CONFIG_BSP_ERROR_CHECK=y
 * un mount fallido llama a abort(), y una particion vacia o con restos del
 * firmware de fabrica (SPIFFS_ERR_NOT_A_FS, -10025) deja la placa en un loop
 * de reinicios. Nunca formatea: seria lento y borraria lo que haya.
 */
static esp_err_t mount_assets(void)
{
    const esp_vfs_spiffs_conf_t conf = {
        .base_path = BSP_SPIFFS_MOUNT_POINT,
        .partition_label = CONFIG_BSP_SPIFFS_PARTITION_LABEL,
        .max_files = CONFIG_BSP_SPIFFS_MAX_FILES,
        .format_if_mount_failed = false,
    };
    return esp_vfs_spiffs_register(&conf);
}

/*
 * Cabecera GIF: firma de 6 bytes y ancho/alto en los bytes 6..9, little-endian.
 * No se usa lv_gif_get_size(): declara ~24 KB en el stack de quien llama y
 * abre el archivo por el sistema de archivos de LVGL, que no esta habilitado.
 */
static bool gif_header_ok(const uint8_t *p, size_t n)
{
    if (n < 13 || (memcmp(p, "GIF87a", 6) != 0 && memcmp(p, "GIF89a", 6) != 0)) {
        ESP_LOGW(TAG, "no es un GIF");
        return false;
    }

    unsigned w = p[6] | (p[7] << 8);
    unsigned h = p[8] | (p[9] << 8);
    if (w == 0 || h == 0 || w > BSP_LCD_H_RES || h > BSP_LCD_V_RES) {
        /* En la placa no se escala el GIF al leerlo: tiene que entrar tal cual. */
        ESP_LOGW(TAG, "GIF %ux%u fuera de %dx%d", w, h, BSP_LCD_H_RES, BSP_LCD_V_RES);
        return false;
    }
    return true;
}

/* Lee el GIF completo a PSRAM. Devuelve el largo, o 0 si no hay GIF usable. */
static size_t read_splash(void)
{
    struct stat st;
    if (stat(SPLASH_PATH, &st) != 0) {
        ESP_LOGW(TAG, "no hay %s", SPLASH_PATH);
        return 0;
    }
    if (st.st_size <= 0 || st.st_size > SPLASH_MAX_BYTES) {
        ESP_LOGW(TAG, "%s mide %ld bytes (maximo %d)", SPLASH_PATH, (long)st.st_size,
                 SPLASH_MAX_BYTES);
        return 0;
    }

    s_buf = heap_caps_malloc((size_t)st.st_size, MALLOC_CAP_SPIRAM);
    if (s_buf == NULL) {
        ESP_LOGE(TAG, "sin PSRAM para %ld bytes", (long)st.st_size);
        return 0;
    }

    FILE *f = fopen(SPLASH_PATH, "rb");
    size_t len = 0;
    if (f != NULL) {
        len = fread(s_buf, 1, (size_t)st.st_size, f);
        fclose(f);
    }

    if (len != (size_t)st.st_size || !gif_header_ok(s_buf, len)) {
        ESP_LOGW(TAG, "%s ilegible o invalido", SPLASH_PATH);
        heap_caps_free(s_buf);
        s_buf = NULL;
        return 0;
    }
    return len;
}

/*
 * Decodifica el frame 0 en un lv_gif oculto y en pausa. Con el lock tomado.
 * Devuelve NULL si lv_gif no pudo cargar el archivo.
 */
static lv_obj_t *decode_frame0(size_t len)
{
    memset(&s_dsc, 0, sizeof(s_dsc));
    s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_dsc.data = s_buf;
    s_dsc.data_size = len;

    lv_obj_t *gif = lv_gif_create(lv_screen_active());
    /* Nunca se dibuja: solo se usa su framebuffer como fuente del fondo. */
    lv_obj_add_flag(gif, LV_OBJ_FLAG_HIDDEN);
    /* ARGB8888 antes de set_src: es el unico formato en el que lv_gif deja el
       indice transparente del GIF como alfa 0. */
    lv_gif_set_color_format(gif, LV_COLOR_FORMAT_ARGB8888);
    /* Decodifica el frame 0 y ademas arranca el timer: se pausa en la misma
       seccion bloqueada, antes de que llegue a correr. */
    lv_gif_set_src(gif, &s_dsc);
    lv_gif_pause(gif);

    if (!lv_gif_is_loaded(gif)) {
        lv_obj_delete(gif);
        return NULL;
    }
    return gif;
}

/*
 * Promedio de una ventana de 2r+1 px sobre 4 canales premultiplicados, por
 * filas o por columnas. Lo que cae fuera del buffer cuenta como transparente.
 */
static void box_blur_1d(const uint8_t *in, uint8_t *out, uint32_t w, uint32_t h,
                        uint32_t r, bool vertical)
{
    const uint32_t win = 2 * r + 1;
    const uint32_t len = vertical ? h : w;
    const uint32_t lines = vertical ? w : h;

    for (uint32_t l = 0; l < lines; l++) {
        for (uint32_t i = 0; i < len; i++) {
            uint32_t sum[4] = { 0 };
            for (int32_t k = (int32_t)i - (int32_t)r; k <= (int32_t)(i + r); k++) {
                if (k < 0 || k >= (int32_t)len) {
                    continue;
                }
                const uint32_t idx = vertical ? (uint32_t)k * w + l : l * w + (uint32_t)k;
                for (int c = 0; c < 4; c++) {
                    sum[c] += in[idx * 4 + c];
                }
            }
            const uint32_t o = vertical ? i * w + l : l * w + i;
            for (int c = 0; c < 4; c++) {
                out[o * 4 + c] = (uint8_t)(sum[c] / win);
            }
        }
    }
}

/*
 * Fondo de la pantalla de inicio a partir del framebuffer ARGB8888 de lv_gif:
 * reducido a BG_SCALE_NUM/BG_SCALE_DEN, desenfocado y oscurecido. Devuelve un
 * draw_buf ARGB8888 nuevo, o NULL si falta memoria. Formato LVGL: B, G, R, A.
 */
static lv_draw_buf_t *make_background(const lv_draw_buf_t *src)
{
    const uint32_t sw = src->header.w;
    const uint32_t sh = src->header.h;
    const uint32_t sstride = src->header.stride;
    const uint32_t w = sw * BG_SCALE_NUM / BG_SCALE_DEN;
    const uint32_t h = sh * BG_SCALE_NUM / BG_SCALE_DEN;
    /* Margen del radio del blur: el desenfoque se expande sin cortarse. */
    const uint32_t pad = BG_BLUR_RADIUS;
    const uint32_t bw = w + 2 * pad;
    const uint32_t bh = h + 2 * pad;

    uint8_t *a = heap_caps_calloc((size_t)bw * bh, 4, MALLOC_CAP_SPIRAM);
    uint8_t *b = heap_caps_calloc((size_t)bw * bh, 4, MALLOC_CAP_SPIRAM);
    lv_draw_buf_t *out = lv_draw_buf_create(bw, bh, LV_COLOR_FORMAT_ARGB8888, 0);
    if (a == NULL || b == NULL || out == NULL) {
        heap_caps_free(a);
        heap_caps_free(b);
        if (out != NULL) {
            lv_draw_buf_destroy(out);
        }
        return NULL;
    }

    /* 1. Reduccion por promedio de area con alfa premultiplicado: los pixels
          transparentes no aportan color y no oscurecen los bordes. */
    for (uint32_t y = 0; y < h; y++) {
        const uint32_t y0 = y * BG_SCALE_DEN / BG_SCALE_NUM;
        const uint32_t y1 = LV_MIN(LV_MAX(y0 + 1, (y + 1) * BG_SCALE_DEN / BG_SCALE_NUM), sh);
        for (uint32_t x = 0; x < w; x++) {
            const uint32_t x0 = x * BG_SCALE_DEN / BG_SCALE_NUM;
            const uint32_t x1 = LV_MIN(LV_MAX(x0 + 1, (x + 1) * BG_SCALE_DEN / BG_SCALE_NUM), sw);
            uint32_t sum[4] = { 0 };
            uint32_t n = 0;
            for (uint32_t yy = y0; yy < y1; yy++) {
                const uint8_t *row = src->data + yy * sstride;
                for (uint32_t xx = x0; xx < x1; xx++) {
                    const uint8_t *px = row + xx * 4;
                    sum[0] += (uint32_t)px[0] * px[3];
                    sum[1] += (uint32_t)px[1] * px[3];
                    sum[2] += (uint32_t)px[2] * px[3];
                    sum[3] += px[3];
                    n++;
                }
            }
            uint8_t *d = &a[((y + pad) * bw + x + pad) * 4];
            d[0] = (uint8_t)(sum[0] / (n * 255));
            d[1] = (uint8_t)(sum[1] / (n * 255));
            d[2] = (uint8_t)(sum[2] / (n * 255));
            d[3] = (uint8_t)(sum[3] / n);
        }
    }

    /* 2. Blur de caja separable: horizontal (a -> b) y vertical (b -> a). */
    box_blur_1d(a, b, bw, bh, BG_BLUR_RADIUS, false);
    box_blur_1d(b, a, bw, bh, BG_BLUR_RADIUS, true);

    /* 3. Des-premultiplicar al ARGB8888 de LVGL y oscurecer. */
    for (uint32_t y = 0; y < bh; y++) {
        uint8_t *dst = out->data + y * out->header.stride;
        for (uint32_t x = 0; x < bw; x++) {
            const uint8_t *s = &a[(y * bw + x) * 4];
            uint8_t *d = dst + x * 4;
            if (s[3] == 0) {
                d[0] = d[1] = d[2] = d[3] = 0;
                continue;
            }
            for (int c = 0; c < 3; c++) {
                uint32_t v = LV_MIN((uint32_t)s[c] * 255 / s[3], 255u);
                d[c] = (uint8_t)(v * BG_DIM_PERCENT / 100);
            }
            d[3] = s[3];
        }
    }

    heap_caps_free(a);
    heap_caps_free(b);
    return out;
}

/* Frame 0 de splash.gif convertido en fondo. NULL si no hay GIF usable o falta
   memoria. Libera el archivo y el lv_gif antes de volver. */
static lv_draw_buf_t *background_from_gif(void)
{
    size_t len = read_splash();
    if (len == 0) {
        return NULL;
    }

    lv_draw_buf_t *bg = NULL;
    int64_t t0 = esp_timer_get_time();

    /* Timeout 0 en esp_lvgl_port = esperar sin limite. No usar portMAX_DELAY:
       el valor esta en milisegundos y desborda al pasar a ticks. */
    if (bsp_display_lock(0)) {
        lv_obj_t *gif = decode_frame0(len);
        const lv_draw_buf_t *frame =
            gif != NULL ? (const lv_draw_buf_t *)lv_image_get_src(gif) : NULL;
        bsp_display_unlock();
        int64_t t1 = esp_timer_get_time();

        if (frame == NULL) {
            ESP_LOGW(TAG, "lv_gif no cargo el GIF");
        } else {
            /* Sin lock: el lv_gif esta oculto y en pausa, nadie escribe su
               framebuffer mientras se procesa. */
            bg = make_background(frame);
            int64_t t2 = esp_timer_get_time();

            if (bsp_display_lock(0)) {
                lv_obj_delete(gif);
                bsp_display_unlock();
            }

            if (bg != NULL) {
                ESP_LOGI(TAG, "fondo %ux%u (decode %lld ms, proceso %lld ms)",
                         (unsigned)bg->header.w, (unsigned)bg->header.h,
                         (long long)((t1 - t0) / 1000), (long long)((t2 - t1) / 1000));
            } else {
                ESP_LOGW(TAG, "sin memoria para el fondo");
            }
        }
    }

    /* El lv_gif ya no existe: el archivo en PSRAM no se usa mas. */
    heap_caps_free(s_buf);
    s_buf = NULL;
    return bg;
}

static void splash_gif_task(void *arg)
{
    (void)arg;

    /* Sin assets-flash, la zona de assets tiene restos del firmware de fabrica:
       el mount falla, y eso es el fallback, no un error fatal. */
    lv_draw_buf_t *bg = NULL;
    esp_err_t err = mount_assets();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "spiffs: %s (falta assets-flash?)", esp_err_to_name(err));
    } else {
        bg = background_from_gif();
    }

    /* Con o sin fondo, la hora y la bateria reemplazan al splash de texto. */
    if (bsp_display_lock(0)) {
        boot_splash_hide_text();
        home_screen_show(bg);
        bsp_display_unlock();
        ESP_LOGI(TAG, "pantalla de inicio visible%s", bg != NULL ? "" : " (sin fondo)");
    }

    ESP_LOGI(TAG, "stack libre: %u bytes", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    vTaskDelete(NULL);
}

void splash_gif_start(void)
{
    if (xTaskCreate(splash_gif_task, "splash_gif", SPLASH_TASK_STACK, NULL,
                    SPLASH_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGW(TAG, "no se pudo crear la task; queda el splash de texto");
    }
}
