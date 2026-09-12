#include "home_screen.h"
#include "pm.h"
#include "svc_wifi.h"
#include "ui_theme.h"

#include <stdbool.h>
#include <stdio.h>
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
static lv_obj_t *s_wifi = NULL;
static lv_obj_t *s_battery = NULL;
static lv_obj_t *s_usb = NULL;
static time_t s_shown = -1;
static bool s_wifi_shown = true;
static bool s_usb_shown = true;

static void set_visible(lv_obj_t *obj, bool vis, bool *was_shown)
{
    if (vis == *was_shown) {
        return;
    }
    *was_shown = vis;
    if (vis) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Corre en la task de LVGL (lv_timer): el lock ya esta tomado. */
static void clock_update(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    time_t now = time(NULL);
    if (now != s_shown) {
        s_shown = now;

        if (now < EPOCH_VALID_MIN) {
            lv_label_set_text(s_clock, "--:--:--");
        } else {
            struct tm local;
            localtime_r(&now, &local);   /* TZ "<-03>3", fijada en app_main */
            lv_label_set_text_fmt(s_clock, "%02d:%02d:%02d",
                                  local.tm_hour, local.tm_min, local.tm_sec);
        }
    }

    /* WiFi en la barra solo con IP. */
    set_visible(s_wifi, svc_wifi_connected(), &s_wifi_shown);
}

/* Corre en la task de LVGL (lv_timer): el lock ya esta tomado. */
static void battery_update(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    pm_status_t st;
    const bool ok = pm_read(&st) == ESP_OK;
    const pm_status_t *p = ok ? &st : NULL;

    char text[16];
    if (p == NULL) {
        snprintf(text, sizeof(text), "sin PMU");
    } else if (!p->present) {
        snprintf(text, sizeof(text), "sin bateria");
    } else if (p->percent < 0) {
        snprintf(text, sizeof(text), "%u mV", (unsigned)p->batt_mv);
    } else {
        snprintf(text, sizeof(text), "%d%%", p->percent);
    }
    lv_label_set_text(s_battery, text);

    set_visible(s_usb, p != NULL && p->vbus, &s_usb_shown);
}

void home_screen_show(lv_draw_buf_t *bg)
{
    lv_obj_t *scr = lv_screen_active();

    if (bg != NULL) {
        lv_obj_t *img = lv_image_create(scr);
        lv_image_set_src(img, bg);
        lv_obj_center(img);
    }

    /* La hora manda: ocupa su propia linea en la escala grande, y debajo va una
       fila de etiquetas chicas con el resto del estado. Las que no aplican se
       ocultan, y el flex cierra el hueco. */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), 84);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_pad_left(bar, UI_PAD_SIDE, 0);
    lv_obj_set_style_pad_right(bar, UI_PAD_SIDE, 0);
    lv_obj_set_style_pad_top(bar, 8, 0);
    lv_obj_set_style_pad_row(bar, 2, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    s_clock = lv_label_create(bar);
    lv_obj_set_style_text_color(s_clock, UI_COL_TEXT, 0);
    lv_obj_set_style_text_font(s_clock, UI_FONT_DISPLAY, 0);

    lv_obj_t *row = lv_obj_create(bar);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    s_battery = lv_label_create(row);
    lv_obj_set_style_text_color(s_battery, UI_COL_TEXT_2, 0);
    lv_obj_set_style_text_font(s_battery, UI_FONT_BODY, 0);

    s_wifi = lv_label_create(row);
    lv_label_set_text(s_wifi, "WiFi");
    lv_obj_set_style_text_color(s_wifi, UI_COL_TEXT_2, 0);
    lv_obj_set_style_text_font(s_wifi, UI_FONT_BODY, 0);

    s_usb = lv_label_create(row);
    lv_label_set_text(s_usb, "USB");
    lv_obj_set_style_text_color(s_usb, UI_COL_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_usb, UI_FONT_BODY, 0);

    s_wifi_shown = true;
    s_usb_shown = true;
    set_visible(s_wifi, false, &s_wifi_shown);
    set_visible(s_usb, false, &s_usb_shown);

    /* Primera lectura inmediata: sin esto las etiquetas quedan vacias hasta el
       primer tick de cada timer. */
    clock_update(NULL);
    battery_update(NULL);

    lv_timer_create(clock_update, CLOCK_POLL_MS, NULL);
    lv_timer_create(battery_update, BATTERY_REFRESH_MS, NULL);
}
