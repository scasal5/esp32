/*
 * Flappy Bird — clon jugable para ws183-os (pantalla 240x284).
 *
 * Mecanica clasica: gravedad, flap, tubos verdes, suelo, puntaje, game over.
 *
 * Entrada:
 *   - Toque (CST816S) o BOOT/GPIO0 = flap / empezar / reiniciar.
 *   - Mientras la app esta abierta, BOOT no abre el menu del shell
 *     (shell_claim_boot). Al cerrar se restaura.
 *
 * Salida (boton fisico ABAJO = PWR):
 *   Short-press NO apaga (hard power-off solo con hold ~4-10s del PEKEY).
 *   Deteccion via GPIO41 (SYS_OUT del PWR en Waveshare 1.83), debounce.
 *   1er short-press -> overlay "Salir?\nPresiona de nuevo"
 *   2o en ~2.5s -> shell_close_app()
 *   Sin boton on-screen "Salir". No se escriben rieles del AXP2101.
 *
 * Graficos: Yorokobi flappy_atlas.png (CC0) upscale NN — pajaro / tubos /
 *   suelo. Cielo #4EC0CA procedural. Ver assets/flappy/NOTICE.
 *   SFX: Kenney Digital Audio (CC0) via flappy_sfx_play() stub.
 */

#include "app_flappy.h"

#include "flappy_img.h"
#include "flappy_sfx.h"
#include "menu_button.h"
#include "shell.h"
#include "ui_theme.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"

#include "lvgl.h"

static const char *TAG = "flappy";

/* --- Medidas (Kenney sprites escalados; gap clasico) -------------------- */
#define SCR_W          240
#define SCR_H          284

#define BIRD_W         FLAPPY_BIRD_W
#define BIRD_H         FLAPPY_BIRD_H

#define PIPE_W         FLAPPY_PIPE_W
#define PIPE_H         FLAPPY_PIPE_H
#define PIPE_GAP       96

#define LAND_H         FLAPPY_GROUND_H
#define SKY_H          (SCR_H - LAND_H)

#define BIRD_X         56
#define MAX_PIPES      3
#define PIPE_SPACING   144

#define TICK_MS        33
#define GRAVITY        0.35f
#define FLAP_VY        (-5.8f)
#define PIPE_VX        2.5f
#define MAX_VY         8.0f
#define ROT_MAX        45

#define GROUND_TILES   ((SCR_W / FLAPPY_GROUND_TILE_W) + 2)

/* Cielo solido muestreado de Kenney background.png (no RGBA fullscreen). */
#define COL_SKY        lv_color_hex(0x4EC0CA) /* classic Flappy cyan */
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
    lv_obj_t *bot;
} pipe_t;

static lv_obj_t *s_root;
static lv_obj_t *s_sky;
static lv_obj_t *s_land_tiles[GROUND_TILES];
static float s_land_scroll;
static lv_obj_t *s_bird;
static int s_bird_frame;
static uint32_t s_bird_frame_ms;
static lv_obj_t *s_score_lbl;
static lv_obj_t *s_hint_lbl;
static lv_obj_t *s_overlay;
static lv_obj_t *s_overlay_lbl;
static lv_timer_t *s_timer;

static pipe_t s_pipes[MAX_PIPES];
static game_state_t s_state;
static float s_bird_y;
static float s_bird_vy;
static int s_score;
static int s_best;
static bool s_exit_armed;
static uint32_t s_exit_armed_ms;
static int s_pwr_stable;          /* last debounced level (1=released) */
static int s_pwr_raw;             /* last raw sample */
static uint32_t s_pwr_edge_ms;    /* when raw last changed */
static bool s_pwr_inited;

static bool s_boot_handler_on;

#define EXIT_ARM_MS 2500

/* Waveshare ESP32-S3-Touch-LCD-1.83: SYS_OUT del boton PWR -> GPIO41.
 * Active-low con pull-up (igual que BOOT). Short-press = flanco a bajo.
 * Hold largo sigue yendo al PEKEY del AXP2101 (hard power-off). */
#define PWR_GPIO           GPIO_NUM_41
#define PWR_DEBOUNCE_MS    40


static const lv_image_dsc_t *const s_bird_frames[3] = {
    &flappy_bird_1,
    &flappy_bird_2,
    &flappy_bird_3,
};

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

    /* Roca superior: punta en gap_top (imagen cuelga hacia arriba). */
    lv_obj_set_pos(p->top, x, gap_top - PIPE_H);
    /* Roca inferior: punta en gap_bot. */
    lv_obj_set_pos(p->bot, x, gap_bot);
}

static lv_obj_t *make_pipe_img(lv_obj_t *parent, const lv_image_dsc_t *src)
{
    lv_obj_t *o = lv_image_create(parent);
    lv_image_set_src(o, src);
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

static void set_bird_frame(int frame)
{
    if (frame < 0) {
        frame = 0;
    }
    if (frame > 2) {
        frame = 2;
    }
    s_bird_frame = frame;
    if (s_bird != NULL) {
        lv_image_set_src(s_bird, s_bird_frames[s_bird_frame]);
    }
}

static void place_bird(void)
{
    const int y = (int)s_bird_y;
    lv_obj_set_pos(s_bird, BIRD_X, y);

    int rot = (int)(s_bird_vy * 6.0f);
    if (rot > ROT_MAX) {
        rot = ROT_MAX;
    }
    if (rot < -ROT_MAX) {
        rot = -ROT_MAX;
    }
    /* LVGL image rotation is 0.1 deg units. */
    lv_image_set_rotation(s_bird, rot * 10);
}

static void place_ground(void)
{
    const int base = (int)s_land_scroll;
    for (int i = 0; i < GROUND_TILES; i++) {
        int x = i * FLAPPY_GROUND_TILE_W - (base % FLAPPY_GROUND_TILE_W);
        lv_obj_set_pos(s_land_tiles[i], x, SKY_H);
    }
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
    s_land_scroll = 0.0f;
    s_bird_frame_ms = lv_tick_get();
    hide_exit_overlay();
    reset_pipes();
    set_bird_frame(0);
    place_bird();
    place_ground();
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
    flappy_sfx_play(FLAPPY_SFX_DIE);
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
        set_bird_frame(1);
        flappy_sfx_play(FLAPPY_SFX_FLAP);
        return;
    }

    if (s_state == ST_READY) {
        s_state = ST_PLAY;
        if (s_hint_lbl != NULL) {
            lv_obj_add_flag(s_hint_lbl, LV_OBJ_FLAG_HIDDEN);
        }
    }

    s_bird_vy = FLAP_VY;
    set_bird_frame(1);
    s_bird_frame_ms = lv_tick_get();
    flappy_sfx_play(FLAPPY_SFX_FLAP);
}


static void pwr_init(void)
{
    if (s_pwr_inited) {
        return;
    }
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << PWR_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GPIO41 PWR init fallo: %s", esp_err_to_name(err));
        return;
    }
    s_pwr_raw = gpio_get_level(PWR_GPIO);
    s_pwr_stable = s_pwr_raw;
    s_pwr_edge_ms = lv_tick_get();
    s_pwr_inited = true;
    ESP_LOGI(TAG, "PWR short-press watch on GPIO%d (level=%d)", (int)PWR_GPIO, s_pwr_stable);
}

static void pwr_deinit(void)
{
    /* Dejar el pin como entrada pull-up; no hace falta resetear. */
    s_pwr_inited = false;
}

/* true en flanco de short-press (released->pressed, active-low). */
static bool pwr_poll_short_press(void)
{
    if (!s_pwr_inited) {
        return false;
    }
    const int raw = gpio_get_level(PWR_GPIO);
    const uint32_t now = lv_tick_get();
    if (raw != s_pwr_raw) {
        s_pwr_raw = raw;
        s_pwr_edge_ms = now;
        return false;
    }
    if ((int)(now - s_pwr_edge_ms) < PWR_DEBOUNCE_MS) {
        return false;
    }
    if (raw == s_pwr_stable) {
        return false;
    }
    const int prev = s_pwr_stable;
    s_pwr_stable = raw;
    /* active-low: press = 1 -> 0 */
    return (prev == 1 && raw == 0);
}

static void handle_pwr_exit(void)
{
    if (!pwr_poll_short_press()) {
        return;
    }
    if (!s_exit_armed) {
        show_exit_overlay();
        ESP_LOGI(TAG, "salida armada (PWR GPIO41 x1)");
        return;
    }
    hide_exit_overlay();
    ESP_LOGI(TAG, "salida confirmada (PWR GPIO41 x2)");
    shell_close_app();
}

static void tick(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    handle_pwr_exit();

    if (s_exit_armed && lv_tick_elaps(s_exit_armed_ms) > EXIT_ARM_MS) {
        hide_exit_overlay();
    }

    if (s_state != ST_PLAY) {
        /* Idle bobbing + aleteo en READY. */
        if (s_state == ST_READY) {
            s_bird_y = (float)(SKY_H / 2 - BIRD_H / 2) +
                       3.0f * sinf((float)lv_tick_get() / 200.0f);
            if (lv_tick_elaps(s_bird_frame_ms) > 120) {
                s_bird_frame_ms = lv_tick_get();
                set_bird_frame((s_bird_frame + 1) % 3);
            }
            place_bird();
        }
        return;
    }

    if (lv_tick_elaps(s_bird_frame_ms) > 80) {
        s_bird_frame_ms = lv_tick_get();
        set_bird_frame((s_bird_frame + 1) % 3);
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

    s_land_scroll += PIPE_VX;
    place_ground();

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
            flappy_sfx_play(FLAPPY_SFX_SCORE);
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

    /* Tubos (rocas Kenney) detras del pajaro */
    for (int i = 0; i < MAX_PIPES; i++) {
        s_pipes[i].top = make_pipe_img(root, &flappy_pipe_top);
        s_pipes[i].bot = make_pipe_img(root, &flappy_pipe_bot);
    }

    /* Suelo mosaico Kenney (tileable) */
    for (int i = 0; i < GROUND_TILES; i++) {
        s_land_tiles[i] = lv_image_create(root);
        lv_image_set_src(s_land_tiles[i], &flappy_ground);
        lv_obj_add_flag(s_land_tiles[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(s_land_tiles[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_land_tiles[i], on_play_pressed, LV_EVENT_PRESSED,
                            NULL);
    }

    /* Ave: 3 frames planeYellow */
    s_bird = lv_image_create(root);
    lv_image_set_src(s_bird, &flappy_bird_1);
    lv_obj_remove_flag(s_bird, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_pivot(s_bird, BIRD_W / 2, BIRD_H / 2);

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

}

static void flappy_open(lv_obj_t *root)
{
    memset(s_pipes, 0, sizeof(s_pipes));
    memset(s_land_tiles, 0, sizeof(s_land_tiles));
    s_timer = NULL;
    s_boot_handler_on = false;
    s_exit_armed = false;
    s_best = 0;
    s_land_scroll = 0.0f;
    s_bird_frame = 0;

    /* Fondo del root lo pinta el shell; aca lo cubrimos con el cielo/suelo. */
    build_ui(root);
    reset_round(true);

    pwr_init();
    shell_claim_boot(true);
    if (esp_event_handler_register(UI_EVENT, UI_EVENT_MENU, on_boot_event,
                                   NULL) == ESP_OK) {
        s_boot_handler_on = true;
    } else {
        ESP_LOGW(TAG, "no se pudo registrar BOOT para flap");
    }

    s_timer = lv_timer_create(tick, TICK_MS, NULL);
    ESP_LOGI(TAG, "open (Yorokobi CC0 bird %dx%d pipe_w %d land_h %d)",
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
    pwr_deinit();
    shell_claim_boot(false);

    s_root = NULL;
    s_sky = NULL;
    memset(s_land_tiles, 0, sizeof(s_land_tiles));
    s_bird = NULL;
    s_score_lbl = NULL;
    s_hint_lbl = NULL;
    s_overlay = NULL;
    s_overlay_lbl = NULL;
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
