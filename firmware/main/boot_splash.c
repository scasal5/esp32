#include "boot_splash.h"

#include "bsp/esp-bsp.h"
#include "lvgl.h"

void boot_splash_show(void)
{
    /* bsp_display_start() ya arranco la task de LVGL: todo acceso a la API
       tiene que ir entre lock y unlock. */
    if (!bsp_display_lock(0)) {
        return;
    }

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "ws183-os");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF2F4F8), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -12);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "booting");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x8A93A6), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 16);

    bsp_display_unlock();
}
