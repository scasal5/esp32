/*
 * Tres estados y un solo boton:
 *
 *   inicio  --BOOT-->  lanzador  --toque-->  app
 *      ^                   |                  |
 *      +-------BOOT--------+--------BOOT------+
 *
 * BOOT siempre significa atras. El lanzador y el root de la app viven en
 * lv_layer_top(), asi que la pantalla de inicio sigue abajo con su reloj
 * corriendo y no hay que reconstruirla al volver.
 */

#include "shell.h"

#include "app_menu.h"
#include "menu_button.h"
#include "ui_theme.h"

#include "esp_log.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "lvgl.h"

static const char *TAG = "shell";

/* Cuatro tarjetas hoy. El registro es un array fijo: sin malloc y sin lista
   enlazada para algo que se llena una vez en el arranque. */
#define SHELL_MAX_APPS 8

static const os_app_t *s_apps[SHELL_MAX_APPS];
static size_t s_app_count;

static const os_app_t *s_current;
static lv_obj_t *s_root;

void shell_register_app(const os_app_t *app)
{
    if (app == NULL || app->id == NULL || app->name == NULL) {
        ESP_LOGW(TAG, "app sin id o sin nombre, ignorada");
        return;
    }
    if (s_app_count >= SHELL_MAX_APPS) {
        ESP_LOGW(TAG, "%s no entra: el registro tiene %d lugares", app->id,
                 SHELL_MAX_APPS);
        return;
    }

    s_apps[s_app_count++] = app;
    ESP_LOGI(TAG, "app registrada %s%s", app->id,
             app->open == NULL ? " (sin implementar)" : "");
}

/* Corre en la task de LVGL. */
static void open_app(const os_app_t *app)
{
    if (app == NULL || app->open == NULL || s_current != NULL) {
        return;
    }

    app_menu_hide();

    /* Root opaco sobre la capa superior: tapa la pantalla de inicio sin
       tocarla, y los toques no la alcanzan. La app no elige nada de esto. */
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, UI_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    s_current = app;
    app->open(s_root);
    ESP_LOGI(TAG, "app abierta %s", app->id);
}

/* Corre en la task de LVGL. */
static void close_app(void)
{
    if (s_current == NULL) {
        return;
    }

    /* El id se guarda antes: despues de close() la app puede haber soltado
       todo, y el puntero del registro es lo unico que sigue siendo valido. */
    const char *id = s_current->id;
    if (s_current->close != NULL) {
        s_current->close();
    }
    s_current = NULL;

    /* Recien ahora: borrar el root se lleva puestos todos los objetos de la
       app, y close() todavia podia necesitarlos. */
    if (s_root != NULL) {
        lv_obj_delete(s_root);
        s_root = NULL;
    }

    ESP_LOGI(TAG, "app cerrada %s", id);
}

static void close_async_cb(void *arg)
{
    LV_UNUSED(arg);
    close_app();
}

void shell_close_app(void)
{
    lv_async_call(close_async_cb, NULL);
}

/* El lanzador avisa que tocaron una tarjeta. Ya viene agendado con
   lv_async_call, asi que aca se puede borrar el lanzador sin problemas. */
static void on_pick(const os_app_t *app)
{
    open_app(app);
}

/* Corre en la task de LVGL (lv_async_call): el lock ya esta tomado. */
static void back_cb(void *arg)
{
    LV_UNUSED(arg);

    if (s_current != NULL) {
        close_app();
        return;
    }
    if (app_menu_visible()) {
        app_menu_hide();
        ESP_LOGI(TAG, "lanzador cerrado");
        return;
    }

    app_menu_show(s_apps, s_app_count, on_pick);
    ESP_LOGI(TAG, "lanzador abierto");
}

/*
 * Corre en la task del loop de eventos por defecto, que tiene 2304 bytes de
 * stack: no alcanza para construir la UI. Solo agenda el cambio en la task de
 * LVGL, que tiene el stack y el contexto correctos.
 */
static void on_ui_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;

    /* lv_async_call tambien es API de LVGL: va con el lock. */
    if (bsp_display_lock(0)) {
        lv_async_call(back_cb, NULL);
        bsp_display_unlock();
    }
}

esp_err_t shell_init(void)
{
    return esp_event_handler_register(UI_EVENT, UI_EVENT_MENU, on_ui_event, NULL);
}
