#include "wifi_scan_ui.h"

#include "app_menu.h"
#include "svc_wifi.h"

#include "esp_log.h"

#include "lvgl.h"

static const char *TAG = "wifi_ui";

static lv_obj_t *s_screen;
static lv_obj_t *s_status;
static lv_obj_t *s_list;
static lv_timer_t *s_timer;
static bool s_open;
static volatile bool s_scan_done;

static void wifi_scan_render(void)
{
    if (!s_open || s_list == NULL) {
        return;
    }

    svc_wifi_ap_t results[SVC_WIFI_MAX_RESULTS];
    size_t count = svc_wifi_copy_results(results, SVC_WIFI_MAX_RESULTS);
    lv_obj_clean(s_list);
    if (count == 0) {
        lv_label_set_text(s_status, "sin redes");
        return;
    }

    lv_label_set_text_fmt(s_status, "%u redes", (unsigned)count);
    for (size_t i = 0; i < count; i++) {
        char text[64];
        const char *lock = results[i].open ? "" : "* ";
        lv_snprintf(text, sizeof(text), "%s%s  %d dBm", lock,
                    results[i].ssid, (int)results[i].rssi);
        lv_list_add_button(s_list, NULL, text);
    }
}

static void wifi_scan_tick(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if (!s_scan_done) {
        return;
    }
    s_scan_done = false;
    wifi_scan_render();
}

static void close_async_cb(void *arg)
{
    LV_UNUSED(arg);
    wifi_scan_ui_close();
}

static void close_clicked(lv_event_t *event)
{
    LV_UNUSED(event);
    /* No borrar el boton desde su propio handler. */
    lv_async_call(close_async_cb, NULL);
}

static void create_screen(void)
{
    s_screen = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "WiFi");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF2F4F8), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    s_status = lv_label_create(s_screen);
    lv_label_set_text(s_status, "buscando...");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x8A93A6), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 48);

    s_list = lv_list_create(s_screen);
    lv_obj_set_size(s_list, LV_PCT(100), 150);
    lv_obj_align(s_list, LV_ALIGN_CENTER, 0, 8);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(0x1E2530), 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_list, 12, 0);

    lv_obj_t *close = lv_button_create(s_screen);
    lv_obj_set_size(close, 92, 36);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_add_event_cb(close, close_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close);
    lv_label_set_text(close_label, "Cerrar");
    lv_obj_center(close_label);

    s_timer = lv_timer_create(wifi_scan_tick, 100, NULL);
}

static void on_scan_done(void *arg, esp_event_base_t base, int32_t id,
                         void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;

    /* Solo senala: LVGL corre en el timer de la pantalla, no desde aca. */
    s_scan_done = true;
}

esp_err_t wifi_scan_ui_init(void)
{
    return esp_event_handler_register(SVC_WIFI_EVENT,
                                      SVC_WIFI_EVENT_SCAN_DONE,
                                      on_scan_done, NULL);
}

void wifi_scan_ui_open(void)
{
    if (s_open) {
        return;
    }

    app_menu_close();
    s_scan_done = false;
    s_open = true;
    create_screen();

    esp_err_t err = svc_wifi_scan();
    if (err != ESP_OK) {
        lv_label_set_text(s_status, "WiFi no listo");
        ESP_LOGW(TAG, "scan: %s", esp_err_to_name(err));
    }
}

void wifi_scan_ui_close(void)
{
    if (!s_open) {
        return;
    }
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_screen != NULL) {
        lv_obj_delete(s_screen);
    }
    s_screen = NULL;
    s_status = NULL;
    s_list = NULL;
    s_open = false;
}

bool wifi_scan_ui_is_open(void)
{
    return s_open;
}
