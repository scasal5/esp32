/*
 * Ajustes: la ficha tecnica de la placa, leida del hardware cada vez que se
 * abre. Ningun dato esta escrito a mano; si un valor no se puede leer, la fila
 * lo dice en vez de inventarlo.
 *
 * Cinco grupos son fijos y dos cambian solos: la energia y la hora se refrescan
 * con el mismo periodo que la pantalla de inicio.
 */

#include "app_ajustes.h"

#include "pm.h"
#include "svc_wifi.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_psram.h"

#include "lvgl.h"

/* Igual que la pantalla de inicio: cada lectura son transacciones I2C dentro de
   la task de LVGL. */
#define REFRESH_MS       3000

/* Antes de 2024-01-01 la hora nunca se seteo (RTC con bit OS o sin RTC). */
#define EPOCH_VALID_MIN  1704067200

static lv_obj_t *s_power_value;
static lv_obj_t *s_power_note;
static lv_obj_t *s_time_value;
static lv_obj_t *s_time_note;
static lv_timer_t *s_timer;

/* --- Piezas de la ficha ------------------------------------------------ */

/* Etiqueta del grupo: chica, gris y espaciada. Es la unica que lleva tracking:
   en un texto largo el espaciado cansa, en tres palabras en mayuscula ordena. */
static void group_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(label, UI_COL_TEXT_MUTED, 0);
    lv_obj_set_style_text_letter_space(label, 2, 0);
    lv_obj_set_style_pad_top(label, 14, 0);
}

/* El dato: lo que uno vino a leer. */
static lv_obj_t *value(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(label, UI_COL_TEXT, 0);
    return label;
}

/* La aclaracion de abajo: unidades, procedencia, estado. */
static lv_obj_t *note(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(label, UI_COL_TEXT_2, 0);
    return label;
}

static void hairline(lv_obj_t *parent)
{
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(line, UI_COL_HAIRLINE, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_margin_top(line, 14, 0);
}

/* --- Datos que no cambian mientras la pantalla esta abierta ------------ */

static void fill_device(lv_obj_t *parent)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    group_label(parent, "DISPOSITIVO");
    value(parent, "ESP32-S3");

    char text[48];
    /* En ESP-IDF 5 la revision viene como 100 * mayor + menor. */
    snprintf(text, sizeof(text), "rev v%d.%d  ·  %d nucleos  ·  240 MHz",
             chip.revision / 100, chip.revision % 100, chip.cores);
    note(parent, text);
}

static void fill_memory(lv_obj_t *parent)
{
    group_label(parent, "MEMORIA");

    char text[32];
    const size_t psram = esp_psram_get_size();
    if (psram > 0) {
        snprintf(text, sizeof(text), "%u MB PSRAM", (unsigned)(psram / (1024 * 1024)));
    } else {
        snprintf(text, sizeof(text), "sin PSRAM");
    }
    value(parent, text);

    uint32_t flash = 0;
    if (esp_flash_get_size(NULL, &flash) == ESP_OK) {
        snprintf(text, sizeof(text), "%u MB flash", (unsigned)(flash / (1024 * 1024)));
    } else {
        snprintf(text, sizeof(text), "flash: sin lectura");
    }
    note(parent, text);
}

static void fill_network(lv_obj_t *parent)
{
    group_label(parent, "RED");

    const char *ssid = svc_wifi_sta_ssid();
    char ip[16];
    const bool have_ip = svc_wifi_ip(ip, sizeof(ip));

    value(parent, (ssid != NULL && ssid[0] != '\0') ? ssid : "sin red");
    note(parent, have_ip ? ip : "sin IP");

    uint8_t mac[6] = { 0 };
    char text[32];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        snprintf(text, sizeof(text), "MAC: sin lectura");
    }
    note(parent, text);
}

static void fill_software(lv_obj_t *parent)
{
    group_label(parent, "SOFTWARE");

    const esp_app_desc_t *app = esp_app_get_description();
    value(parent, app != NULL ? app->version : "?");

    char text[48];
    snprintf(text, sizeof(text), "ESP-IDF %s", app != NULL ? app->idf_ver : "?");
    note(parent, text);
    if (app != NULL) {
        snprintf(text, sizeof(text), "%s %s", app->date, app->time);
        note(parent, text);
    }
}

/* --- Datos vivos -------------------------------------------------------- */

/* Corre en la task de LVGL (lv_timer): el lock ya esta tomado. */
static void refresh(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    pm_status_t st;
    const bool ok = pm_read(&st) == ESP_OK;

    char text[40];
    if (!ok) {
        lv_label_set_text(s_power_value, "sin PMU");
        lv_label_set_text(s_power_note, "el AXP2101 no responde");
    } else if (!st.present) {
        lv_label_set_text(s_power_value, "sin bateria");
        snprintf(text, sizeof(text), "VBUS %u mV", (unsigned)st.vbus_mv);
        lv_label_set_text(s_power_note, text);
    } else {
        if (st.percent < 0) {
            snprintf(text, sizeof(text), "%u mV", (unsigned)st.batt_mv);
        } else {
            snprintf(text, sizeof(text), "%d%%", st.percent);
        }
        lv_label_set_text(s_power_value, text);

        snprintf(text, sizeof(text), "%u mV  ·  %s", (unsigned)st.batt_mv,
                 st.charging ? "cargando" : (st.vbus ? "USB" : "bateria"));
        lv_label_set_text(s_power_note, text);
    }

    const time_t now = time(NULL);
    if (now < EPOCH_VALID_MIN) {
        lv_label_set_text(s_time_value, "--:--:--");
        lv_label_set_text(s_time_note, "el RTC nunca se puso en hora");
    } else {
        struct tm local;
        localtime_r(&now, &local);   /* TZ "<-03>3", fijada en app_main */
        strftime(text, sizeof(text), "%H:%M:%S", &local);
        lv_label_set_text(s_time_value, text);
        strftime(text, sizeof(text), "%Y-%m-%d  ·  UTC-3", &local);
        lv_label_set_text(s_time_note, text);
    }
}

static void fill_power_and_time(lv_obj_t *parent)
{
    group_label(parent, "ENERGIA");
    s_power_value = value(parent, "...");
    s_power_note = note(parent, "");
    hairline(parent);

    group_label(parent, "HORA");
    s_time_value = value(parent, "--:--:--");
    s_time_note = note(parent, "");
}

/* --- Contrato ----------------------------------------------------------- */

static void ajustes_open(lv_obj_t *root)
{
    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "Ajustes");
    lv_obj_set_style_text_font(title, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, UI_COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* La ficha no entra en 240x284: columna con scroll vertical. El titulo
       queda fijo arriba y solo se mueve el contenido. */
    lv_obj_t *list = lv_obj_create(root);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LV_PCT(100), 284 - 44);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_style_pad_left(list, UI_PAD_SIDE, 0);
    lv_obj_set_style_pad_right(list, UI_PAD_SIDE, 0);
    lv_obj_set_style_pad_bottom(list, 20, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    fill_device(list);
    hairline(list);
    fill_memory(list);
    hairline(list);
    fill_network(list);
    hairline(list);
    fill_power_and_time(list);
    hairline(list);
    fill_software(list);

    /* Primera lectura inmediata: sin esto la energia y la hora quedan en el
       placeholder hasta el primer tick. */
    refresh(NULL);
    s_timer = lv_timer_create(refresh, REFRESH_MS, NULL);
}

static void ajustes_close(void)
{
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_power_value = NULL;
    s_power_note = NULL;
    s_time_value = NULL;
    s_time_note = NULL;
}

const os_app_t app_ajustes = {
    .id = "ajustes",
    .icon = LV_SYMBOL_SETTINGS,
    .name = "Ajustes",
    .open = ajustes_open,
    .close = ajustes_close,
};
