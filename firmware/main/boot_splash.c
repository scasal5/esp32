#include "boot_splash.h"
#include "pm.h"

#include "bsp/esp-bsp.h"
#include "lvgl.h"

/* Periodo de refresco del estado de bateria. Cada tick hace unas pocas
   transacciones I2C dentro de la task de LVGL, asi que conviene que sea
   holgado: no hay nada que mirar a mayor frecuencia. */
#define BATTERY_REFRESH_MS 3000

static lv_obj_t *s_status = NULL;

/*
 * Corre en el contexto de la task de LVGL, invocada por lv_timer. Esa task ya
 * tiene tomado el lock, por eso aca NO se llama a bsp_display_lock(): hacerlo
 * seria un segundo lock desde el mismo hilo.
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
    if (pm_read(&st) != ESP_OK) {
        lv_label_set_text(s_status, "sin PMU");
        return;
    }

    if (!st.present) {
        lv_label_set_text(s_status, "sin bateria");
    } else if (st.percent < 0) {
        /* El PMU no pudo estimar el porcentaje: mostramos la tension medida,
           que siempre es un dato real. */
        lv_label_set_text_fmt(s_status, "%u mV%s",
                              (unsigned)st.batt_mv, st.charging ? " USB" : "");
    } else {
        lv_label_set_text_fmt(s_status, "%d%%%s",
                              st.percent, st.charging ? " USB" : "");
    }
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

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "ws183-os");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF2F4F8), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -12);

    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "...");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x8A93A6), 0);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 16);

    /* Primera lectura inmediata: sin esto la pantalla mostraria el placeholder
       durante los primeros BATTERY_REFRESH_MS. */
    battery_update(NULL);

    lv_timer_create(battery_update, BATTERY_REFRESH_MS, NULL);

    bsp_display_unlock();
}
