#include "boot_splash.h"
#include "pm.h"
#include "pm_format.h"

#include "bsp/esp-bsp.h"
#include "lvgl.h"

/* Periodo de refresco del estado de bateria. Cada tick hace unas pocas
   transacciones I2C dentro de la task de LVGL, asi que conviene que sea
   holgado: no hay nada que mirar a mayor frecuencia. */
#define BATTERY_REFRESH_MS 3000

static lv_obj_t *s_title = NULL;
static lv_obj_t *s_status = NULL;
static lv_timer_t *s_battery_timer = NULL;

/*
 * Corre en el contexto de la task de LVGL, invocada por lv_timer. Esa task ya
 * tiene tomado el lock, por eso aca NO se llama a bsp_display_lock().
 *
 * Solo lee el PMU. No configura carga ni toca rieles.
 */
static void battery_update(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (s_status == NULL) {
        return;
    }

    pm_status_t st;
    char text[24];
    pm_format_label(pm_read(&st) == ESP_OK ? &st : NULL, text, sizeof(text));
    lv_label_set_text(s_status, text);
}

void boot_splash_show(void)
{
    /* bsp_display_start() ya arranco la task de LVGL: todo acceso a la API
       tiene que ir entre lock y unlock. */
    if (!bsp_display_lock(0)) {
        return;
    }

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_title = lv_label_create(scr);
    lv_label_set_text(s_title, "ws183-os");
    lv_obj_set_style_text_color(s_title, lv_color_hex(0xF2F4F8), 0);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_20, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, -12);

    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "...");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x8A93A6), 0);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 16);

    /* Primera lectura inmediata: sin esto la pantalla mostraria el placeholder
       durante los primeros BATTERY_REFRESH_MS. */
    battery_update(NULL);

    s_battery_timer = lv_timer_create(battery_update, BATTERY_REFRESH_MS, NULL);

    bsp_display_unlock();
}

void boot_splash_hide_text(void)
{
    /* Timeout 0 en esp_lvgl_port = esperar sin limite. El mutex es recursivo,
       asi que funciona tambien si quien llama ya tiene el lock. */
    if (!bsp_display_lock(0)) {
        return;
    }

    /* La pantalla de inicio lee la bateria por su cuenta: dos timers harian
       el doble de transacciones I2C dentro de la task de LVGL. */
    if (s_battery_timer != NULL) {
        lv_timer_delete(s_battery_timer);
        s_battery_timer = NULL;
    }

    /* Solo las etiquetas propias: la pantalla de inicio cuelga de la misma
       pantalla y no hay que ocultarla. */
    if (s_title != NULL) {
        lv_obj_add_flag(s_title, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_status != NULL) {
        lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    }

    bsp_display_unlock();
}
