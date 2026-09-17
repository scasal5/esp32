/*
 * Flappy Bird — clon jugable para ws183-os (pantalla 240x284).
 *
 * Mecanica clasica: gravedad, flap, tubos, puntaje, game over + reinicio.
 *
 * Entrada:
 *   - Toque (CST816S) o BOOT/GPIO0 = flap / empezar / reiniciar.
 *   - Mientras la app esta abierta, BOOT no abre el menu del shell
 *     (shell_claim_boot). Al cerrar se restaura.
 *
 * Salida (boton de abajo):
 *   El PWR fisico es el PEKEY del AXP2101: apaga la placa en hardware y no
 *   tiene GPIO (menu_button.c: "PWR no tiene GPIO"; README: "PWR apaga por
 *   el AXP2101: el firmware no toca ese pin"). pm_axp2101.cpp es SOLO
 *   LECTURA a proposito (no se reconfigura PEKEY: tocar el PMU apaga rieles).
 *   Fallback: zona "Salir" abajo del suelo con el mismo UX de doble toque
 *   ("Salir? Presiona de nuevo" → shell_close_app). Sin acentos: las
 *   Montserrat de LVGL no traen glifos latinos extendidos.
 *
 * Sprites: placeholders con medidas clasicas (atlas 2x tipico):
 *   bird 34x24, pipe width 52, land height 112, gap 100.
 *   Colores aproximados al dia clasico; listos para swap por assets OSS.
 */

#include "app_flappy.h"

#include "menu_button.h"
#include "shell.h"
#include "ui_theme.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_random.h"

#include "lvgl.h"

static const char *TAG = "flappy";

/* --- Medidas clasicas (2x del atlas original ~17x12 / 26-wide) ---------- */
#define SCR_W          240
#define SCR_H          284

#define BIRD_W         34
#define BIRD_H         24

#define PIPE_W         52
#define PIPE_GAP       100
#define PIPE_CAP_H     26   /* labio del tubo; placeholder del cap clasico */

#define LAND_H         112
#define SKY_H          (SCR_H - LAND_H)

#define BIRD_X         56
#define MAX_PIPES      3
#define PIPE_SPACING   144  /* distancia horizontal entre pares */

#define TICK_MS        33
#define GRAVITY        0.28f
#define FLAP_VY        (-4.6f)
#define PIPE_VX        2.2f
#define MAX_VY         8.0f
#define ROT_MAX        45

/* Colores placeholder (dia clasico, no assets propietarios). */
#define COL_SKY        lv_color_hex(0x4EC0CA)
#define COL_LAND       lv_color_hex(0xDED895)
#define COL_LAND_EDGE  lv_color_hex(0xD2B456)
#define COL_PIPE       lv_color_hex(0x73BF2E)
#define COL_PIPE_EDGE  lv_color_hex(0x558F1F)
#define COL_BIRD       lv_color_hex(0xF8C82E)
#define COL_BIRD_BEAK  lv_color_hex(0xE86101)
#define COL_BIRD_EYE   lv_color_hex(0xFFFFFF)
#define COL_OVERLAY    lv_color_hex(0x000000)

typedef enum {
    ST_READY = 0,
    ST_PLAY,
    ST_DEAD,
} game_state_t;

typedef struct {
    float x;
    int gap_y;     /* centro del hueco en Y */
    bool scored;
    lv_obj_t *top;
    lv_obj_t *top_cap;
    lv_obj_t *bot;
    lv_obj_t *bot_cap;
} pipe_t;

static lv_obj_t *s_root;
static lv_obj_t *s_sky;
static lv_obj_t *s_land;
static lv_obj_t *s_land_stripe;
static lv_obj_t *s_bird;
static lv_obj_t *s_beak;
static lv_obj_t *s_eye;
static lv_obj_t *s_score_lbl;
static lv_obj_t *s_hint_lbl;
static lv_obj_t *s_overlay;
static lv_obj_t *s_overlay_lbl;
static lv_obj_t *s_exit_btn;
static lv_timer_t *s_timer;

static pipe_t s_pipes[MAX_PIPES];
static game_state_t s_state;
static float s_bird_y;
static float s_bird_vy;
static int s_score;
static int s_best;
static bool s_exit_armed;
static uint32_t s_exit_armed_ms;
static bool s_boot_handler_on;

#define EXIT_ARM_MS 2500

/* --- Util -------------------------------------------------------------- */

static int rand_gap_y(void)
{
    /* Hueco centrado con margen respecto al cielo y al suelo. */
    const int margin = PIPE_GAP / 2 + 8;
    const int lo = margin;
    const int hi = SKY_H - margin;
    if (hi <= lo) {
        return SKY_H / 2;
    }
    return lo + (int)(esp_random() % (uint32_t)(hi - lo + 1));
}

static void set_pipe_objs(pipe_t *p)
{
    const int gap_top = p->gap_y - PIPE_GAP / 2;
    const int gap_bot = p->gap_y + PIPE_GAP / 2;
    const int x = (int)p->x;

    /* Tubo superior: desde y=0 hasta gap_top. */
    int top_h = gap_top;
    if (top_h < 0) {
        top_h = 0;
    }
    lv_obj_set_size(p->top, PIPE_W, top_h > 0 ? top_h : 1);
    lv_obj_set_pos(p->top, x, 0);
    if (top_h <= 0) {
        lv_obj_add_flag(p->top, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(p->top_cap, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(p->top, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(p->top_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(p->top_cap, PIPE_W + 4, PIPE_CAP_H);
        lv_obj_set_pos(p->top_cap, x - 2, gap_top - PIPE_CAP_H);
    }

    /* Tubo inferior: desde gap_bot hasta el suelo. */
    int bot_h = SKY_H - gap_bot;
    if (bot_h < 0) {
        bot_h = 0;
    }
    lv_obj_set_size(p->bot, PIPE_W, bot_h > 0 ? bot_h : 1);
    lv_obj_set_pos(p->bot, x, gap_bot);
    if (bot_h <= 0) {
        lv_obj_add_flag(p->bot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(p->bot_cap, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(p->bot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(p->bot_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(p->bot_cap, PIPE_W + 4, PIPE_CAP_H);
        lv_obj_set_pos(p->bot_cap, x - 2, gap_bot);
    }
}

static lv_obj_t *make_pipe_body(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, COL_PIPE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, COL_PIPE_EDGE, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *make_pipe_cap(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, COL_PIPE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, COL_PIPE_EDGE, 0);
    lv_obj_set_style_radius(o, 4, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void update_score_lbl(void)
{
    char buf[24];
    if (s_state == ST_DEAD && s_best > 0) {
        snprintf(buf, sizeof(buf), "%d  (mejor %d)", s_score, s_best);
    } else {
        snprintf(buf, sizeof(buf), "%d", s_score);
    }
    lv_label_set_text(s_score_lbl, buf);
}

static void hide_exit_overlay(void)
{
    s_exit_armed = false;
    if (s_overlay != NULL) {
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_exit_overlay(void)
{
    s_exit_armed = true;
    s_exit_armed_ms = lv_tick_get();
    if (s_overlay_lbl != NULL) {
        /* ASCII: Montserrat stock no tiene acentos (ver app_menu.c). */
        lv_label_set_text(s_overlay_lbl, "Salir?\nPresiona de nuevo");
    }
    if (s_overlay != NULL) {
        lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void place_bird(void)
{
    const int y = (int)s_bird_y;
    lv_obj_set_pos(s_bird, BIRD_X, y);
    /* Pico y ojo relativos al cuerpo 34x24. */
    lv_obj_set_pos(s_beak, BIRD_X + BIRD_W - 6, y + BIRD_H / 2 - 3);
    lv_obj_set_pos(s_eye, BIRD_X + 20, y + 5);

    int rot = (int)(s_bird_vy * 6.0f);
    if (rot > ROT_MAX) {
        rot = ROT_MAX;
    }
    if (rot < -ROT_MAX) {
        rot = -ROT_MAX;
    }
    lv_obj_set_style_transform_rotation(s_bird, rot * 10, 0); /* 0.1 deg units */
}

static void reset_pipes(void)
{
    for (int i = 0; i < MAX_PIPES; i++) {
        s_pipes[i].x = (float)(SCR_W + 20 + i * PIPE_SPACING);
        s_pipes[i].gap_y = rand_gap_y();
        s_pipes[i].scored = false;
        set_pipe_objs(&s_pipes[i]);
    }
}

static void reset_round(bool playing_hint)
{
    s_state = ST_READY;
    s_bird_y = (float)(SKY_H / 2 - BIRD_H / 2);
    s_bird_vy = 0.0f;
    s_score = 0;
    hide_exit_overlay();
    reset_pipes();
    place_bird();
    update_score_lbl();
    if (s_hint_lbl != NULL) {
        lv_label_set_text(s_hint_lbl,
                          playing_hint ? "Toca o BOOT para volar"
                                       : "Toca o BOOT");
        lv_obj_remove_flag(s_hint_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}

static bool bird_hits_pipe(const pipe_t *p)
{
    const float bx0 = (float)BIRD_X + 2.0f;
    const float bx1 = (float)(BIRD_X + BIRD_W) - 2.0f;
    const float by0 = s_bird_y + 2.0f;
    const float by1 = s_bird_y + (float)BIRD_H - 2.0f;

    const float px0 = p->x;
    const float px1 = p->x + (float)PIPE_W;
    if (bx1 < px0 || bx0 > px1) {
        return false;
    }

    const float gap0 = (float)(p->gap_y - PIPE_GAP / 2);
    const float gap1 = (float)(p->gap_y + PIPE_GAP / 2);
    if (by0 < gap0 || by1 > gap1) {
        return true;
    }
    return false;
}

static void die(void)
{
    if (s_state == ST_DEAD) {
        return;
    }
    s_state = ST_DEAD;
    if (s_score > s_best) {
        s_best = s_score;
    }
    update_score_lbl();
    if (s_hint_lbl != NULL) {
        lv_label_set_text(s_hint_lbl, "Game over — toca para reiniciar");
        lv_obj_remove_flag(s_hint_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    ESP_LOGI(TAG, "game over score=%d best=%d", s_score, s_best);
}

static void do_flap(void)
{
    if (s_exit_armed) {
        hide_exit_overlay();
    }

    if (s_state == ST_DEAD) {
        reset_round(true);
        s_state = ST_PLAY;
        if (s_hint_lbl != NULL) {
            lv_obj_add_flag(s_hint_lbl, LV_OBJ_FLAG_HIDDEN);
        }
        s_bird_vy = FLAP_VY;
        return;
    }

    if (s_state == ST_READY) {
        s_state = ST_PLAY;
        if (s_hint_lbl != NULL) {
            lv_obj_add_flag(s_hint_lbl, LV_OBJ_FLAG_HIDDEN);
        }
    }

    s_bird_vy = FLAP_VY;
}

static void tick(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (s_exit_armed && lv_tick_elaps(s_exit_armed_ms) > EXIT_ARM_MS) {
        hide_exit_overlay();
    }

    if (s_state != ST_PLAY) {
        /* Idle bobbing en READY. */
        if (s_state == ST_READY) {
            s_bird_y = (float)(SKY_H / 2 - BIRD_H / 2) +
                       3.0f * sinf((float)lv_tick_get() / 200.0f);
            place_bird();
        }
        return;
    }

    s_bird_vy += GRAVITY;
    if (s_bird_vy > MAX_VY) {
        s_bird_vy = MAX_VY;
    }
    s_bird_y += s_bird_vy;
    place_bird();

    if (s_bird_y < 0.0f) {
        s_bird_y = 0.0f;
        s_bird_vy = 0.0f;
    }
    if (s_bird_y + (float)BIRD_H >= (float)SKY_H) {
        s_bird_y = (float)(SKY_H - BIRD_H);
        die();
        return;
    }

    for (int i = 0; i < MAX_PIPES; i++) {
        pipe_t *p = &s_pipes[i];
        p->x -= PIPE_VX;
        if (p->x + (float)PIPE_W < -4.0f) {
            float max_x = p->x;
            for (int j = 0; j < MAX_PIPES; j++) {
                if (s_pipes[j].x > max_x) {
                    max_x = s_pipes[j].x;
                }
            }
            p->x = max_x + (float)PIPE_SPACING;
            p->gap_y = rand_gap_y();
            p->scored = false;
        }
        set_pipe_objs(p);

        if (!p->scored && p->x + (float)PIPE_W < (float)BIRD_X) {
            p->scored = true;
            s_score++;
            update_score_lbl();
        }

        if (bird_hits_pipe(p)) {
            die();
            return;
        }
    }
}

static void on_play_pressed(lv_event_t *e)
{
    LV_UNUSED(e);
    do_flap();
}

static void on_exit_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!s_exit_armed) {
        show_exit_overlay();
        return;
    }
    hide_exit_overlay();
    ESP_LOGI(TAG, "salida confirmada (fallback Salir on-screen)");
    shell_close_app();
}

static void flap_async(void *arg)
{
    LV_UNUSED(arg);
    do_flap();
}

/* Corre en la task del event loop: agenda flap en LVGL. */
static void on_boot_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;

    if (bsp_display_lock(0)) {
        lv_async_call(flap_async, NULL);
        bsp_display_unlock();
    }
}

static void build_ui(lv_obj_t *root)
{
    s_root = root;

    /* Cielo */
    s_sky = lv_obj_create(root);
    lv_obj_remove_style_all(s_sky);
    lv_obj_set_size(s_sky, SCR_W, SKY_H);
    lv_obj_set_pos(s_sky, 0, 0);
    lv_obj_set_style_bg_color(s_sky, COL_SKY, 0);
    lv_obj_set_style_bg_opa(s_sky, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_sky, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_sky, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_sky, on_play_pressed, LV_EVENT_PRESSED, NULL);

    /* Tubos detras del pajaro */
    for (int i = 0; i < MAX_PIPES; i++) {
        s_pipes[i].top = make_pipe_body(root);
        s_pipes[i].bot = make_pipe_body(root);
        s_pipes[i].top_cap = make_pipe_cap(root);
        s_pipes[i].bot_cap = make_pipe_cap(root);
    }

    /* Suelo clasico 112 px de alto */
    s_land = lv_obj_create(root);
    lv_obj_remove_style_all(s_land);
    lv_obj_set_size(s_land, SCR_W, LAND_H);
    lv_obj_set_pos(s_land, 0, SKY_H);
    lv_obj_set_style_bg_color(s_land, COL_LAND, 0);
    lv_obj_set_style_bg_opa(s_land, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_land, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_land, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_land, on_play_pressed, LV_EVENT_PRESSED, NULL);

    s_land_stripe = lv_obj_create(root);
    lv_obj_remove_style_all(s_land_stripe);
    lv_obj_set_size(s_land_stripe, SCR_W, 6);
    lv_obj_set_pos(s_land_stripe, 0, SKY_H);
    lv_obj_set_style_bg_color(s_land_stripe, COL_LAND_EDGE, 0);
    lv_obj_set_style_bg_opa(s_land_stripe, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_land_stripe, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* Ave 34x24 + pico/ojo placeholder */
    s_bird = lv_obj_create(root);
    lv_obj_remove_style_all(s_bird);
    lv_obj_set_size(s_bird, BIRD_W, BIRD_H);
    lv_obj_set_style_bg_color(s_bird, COL_BIRD, 0);
    lv_obj_set_style_bg_opa(s_bird, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_bird, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(s_bird, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_transform_pivot_x(s_bird, BIRD_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_bird, BIRD_H / 2, 0);

    s_beak = lv_obj_create(root);
    lv_obj_remove_style_all(s_beak);
    lv_obj_set_size(s_beak, 8, 6);
    lv_obj_set_style_bg_color(s_beak, COL_BIRD_BEAK, 0);
    lv_obj_set_style_bg_opa(s_beak, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_beak, 2, 0);
    lv_obj_remove_flag(s_beak, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_eye = lv_obj_create(root);
    lv_obj_remove_style_all(s_eye);
    lv_obj_set_size(s_eye, 6, 6);
    lv_obj_set_style_bg_color(s_eye, COL_BIRD_EYE, 0);
    lv_obj_set_style_bg_opa(s_eye, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_eye, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(s_eye, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_score_lbl = lv_label_create(root);
    lv_obj_set_style_text_font(s_score_lbl, UI_FONT_DISPLAY, 0);
    lv_obj_set_style_text_color(s_score_lbl, UI_COL_TEXT, 0);
    lv_obj_align(s_score_lbl, LV_ALIGN_TOP_MID, 0, 8);
    lv_label_set_text(s_score_lbl, "0");

    s_hint_lbl = lv_label_create(root);
    lv_obj_set_style_text_font(s_hint_lbl, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(s_hint_lbl, UI_COL_TEXT, 0);
    lv_obj_set_style_text_align(s_hint_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_hint_lbl, LV_ALIGN_CENTER, 0, -20);
    lv_label_set_text(s_hint_lbl, "Toca o BOOT para volar");

    /* Overlay de confirmacion de salida */
    s_overlay = lv_obj_create(root);
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, SCR_W - 40, 72);
    lv_obj_align(s_overlay, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(s_overlay, COL_OVERLAY, 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_80, 0);
    lv_obj_set_style_radius(s_overlay, UI_RADIUS, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    s_overlay_lbl = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_overlay_lbl, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(s_overlay_lbl, UI_COL_TEXT, 0);
    lv_obj_set_style_text_align(s_overlay_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_overlay_lbl, "Salir?\nPresiona de nuevo");
    lv_obj_center(s_overlay_lbl);

    /*
     * Fallback del boton PWR (PEKEY): control on-screen abajo a la derecha.
     * Doble toque = salir. Ver cabecera del archivo para la evidencia.
     */
    s_exit_btn = lv_button_create(root);
    lv_obj_set_size(s_exit_btn, 72, 28);
    lv_obj_align(s_exit_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -10);
    lv_obj_set_style_bg_color(s_exit_btn, UI_COL_SURFACE, 0);
    lv_obj_set_style_bg_opa(s_exit_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_exit_btn, 8, 0);
    lv_obj_set_style_shadow_width(s_exit_btn, 0, 0);
    lv_obj_set_style_border_width(s_exit_btn, 0, 0);
    lv_obj_add_event_cb(s_exit_btn, on_exit_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *exit_lbl = lv_label_create(s_exit_btn);
    lv_label_set_text(exit_lbl, "Salir");
    lv_obj_set_style_text_font(exit_lbl, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(exit_lbl, UI_COL_TEXT, 0);
    lv_obj_center(exit_lbl);
}

static void flappy_open(lv_obj_t *root)
{
    memset(s_pipes, 0, sizeof(s_pipes));
    s_timer = NULL;
    s_boot_handler_on = false;
    s_exit_armed = false;
    s_best = 0;

    /* Fondo del root lo pinta el shell; aca lo cubrimos con el cielo/suelo. */
    build_ui(root);
    reset_round(true);

    shell_claim_boot(true);
    if (esp_event_handler_register(UI_EVENT, UI_EVENT_MENU, on_boot_event,
                                   NULL) == ESP_OK) {
        s_boot_handler_on = true;
    } else {
        ESP_LOGW(TAG, "no se pudo registrar BOOT para flap");
    }

    s_timer = lv_timer_create(tick, TICK_MS, NULL);
    ESP_LOGI(TAG, "open (placeholders bird %dx%d pipe_w %d land_h %d)",
             BIRD_W, BIRD_H, PIPE_W, LAND_H);
}

static void flappy_close(void)
{
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }

    if (s_boot_handler_on) {
        esp_event_handler_unregister(UI_EVENT, UI_EVENT_MENU, on_boot_event);
        s_boot_handler_on = false;
    }
    shell_claim_boot(false);

    s_root = NULL;
    s_sky = NULL;
    s_land = NULL;
    s_land_stripe = NULL;
    s_bird = NULL;
    s_beak = NULL;
    s_eye = NULL;
    s_score_lbl = NULL;
    s_hint_lbl = NULL;
    s_overlay = NULL;
    s_overlay_lbl = NULL;
    s_exit_btn = NULL;
    memset(s_pipes, 0, sizeof(s_pipes));

    ESP_LOGI(TAG, "close (BOOT restaurado al shell)");
}

const os_app_t app_flappy = {
    .id = "flappy",
    .icon = LV_SYMBOL_PLAY,
    .name = "Flappy Bird",
    .open = flappy_open,
    .close = flappy_close,
};
