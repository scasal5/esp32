#include "esp_log.h"
#include "esp_err.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"   /* brightness/backlight viven aca, no en esp-bsp.h */

#include "boot_splash.h"

static const char *TAG = "ws183";

void app_main(void)
{
    /* 1. I2C primero. De este bus cuelgan el AXP2101, el tactil CST816S,
          la IMU QMI8658 y el RTC de la placa. */
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_LOGI(TAG, "i2c up");

    /* 2. Backlight apagado ANTES de encender el panel. Si se enciende la luz
          primero, el usuario ve el framebuffer sin inicializar.
          Sin ESP_ERROR_CHECK: bsp_display_start() vuelve a inicializar el
          LEDC por su cuenta, y no queremos abortar por una doble init. */
    esp_err_t err = bsp_display_brightness_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "brightness_init: %s", esp_err_to_name(err));
    }
    bsp_display_backlight_off();

    /* 3. Panel + LVGL. */
    lv_display_t *disp = bsp_display_start();
    if (disp == NULL) {
        ESP_LOGE(TAG, "bsp_display_start() devolvio NULL");
        return;
    }
    ESP_LOGI(TAG, "panel up: %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);

    /* 4. Splash, todavia a oscuras. */
    boot_splash_show();

    /* 5. Recien ahora la luz: el primer frame visible ya es el splash. */
    ESP_ERROR_CHECK(bsp_display_brightness_set(80));
    ESP_LOGI(TAG, "splash visible");

    /* 6. Servicios (pm, audio, imu, wifi) van aca, despues del splash.
          Nada de esto debe bloquear el primer frame. */
}
