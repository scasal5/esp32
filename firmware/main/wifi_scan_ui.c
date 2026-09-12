#include "wifi_scan_ui.h"

#include "app_menu.h"
#include "svc_wifi.h"

#include <string.h>

#include "esp_log.h"

#include "lvgl.h"

static const char *TAG = "wifi_ui";

static lv_obj_t *s_screen;
static lv_obj_t *s_status;
static lv_obj_t *s_list;
static lv_obj_t *s_qr_box;
static lv_obj_t *s_qr;
static lv_obj_t *s_hint;
static lv_timer_t *s_timer;
static bool s_open;
static bool s_qr_mode;
static volatile bool s_scan_done;
static volatile bool s_connected;
static volatile bool s_connect_fail;
static svc_wifi_ap_t s_shown[SVC_WIFI_MAX_RESULTS];
static char s_pick_ssid[33];
static bool s_pick_open;

static void show_list_widgets(bool list_on)
{
    if (s_list != NULL) {
        if (list_on) {
            lv_obj_remove_flag(s_list, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_list, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_qr_box != NULL) {
        if (list_on) {
            lv_obj_add_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void show_qr_for_pick(void)
{
    s_qr_mode = true;
    show_list_widgets(false);
    lv_label_set_text(s_status, s_pick_ssid);

    if (s_qr != NULL) {
        lv_qrcode_set_data(s_qr, svc_wifi_prov_qr());
    }
    if (s_hint != NULL) {
        lv_label_set_text_fmt(s_hint, "escanea  %s", svc_wifi_prov_ap_ssid());
    }
}

static void apply_pick(void)
{
    if (s_pick_open) {
        s_qr_mode = false;
        show_list_widgets(true);
        lv_label_set_text(s_status, "solo redes privadas");
        return;
    }

    esp_err_t err = svc_wifi_prov_start(s_pick_ssid);
    if (err != ESP_OK) {
        lv_label_set_text(s_status, "WiFi no listo");
        ESP_LOGW(TAG, "prov: %s", esp_err_to_name(err));
        return;
    }
    show_qr_for_pick();
}

static void pick_cb(void *arg)
{
    LV_UNUSED(arg);
    apply_pick();
}

static void net_clicked(lv_event_t *event)
{
    const svc_wifi_ap_t *ap = lv_event_get_user_data(event);
    if (ap == NULL) {
        return;
    }
    strncpy(s_pick_ssid, ap->ssid, sizeof(s_pick_ssid) - 1);
    s_pick_ssid[sizeof(s_pick_ssid) - 1] = '\0';
    s_pick_open = ap->open;
    lv_async_call(pick_cb, NULL);
}

static void wifi_scan_render(void)
{
    if (!s_open || s_list == NULL || s_qr_mode) {
        return;
    }

    size_t count = svc_wifi_copy_results(s_shown, SVC_WIFI_MAX_RESULTS);
    lv_obj_clean(s_list);
    if (count == 0) {
        lv_label_set_text(s_status, "sin redes");
        return;
    }

    lv_label_set_text_fmt(s_status, "%u redes", (unsigned)count);
    for (size_t i = 0; i < count; i++) {
        char text[64];
        const char *lock = s_shown[i].open ? "" : "* ";
        lv_snprintf(text, sizeof(text), "%s%s  %d dBm", lock,
                    s_shown[i].ssid, (int)s_shown[i].rssi);
        lv_obj_t *btn = lv_list_add_button(s_list, NULL, text);
        lv_obj_add_event_cb(btn, net_clicked, LV_EVENT_CLICKED, &s_shown[i]);
    }
}

static void close_later(lv_timer_t *timer)
{
    lv_timer_delete(timer);
    wifi_scan_ui_close();
}

static void wifi_scan_tick(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (s_connected) {
        s_connected = false;
        lv_label_set_text(s_status, "conectado");
        lv_timer_t *t = lv_timer_create(close_later, 1200, NULL);
        lv_timer_set_repeat_count(t, 1);
        return;
    }
    if (s_connect_fail) {
        s_connect_fail = false;
        lv_label_set_text(s_status, "no se pudo conectar");
        return;
    }
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
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    s_status = lv_label_create(s_screen);
    lv_label_set_text(s_status, "buscando...");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x8A93A6), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 36);

    s_list = lv_list_create(s_screen);
    lv_obj_set_size(s_list, LV_PCT(100), 150);
    lv_obj_align(s_list, LV_ALIGN_CENTER, 0, 4);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(0x1E2530), 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_list, 12, 0);

    s_qr_box = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_qr_box);
    lv_obj_set_size(s_qr_box, LV_PCT(100), 180);
    lv_obj_align(s_qr_box, LV_ALIGN_CENTER, 0, 4);
    lv_obj_remove_flag(s_qr_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);

    s_qr = lv_qrcode_create(s_qr_box);
    lv_qrcode_set_size(s_qr, 140);
    lv_qrcode_set_dark_color(s_qr, lv_color_hex(0x101418));
    lv_qrcode_set_light_color(s_qr, lv_color_hex(0xFFFFFF));
    lv_qrcode_set_quiet_zone(s_qr, true);
    lv_obj_align(s_qr, LV_ALIGN_TOP_MID, 0, 0);

    s_hint = lv_label_create(s_qr_box);
    lv_label_set_text(s_hint, "");
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0x8A93A6), 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *close = lv_button_create(s_screen);
    lv_obj_set_size(close, 92, 36);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(close, close_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close);
    lv_label_set_text(close_label, "Cerrar");
    lv_obj_center(close_label);

    s_timer = lv_timer_create(wifi_scan_tick, 100, NULL);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id,
                          void *data)
{
    (void)arg;
    (void)base;
    (void)data;

    if (id == SVC_WIFI_EVENT_SCAN_DONE) {
        s_scan_done = true;
    } else if (id == SVC_WIFI_EVENT_CONNECTED) {
        s_connected = true;
    } else if (id == SVC_WIFI_EVENT_CONNECT_FAIL) {
        s_connect_fail = true;
    }
}

esp_err_t wifi_scan_ui_init(void)
{
    return esp_event_handler_register(SVC_WIFI_EVENT, ESP_EVENT_ANY_ID,
                                      on_wifi_event, NULL);
}

void wifi_scan_ui_open(void)
{
    if (s_open) {
        return;
    }

    app_menu_close();
    s_scan_done = false;
    s_connected = false;
    s_connect_fail = false;
    s_qr_mode = false;
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
    svc_wifi_prov_stop();
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
    s_qr_box = NULL;
    s_qr = NULL;
    s_hint = NULL;
    s_qr_mode = false;
    s_open = false;
}

bool wifi_scan_ui_is_open(void)
{
    return s_open;
}
