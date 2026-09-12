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

/* Mientras una transicion corre, BOOT y los toques se ignoran: un click a mitad
   de fade no puede apilar un segundo cierre ni dejar overlays huerfanos. */
static bool s_busy;

/* Solo hay una transicion a la vez, asi que el callback de fin vive en un unico
   lugar y no hace falta reservar nada. */
static void (*s_fade_done)(void *);

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

static void fade_exec(void *obj, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

static void fade_completed(lv_anim_t *anim)
{
    void (*done)(void *) = s_fade_done;
    void *obj = lv_anim_get_user_data(anim);

    s_fade_done = NULL;
    s_busy = false;

    if (done != NULL) {
        done(obj);
    }
}

/*
 * Unica animacion del firmware: la opacidad de un objeto entero, con la
 * duracion y la curva de ui_theme.h. `done` corre recien al terminar, nunca a
 * mitad de camino: borrar el objeto antes deja la animacion escribiendo sobre
 * memoria liberada. Sin objeto, `done` corre ya.
 */
static void shell_fade(lv_obj_t *obj, bool in, void (*done)(void *))
{
    if (obj == NULL) {
        if (done != NULL) {
            done(NULL);
        }
        return;
    }

    s_fade_done = done;
    s_busy = true;

    const lv_opa_t from = in ? UI_FADE_FROM : UI_FADE_TO;
    const lv_opa_t to = in ? UI_FADE_TO : UI_FADE_FROM;

    /* El estado inicial se fija antes de arrancar: si el primer frame de la
       animacion tarda, el objeto ya esta donde tiene que estar. */
    lv_obj_set_style_opa(obj, from, 0);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_user_data(&anim, obj);
    lv_anim_set_exec_cb(&anim, fade_exec);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_duration(&anim, UI_MOTION_MS);
    lv_anim_set_path_cb(&anim, UI_MOTION_PATH);
    lv_anim_set_completed_cb(&anim, fade_completed);
    lv_anim_start(&anim);
}

/* La app elegida espera aca mientras el lanzador se desvanece. */
static const os_app_t *s_pending;

/* Fin del fade out del lanzador: recien ahora se lo puede borrar. */
static void menu_gone(void *obj)
{
    LV_UNUSED(obj);
    app_menu_hide();
    ESP_LOGI(TAG, "lanzador cerrado");
}

/* Fin del fade out del lanzador cuando lo que sigue es abrir una app. */
static void mount_app(void *obj)
{
    LV_UNUSED(obj);
    app_menu_hide();

    const os_app_t *app = s_pending;
    s_pending = NULL;
    if (app == NULL) {
        return;
    }

    /* Root opaco sobre la capa superior: tapa la pantalla de inicio sin
       tocarla, y los toques no la alcanzan. La app no elige nada de esto. */
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, UI_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    /* Nace invisible: la app construye adentro sin enterarse del fade. */
    lv_obj_set_style_opa(s_root, UI_FADE_FROM, 0);

    s_current = app;
    app->open(s_root);
    ESP_LOGI(TAG, "app abierta %s", app->id);

    shell_fade(s_root, true, NULL);
}

/* Corre en la task de LVGL. */
static void open_app(const os_app_t *app)
{
    if (app == NULL || app->open == NULL || s_current != NULL) {
        return;
    }

    s_pending = app;
    shell_fade(app_menu_obj(), false, mount_app);
}

/* Fin del fade out de la app. */
static void unmount_app(void *obj)
{
    LV_UNUSED(obj);

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

/* Corre en la task de LVGL. */
static void close_app(void)
{
    if (s_current == NULL) {
        return;
    }
    shell_fade(s_root, false, unmount_app);
}

static void close_async_cb(void *arg)
{
    LV_UNUSED(arg);
    if (s_busy) {
        return;
    }
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
    if (s_busy) {
        return;
    }
    open_app(app);
}

/* Corre en la task de LVGL (lv_async_call): el lock ya esta tomado. */
static void back_cb(void *arg)
{
    LV_UNUSED(arg);

    /* Un BOOT a mitad de transicion no hace nada: sin esto se apilarian dos
       cierres sobre el mismo objeto. */
    if (s_busy) {
        return;
    }

    if (s_current != NULL) {
        close_app();
        return;
    }
    if (app_menu_visible()) {
        shell_fade(app_menu_obj(), false, menu_gone);
        return;
    }

    app_menu_show(s_apps, s_app_count, on_pick);
    ESP_LOGI(TAG, "lanzador abierto");
    shell_fade(app_menu_obj(), true, NULL);
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
