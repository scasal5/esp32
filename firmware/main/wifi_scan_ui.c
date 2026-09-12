#include "wifi_scan_ui.h"

#include "shell.h"
#include "svc_wifi.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"

#include "lvgl.h"

static const char *TAG = "wifi_ui";

#define PORTAL_URL "http://192.168.4.1/"

static lv_obj_t *s_status;
static lv_obj_t *s_list;
static lv_obj_t *s_qr_box;
static lv_obj_t *s_qr;
static lv_obj_t *s_hint;
static lv_obj_t *s_ask;
static lv_obj_t *s_ask_id;
static lv_timer_t *s_timer;
static bool s_qr_mode;
static bool s_asking;
static uint32_t s_ask_ticks;
static volatile bool s_scan_done;
static volatile bool s_connected;
static volatile bool s_connect_fail;
static volatile bool s_prov_client;
static volatile bool s_prov_gone;
static svc_wifi_prov_client_t s_client;
static svc_wifi_ap_t s_shown[SVC_WIFI_MAX_RESULTS];
static char s_pick_ssid[33];
static bool s_pick_open;

#define ASK_TIMEOUT_TICKS 300  /* 30 s a 100 ms */

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
        lv_label_set_text(s_hint, "escanea para unirte");
    }
}

static void show_portal_qr(void)
{
    if (!s_qr_mode || s_qr == NULL) {
        return;
    }
    if (s_ask != NULL) {
        lv_obj_add_flag(s_ask, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_qr_box != NULL) {
        lv_obj_remove_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);
    }
    lv_qrcode_set_data(s_qr, PORTAL_URL);
    lv_label_set_text(s_status, "escanea otra vez");
    if (s_hint != NULL) {
        lv_label_set_text(s_hint, "abre la clave");
    }
    ESP_LOGI(TAG, "QR portal %s", PORTAL_URL);
}

static void show_ask(void)
{
    s_asking = true;
    s_ask_ticks = 0;
    if (s_qr_box != NULL) {
        lv_obj_add_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ask != NULL) {
        char id[24];
        snprintf(id, sizeof(id), "%02X:%02X:%02X:%02X:%02X:%02X",
                 s_client.mac[0], s_client.mac[1], s_client.mac[2],
                 s_client.mac[3], s_client.mac[4], s_client.mac[5]);
        if (s_ask_id != NULL) {
            lv_label_set_text(s_ask_id, id);
        }
        lv_obj_remove_flag(s_ask, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text_fmt(s_status, "permitir este celular? %us",
                         (unsigned)(ASK_TIMEOUT_TICKS / 10));
    ESP_LOGI(TAG, "ask " MACSTR, MAC2STR(s_client.mac));
}

static void hide_ask(void)
{
    s_asking = false;
    if (s_ask != NULL) {
        lv_obj_add_flag(s_ask, LV_OBJ_FLAG_HIDDEN);
    }
}

static void deny_now(void)
{
    hide_ask();
    svc_wifi_prov_deny();
    show_qr_for_pick();
}

static void allow_cb(void *arg)
{
    LV_UNUSED(arg);
    hide_ask();
    svc_wifi_prov_allow();
    show_portal_qr();
}

static void deny_cb(void *arg)
{
    LV_UNUSED(arg);
    deny_now();
}

static void allow_clicked(lv_event_t *event)
{
    LV_UNUSED(event);
    lv_async_call(allow_cb, NULL);
}

static void deny_clicked(lv_event_t *event)
{
    LV_UNUSED(event);
    lv_async_call(deny_cb, NULL);
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
    if (s_list == NULL || s_qr_mode) {
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
        /* Sin fondo propio: cada fila se apoya en la superficie de la lista. */
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_text_color(btn, UI_COL_TEXT, 0);
        lv_obj_set_style_text_font(btn, UI_FONT_BODY, 0);
        lv_obj_add_event_cb(btn, net_clicked, LV_EVENT_CLICKED, &s_shown[i]);
    }
}

static void close_later(lv_timer_t *timer)
{
    lv_timer_delete(timer);
    shell_close_app();
}

static void wifi_scan_tick(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (s_connected) {
        s_connected = false;
        lv_label_set_text(s_status, "conectado");
        lv_timer_t *t = lv_timer_create(close_later, 10000, NULL);
        lv_timer_set_repeat_count(t, 1);
        return;
    }
    if (s_connect_fail) {
        s_connect_fail = false;
        lv_label_set_text(s_status, "no se pudo conectar");
        return;
    }
    if (s_prov_gone) {
        s_prov_gone = false;
        if (s_asking) {
            hide_ask();
            show_qr_for_pick();
        }
        return;
    }
    if (s_prov_client) {
        s_prov_client = false;
        show_ask();
        return;
    }
    if (s_asking) {
        s_ask_ticks++;
        if (s_ask_ticks >= ASK_TIMEOUT_TICKS) {
            deny_now();
            return;
        }
        if ((s_ask_ticks % 10) == 0) {
            unsigned left = (ASK_TIMEOUT_TICKS - s_ask_ticks) / 10;
            lv_label_set_text_fmt(s_status, "permitir este celular? %us", left);
        }
        return;
    }
    if (!s_scan_done) {
        return;
    }
    s_scan_done = false;
    wifi_scan_render();
}

/* La app no se cierra sola: le pide al shell volver a la pantalla de inicio,
   igual que si el usuario apretara BOOT. */
static void close_clicked(lv_event_t *event)
{
    LV_UNUSED(event);
    shell_close_app();
}

/*
 * El tema por defecto de LVGL pinta los botones de azul. Aca la UI es
 * monocroma: la accion afirmativa se invierte (claro sobre fondo) y el resto
 * usa la superficie de las tarjetas.
 */
static void style_button(lv_obj_t *btn, bool primary)
{
    lv_obj_set_style_bg_color(btn, primary ? UI_COL_TEXT : UI_COL_SURFACE, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_text_color(btn, primary ? UI_COL_BG : UI_COL_TEXT, 0);
    lv_obj_set_style_text_font(btn, UI_FONT_BODY, 0);
}

/* Todo cuelga de root. El fondo, la capa y el bloqueo de toques los pone el
   shell: esta pantalla no sabe donde la estan mostrando. */
static void build(lv_obj_t *root)
{
    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "WiFi");
    lv_obj_set_style_text_font(title, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, UI_COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    s_status = lv_label_create(root);
    lv_label_set_text(s_status, "buscando...");
    lv_obj_set_style_text_color(s_status, UI_COL_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_status, UI_FONT_BODY, 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 36);

    s_list = lv_list_create(root);
    lv_obj_set_size(s_list, LV_PCT(100), 150);
    lv_obj_align(s_list, LV_ALIGN_CENTER, 0, 4);
    lv_obj_set_style_bg_color(s_list, UI_COL_SURFACE, 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_list, UI_RADIUS, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);

    s_qr_box = lv_obj_create(root);
    lv_obj_remove_style_all(s_qr_box);
    lv_obj_set_size(s_qr_box, LV_PCT(100), 180);
    lv_obj_align(s_qr_box, LV_ALIGN_CENTER, 0, 4);
    lv_obj_remove_flag(s_qr_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);

    s_qr = lv_qrcode_create(s_qr_box);
    lv_qrcode_set_size(s_qr, 140);
    lv_qrcode_set_dark_color(s_qr, UI_COL_QR_DARK);
    lv_qrcode_set_light_color(s_qr, UI_COL_QR_LIGHT);
    lv_qrcode_set_quiet_zone(s_qr, true);
    lv_obj_align(s_qr, LV_ALIGN_TOP_MID, 0, 0);

    s_hint = lv_label_create(s_qr_box);
    lv_label_set_text(s_hint, "");
    lv_obj_set_style_text_color(s_hint, UI_COL_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_hint, UI_FONT_BODY, 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, 0);

    s_ask = lv_obj_create(root);
    lv_obj_remove_style_all(s_ask);
    lv_obj_set_size(s_ask, LV_PCT(100), 180);
    lv_obj_align(s_ask, LV_ALIGN_CENTER, 0, 4);
    lv_obj_remove_flag(s_ask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ask, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *ask_title = lv_label_create(s_ask);
    lv_label_set_text(ask_title, "dispositivo");
    lv_obj_set_style_text_color(ask_title, UI_COL_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(ask_title, UI_FONT_BODY, 0);
    lv_obj_align(ask_title, LV_ALIGN_TOP_MID, 0, 8);

    s_ask_id = lv_label_create(s_ask);
    lv_label_set_text(s_ask_id, "--");
    lv_obj_set_width(s_ask_id, LV_PCT(100));
    lv_obj_set_style_text_align(s_ask_id, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_ask_id, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(s_ask_id, UI_COL_TEXT, 0);
    lv_obj_align(s_ask_id, LV_ALIGN_TOP_MID, 0, 32);

    lv_obj_t *yes = lv_button_create(s_ask);
    lv_obj_set_size(yes, 100, 40);
    lv_obj_align(yes, LV_ALIGN_BOTTOM_MID, -58, 0);
    lv_obj_add_event_cb(yes, allow_clicked, LV_EVENT_CLICKED, NULL);
    style_button(yes, true);
    lv_obj_t *yes_l = lv_label_create(yes);
    lv_label_set_text(yes_l, "Si");
    lv_obj_center(yes_l);

    lv_obj_t *no = lv_button_create(s_ask);
    lv_obj_set_size(no, 100, 40);
    lv_obj_align(no, LV_ALIGN_BOTTOM_MID, 58, 0);
    lv_obj_add_event_cb(no, deny_clicked, LV_EVENT_CLICKED, NULL);
    style_button(no, false);
    lv_obj_t *no_l = lv_label_create(no);
    lv_label_set_text(no_l, "No");
    lv_obj_center(no_l);

    lv_obj_t *close = lv_button_create(root);
    lv_obj_set_size(close, 92, 36);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(close, close_clicked, LV_EVENT_CLICKED, NULL);
    style_button(close, false);
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
    } else if (id == SVC_WIFI_EVENT_PROV_CLIENT && data != NULL) {
        memcpy(&s_client, data, sizeof(s_client));
        s_prov_client = true;
    } else if (id == SVC_WIFI_EVENT_PROV_GONE) {
        s_prov_gone = true;
    }
}

esp_err_t wifi_scan_ui_init(void)
{
    return esp_event_handler_register(SVC_WIFI_EVENT, ESP_EVENT_ANY_ID,
                                      on_wifi_event, NULL);
}

static void wifi_open(lv_obj_t *root)
{
    s_scan_done = false;
    s_connected = false;
    s_connect_fail = false;
    s_prov_client = false;
    s_prov_gone = false;
    s_asking = false;
    s_qr_mode = false;

    build(root);

    esp_err_t err = svc_wifi_scan();
    if (err != ESP_OK) {
        lv_label_set_text(s_status, "WiFi no listo");
        ESP_LOGW(TAG, "scan: %s", esp_err_to_name(err));
    }
}

/*
 * El shell borra el root apenas vuelve de aca, y con el todos los objetos. Lo
 * que hay que soltar a mano es lo que no cuelga de root: el SoftAP y el timer.
 */
static void wifi_close(void)
{
    svc_wifi_prov_stop();
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_status = NULL;
    s_list = NULL;
    s_qr_box = NULL;
    s_qr = NULL;
    s_hint = NULL;
    s_ask = NULL;
    s_ask_id = NULL;
    s_asking = false;
    s_qr_mode = false;
}

const os_app_t app_wifi = {
    .id = "wifi",
    .icon = LV_SYMBOL_WIFI,
    .name = "WiFi",
    .open = wifi_open,
    .close = wifi_close,
};
