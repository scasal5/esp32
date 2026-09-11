#include "splash_gif.h"
#include "boot_splash.h"

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

/* El archivo entero vive en PSRAM mientras exista el objeto lv_gif. */
#define SPLASH_MAX_BYTES  (1024 * 1024)

/* 8 KB: montar SPIFFS y decodificar el frame 0 corren en esta task. El log
   final informa cuanto sobro, para ajustarlo con datos. */
#define SPLASH_TASK_STACK  8192

/* Por debajo de la task de LVGL de esp_lvgl_port (prioridad 4): leer la flash
   no le quita CPU al render. */
#define SPLASH_TASK_PRIO   2

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
        /* En la placa no se escala: el GIF tiene que entrar tal cual. */
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

static bool show_frame0(size_t len)
{
    memset(&s_dsc, 0, sizeof(s_dsc));
    s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_dsc.data = s_buf;
    s_dsc.data_size = len;

    /* Timeout 0 en esp_lvgl_port = esperar sin limite. No usar portMAX_DELAY:
       el valor esta en milisegundos y desborda al pasar a ticks. */
    if (!bsp_display_lock(0)) {
        return false;
    }

    lv_obj_t *gif = lv_gif_create(lv_screen_active());
    /* ARGB8888, antes de set_src: es el unico formato en el que lv_gif respeta
       el indice transparente del GIF (alfa 0) y el fondo del splash se ve a
       traves. En RGB565 los pixels transparentes se pintan opacos con el color
       de fondo del GIF. Cuesta 4 bytes por pixel en PSRAM (224 KB a 240x234). */
    lv_gif_set_color_format(gif, LV_COLOR_FORMAT_ARGB8888);

    int64_t t0 = esp_timer_get_time();
    /* Decodifica el frame 0 con el lock tomado, y ademas arranca el timer. */
    lv_gif_set_src(gif, &s_dsc);
    int64_t decode_us = esp_timer_get_time() - t0;

    /* Fase 1a: solo el frame 0. En la misma seccion bloqueada, el timer no
       llega a correr. La animacion (lv_gif_resume) es la fase 1b. */
    lv_gif_pause(gif);

    bool ok = lv_gif_is_loaded(gif);
    if (ok) {
        lv_obj_center(gif);
        boot_splash_hide_text();
    } else {
        lv_obj_delete(gif);
    }

    bsp_display_unlock();

    if (ok) {
        ESP_LOGI(TAG, "frame 0 visible (%u bytes, decode %lld ms)", (unsigned)len,
                 (long long)(decode_us / 1000));
    } else {
        ESP_LOGW(TAG, "lv_gif no cargo el GIF");
    }
    return ok;
}

static void splash_gif_task(void *arg)
{
    (void)arg;

    /* Sin assets-flash, la zona de assets tiene restos del firmware de fabrica:
       el mount falla, y eso es el fallback, no un error fatal. */
    esp_err_t err = mount_assets();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "spiffs: %s (falta assets-flash?)", esp_err_to_name(err));
    } else {
        size_t len = read_splash();
        if (len > 0 && !show_frame0(len)) {
            heap_caps_free(s_buf);
            s_buf = NULL;
        }
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
