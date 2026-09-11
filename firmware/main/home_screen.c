#include "home_screen.h"
#include "pm.h"
#include "pm_format.h"

#include <time.h>

/* La hora se revisa 5 veces por segundo y la etiqueta cambia solo cuando cambia
   el segundo: en pantalla el segundo avanza con menos de 200 ms de atraso, sin
   redibujar de mas. */
#define CLOCK_POLL_MS       200

/* Igual que el splash de texto: cada lectura son transacciones I2C dentro de la
   task de LVGL (deuda de fase 2, svc_pm con eventos). */
#define BATTERY_REFRESH_MS  3000

/* Antes de 2024-01-01 la hora nunca se seteo (RTC con bit OS o sin RTC). */
#define EPOCH_VALID_MIN     1704067200

static lv_obj_t *s_clock = NULL;
static lv_obj_t *s_battery = NULL;
static time_t s_shown = -1;

/* Corre en la task de LVGL (lv_timer): el lock ya esta tomado. */
static void clock_update(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    time_t now = time(NULL);
    if (now == s_shown) {
        return;
    }
    s_shown = now;

    if (now < EPOCH_VALID_MIN) {
        lv_label_set_text(s_clock, "--:--:--");
        return;
    }

    struct tm local;
    localtime_r(&now, &local);   /* TZ "<-03>3", fijada en app_main */
    lv_label_set_text_fmt(s_clock, "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
}

/* Corre en la task de LVGL (lv_timer): el lock ya esta tomado. */
static void battery_update(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    pm_status_t st;
    char text[24];
    pm_format_label(pm_read(&st) == ESP_OK ? &st : NULL, text, sizeof(text));
    lv_label_set_text(s_battery, text);
}

void home_screen_show(lv_draw_buf_t *bg)
{
    lv_obj_t *scr = lv_screen_active();

    if (bg != NULL) {
        lv_obj_t *img = lv_image_create(scr);
        lv_image_set_src(img, bg);
        lv_obj_center(img);
    }

    s_clock = lv_label_create(scr);
    lv_obj_set_style_text_color(s_clock, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_clock, &lv_font_montserrat_36, 0);
    lv_obj_align(s_clock, LV_ALIGN_CENTER, 0, -14);

    s_battery = lv_label_create(scr);
    lv_obj_set_style_text_color(s_battery, lv_color_hex(0xD8DEE9), 0);
    lv_obj_set_style_text_font(s_battery, &lv_font_montserrat_20, 0);
    lv_obj_align(s_battery, LV_ALIGN_CENTER, 0, 24);

    /* Primera lectura inmediata: sin esto las etiquetas quedan vacias hasta el
       primer tick de cada timer. */
    clock_update(NULL);
    battery_update(NULL);

    lv_timer_create(clock_update, CLOCK_POLL_MS, NULL);
    lv_timer_create(battery_update, BATTERY_REFRESH_MS, NULL);
}
