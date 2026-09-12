#include "fondo_ui.h"

#include "fondo_http.h"
#include "shell.h"
#include "splash_gif.h"
#include "svc_wifi.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"

#include "lvgl.h"

static const char *TAG = "fondo_ui";

static lv_obj_t *s_status;
static lv_obj_t *s_qr_box;
static lv_obj_t *s_qr;
static lv_obj_t *s_hint;
static lv_obj_t *s_ask;
static lv_obj_t *s_ask_id;
static lv_obj_t *s_wait;
static lv_timer_t *s_timer;
static bool s_asking;
static uint32_t s_ask_ticks;
static volatile bool s_got_client;
static volatile bool s_saved;
static volatile bool s_fail;
static fondo_client_t s_client;
static char s_url[40];

#define ASK_TIMEOUT_TICKS 300

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

static void hide_wait(void)
{
    if (s_wait != NULL) {
        lv_obj_add_flag(s_wait, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_wait(void)
{
    if (s_ask != NULL) {
        lv_obj_add_flag(s_ask, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_qr_box != NULL) {
        lv_obj_add_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_wait != NULL) {
        lv_obj_remove_flag(s_wait, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(s_status, "esperando imagen");
}

static void show_qr(void)
{
    s_asking = false;
    hide_wait();
    if (s_ask != NULL) {
        lv_obj_add_flag(s_ask, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_qr_box != NULL) {
        lv_obj_remove_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);
        if (s_qr != NULL && s_url[0] != '\0') {
            lv_qrcode_set_data(s_qr, s_url);
        }
    }
    lv_label_set_text(s_status, "escanea para subir");
    if (s_hint != NULL) {
        lv_label_set_text(s_hint, s_url);
    }
}

static void show_ask(void)
{
    s_asking = true;
    s_ask_ticks = 0;
    hide_wait();
    if (s_qr_box != NULL) {
        lv_obj_add_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ask != NULL) {
        char id[24];
        if (s_client.have_mac) {
            snprintf(id, sizeof(id), "%02X:%02X:%02X:%02X:%02X:%02X",
                     s_client.mac[0], s_client.mac[1], s_client.mac[2],
                     s_client.mac[3], s_client.mac[4], s_client.mac[5]);
        } else {
            snprintf(id, sizeof(id), "%s", s_client.ip);
        }
        if (s_ask_id != NULL) {
            lv_label_set_text(s_ask_id, id);
        }
        lv_obj_remove_flag(s_ask, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text_fmt(s_status, "permitir este celular? %us",
                         (unsigned)(ASK_TIMEOUT_TICKS / 10));
    if (s_client.have_mac) {
        ESP_LOGI(TAG, "ask " MACSTR, MAC2STR(s_client.mac));
    } else {
        ESP_LOGI(TAG, "ask %s", s_client.ip);
    }
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
    fondo_http_deny();
    show_qr();
}

static void allow_cb(void *arg)
{
    LV_UNUSED(arg);
    hide_ask();
    fondo_http_allow();
    show_wait();
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

static void tick(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (s_saved) {
        s_saved = false;
        hide_ask();
        hide_wait();
        lv_label_set_text(s_status, "fondo guardado");
        if (s_hint != NULL) {
            lv_label_set_text(s_hint, "cerra para verlo");
        }
        if (s_qr_box != NULL) {
            lv_obj_add_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    if (s_fail) {
        s_fail = false;
        lv_label_set_text(s_status, "no se pudo guardar");
        return;
    }
    if (s_got_client) {
        s_got_client = false;
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
    }
}

static void close_clicked(lv_event_t *event)
{
    LV_UNUSED(event);
    shell_close_app();
}

static void quitar_cb(void *arg)
{
    LV_UNUSED(arg);
    const esp_err_t err = splash_gif_clear();
    if (s_status != NULL) {
        lv_label_set_text(s_status, err == ESP_OK ? "fondo quitado" : "no se pudo quitar");
    }
    if (s_hint != NULL) {
        lv_label_set_text(s_hint, "home sin imagen");
    }
}

static void quitar_clicked(lv_event_t *event)
{
    LV_UNUSED(event);
    lv_async_call(quitar_cb, NULL);
}

static void add_bottom_actions(lv_obj_t *root, const char *close_text)
{
    lv_obj_t *quitar = lv_button_create(root);
    lv_obj_set_size(quitar, 100, 36);
    lv_obj_align(quitar, LV_ALIGN_BOTTOM_MID, -58, -12);
    lv_obj_add_event_cb(quitar, quitar_clicked, LV_EVENT_CLICKED, NULL);
    style_button(quitar, false);
    lv_obj_t *quitar_label = lv_label_create(quitar);
    lv_label_set_text(quitar_label, "Quitar");
    lv_obj_center(quitar_label);

    lv_obj_t *close = lv_button_create(root);
    lv_obj_set_size(close, 100, 36);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 58, -12);
    lv_obj_add_event_cb(close, close_clicked, LV_EVENT_CLICKED, NULL);
    style_button(close, false);
    lv_obj_t *close_label = lv_label_create(close);
    lv_label_set_text(close_label, close_text);
    lv_obj_center(close_label);
}

static void build_offline(lv_obj_t *root)
{
    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "Fondo");
    lv_obj_set_style_text_font(title, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, UI_COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    s_status = lv_label_create(root);
    lv_label_set_text(s_status, "sin conexion a Internet");
    lv_obj_set_style_text_color(s_status, UI_COL_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_status, UI_FONT_BODY, 0);
    lv_obj_set_width(s_status, LV_PCT(90));
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *note = lv_label_create(root);
    lv_label_set_text(note, "conecta la placa al WiFi y volve");
    lv_obj_set_style_text_color(note, UI_COL_TEXT_2, 0);
    lv_obj_set_style_text_font(note, UI_FONT_BODY, 0);
    lv_obj_set_width(note, LV_PCT(90));
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(note, LV_ALIGN_CENTER, 0, 16);

    add_bottom_actions(root, "Cerrar");
}

static void build_online(lv_obj_t *root)
{
    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "Fondo");
    lv_obj_set_style_text_font(title, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, UI_COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    s_status = lv_label_create(root);
    lv_label_set_text(s_status, "escanea para subir");
    lv_obj_set_style_text_color(s_status, UI_COL_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_status, UI_FONT_BODY, 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 36);

    s_qr_box = lv_obj_create(root);
    lv_obj_remove_style_all(s_qr_box);
    lv_obj_set_size(s_qr_box, LV_PCT(100), 180);
    lv_obj_align(s_qr_box, LV_ALIGN_CENTER, 0, 4);
    lv_obj_remove_flag(s_qr_box, LV_OBJ_FLAG_SCROLLABLE);

    s_qr = lv_qrcode_create(s_qr_box);
    lv_qrcode_set_size(s_qr, 140);
    lv_qrcode_set_dark_color(s_qr, UI_COL_QR_DARK);
    lv_qrcode_set_light_color(s_qr, UI_COL_QR_LIGHT);
    lv_qrcode_set_quiet_zone(s_qr, true);
    lv_obj_align(s_qr, LV_ALIGN_TOP_MID, 0, 0);
    lv_qrcode_set_data(s_qr, s_url);

    s_hint = lv_label_create(s_qr_box);
    lv_label_set_text(s_hint, s_url);
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

    s_wait = lv_obj_create(root);
    lv_obj_remove_style_all(s_wait);
    lv_obj_set_size(s_wait, LV_PCT(100), 180);
    lv_obj_align(s_wait, LV_ALIGN_CENTER, 0, 4);
    lv_obj_remove_flag(s_wait, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_wait, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *spin = lv_spinner_create(s_wait);
    lv_obj_set_size(spin, 72, 72);
    lv_spinner_set_anim_params(spin, 900, 80);
    lv_obj_set_style_arc_color(spin, UI_COL_HAIRLINE, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spin, UI_COL_TEXT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spin, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spin, 6, LV_PART_INDICATOR);
    lv_obj_align(spin, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *wait_l = lv_label_create(s_wait);
    lv_label_set_text(wait_l, "elige la imagen");
    lv_obj_set_style_text_color(wait_l, UI_COL_TEXT_2, 0);
    lv_obj_set_style_text_font(wait_l, UI_FONT_BODY, 0);
    lv_obj_align(wait_l, LV_ALIGN_BOTTOM_MID, 0, -8);

    add_bottom_actions(root, "Cancelar");

    s_timer = lv_timer_create(tick, 100, NULL);
}

static void on_fondo_event(void *arg, esp_event_base_t base, int32_t id,
                           void *data)
{
    (void)arg;
    (void)base;

    if (id == FONDO_EVENT_CLIENT && data != NULL) {
        memcpy(&s_client, data, sizeof(s_client));
        s_got_client = true;
    } else if (id == FONDO_EVENT_SAVED) {
        s_saved = true;
    } else if (id == FONDO_EVENT_FAIL) {
        s_fail = true;
    }
}

esp_err_t fondo_ui_init(void)
{
    return esp_event_handler_register(FONDO_EVENT, ESP_EVENT_ANY_ID,
                                      on_fondo_event, NULL);
}

static void fondo_open(lv_obj_t *root)
{
    s_got_client = false;
    s_saved = false;
    s_fail = false;
    s_asking = false;
    s_url[0] = '\0';
    s_status = NULL;
    s_qr_box = NULL;
    s_qr = NULL;
    s_hint = NULL;
    s_ask = NULL;
    s_ask_id = NULL;
    s_wait = NULL;
    s_timer = NULL;

    char ip[16];
    if (!svc_wifi_connected() || !svc_wifi_ip(ip, sizeof(ip))) {
        build_offline(root);
        return;
    }

    snprintf(s_url, sizeof(s_url), "http://%s/", ip);
    esp_err_t err = fondo_http_start();
    if (err != ESP_OK) {
        build_offline(root);
        lv_label_set_text(s_status, "HTTP no listo");
        ESP_LOGW(TAG, "http: %s", esp_err_to_name(err));
        return;
    }
    build_online(root);
    ESP_LOGI(TAG, "QR %s", s_url);
}

static void fondo_close(void)
{
    fondo_http_stop();
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_status = NULL;
    s_qr_box = NULL;
    s_qr = NULL;
    s_hint = NULL;
    s_ask = NULL;
    s_ask_id = NULL;
    s_wait = NULL;
    s_asking = false;
}

const os_app_t app_fondo = {
    .id = "fondo",
    .icon = LV_SYMBOL_IMAGE,
    .name = "Fondo",
    .open = fondo_open,
    .close = fondo_close,
};
