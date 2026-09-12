#include "app_menu.h"
#include "ui_theme.h"

#include "bsp/esp-bsp.h"
#include "lvgl.h"

#define CARD_W    116
#define CARD_H    136
#define CARD_GAP  14

static lv_obj_t *s_menu = NULL;
static lv_obj_t *s_hint = NULL;
static void (*s_on_pick)(const os_app_t *app) = NULL;

/* Agendado desde el toque: para cuando corre, el shell puede borrar el
   lanzador sin sacarle el piso al evento que lo disparo. */
static void pick_async(void *arg)
{
    if (s_on_pick != NULL) {
        s_on_pick((const os_app_t *)arg);
    }
}

static void card_clicked(lv_event_t *e)
{
    const os_app_t *app = lv_event_get_user_data(e);

    /* Sin open() la tarjeta ocupa el lugar de una fase de la hoja de ruta y
       solo avisa que viene. Texto ASCII: las fuentes Montserrat de LVGL no
       traen acentos. */
    if (app->open == NULL) {
        lv_label_set_text_fmt(s_hint, "%s: proximamente", app->name);
        return;
    }

    lv_async_call(pick_async, (void *)app);
}

static lv_obj_t *create_card(lv_obj_t *parent, const os_app_t *app)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_style_bg_color(card, UI_COL_SURFACE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, UI_RADIUS, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 10, 0);

    lv_obj_t *icon = lv_label_create(card);
    lv_label_set_text(icon, app->icon != NULL ? app->icon : LV_SYMBOL_FILE);
    lv_obj_set_style_text_font(icon, UI_FONT_DISPLAY, 0);
    lv_obj_set_style_text_color(icon, UI_COL_TEXT, 0);

    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, app->name);
    lv_obj_set_style_text_font(name, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(name, UI_COL_TEXT_2, 0);

    /* Las etiquetas no son clickeables: el toque llega a la tarjeta. */
    lv_obj_add_event_cb(card, card_clicked, LV_EVENT_CLICKED, (void *)app);
    return card;
}

void app_menu_show(const os_app_t *const *apps, size_t count,
                   void (*on_pick)(const os_app_t *app))
{
    if (s_menu != NULL) {
        return;
    }

    s_on_pick = on_pick;

    /* Capa superior: tapa la pantalla de inicio sin tocarla, y la hora sigue
       corriendo debajo. */
    s_menu = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_menu);
    lv_obj_set_size(s_menu, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_menu, UI_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_menu, LV_OPA_90, 0);
    /* Clickeable: los toques no pasan a la pantalla de abajo. */
    lv_obj_add_flag(s_menu, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_menu, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_menu);
    lv_label_set_text(title, "Apps");
    lv_obj_set_style_text_font(title, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(title, UI_COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    /* Carrusel: fila con scroll horizontal, de a una tarjeta, centrada. */
    lv_obj_t *row = lv_obj_create(s_menu);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), CARD_H + 10);
    lv_obj_center(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, CARD_GAP, 0);
    /* Margen a cada lado para que la primera y la ultima tarjeta queden en el
       centro de la pantalla. */
    lv_obj_set_style_pad_left(row, (BSP_LCD_H_RES - CARD_W) / 2, 0);
    lv_obj_set_style_pad_right(row, (BSP_LCD_H_RES - CARD_W) / 2, 0);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(row, LV_SCROLL_SNAP_CENTER);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);

    for (size_t i = 0; i < count; i++) {
        create_card(row, apps[i]);
    }
    lv_obj_update_snap(row, LV_ANIM_OFF);

    s_hint = lv_label_create(s_menu);
    lv_label_set_text(s_hint, "Desliza para ver mas. BOOT cierra");
    lv_obj_set_style_text_color(s_hint, UI_COL_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(s_hint, UI_FONT_BODY, 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -20);
}

void app_menu_hide(void)
{
    if (s_menu == NULL) {
        return;
    }
    lv_obj_delete(s_menu);
    s_menu = NULL;
    s_hint = NULL;
}

bool app_menu_visible(void)
{
    return s_menu != NULL;
}
