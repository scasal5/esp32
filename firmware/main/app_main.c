#include "esp_log.h"
#include "esp_err.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"   /* brightness/backlight viven aca, no en esp-bsp.h */

#include "boot_splash.h"
#include "pm.h"

static const char *TAG = "ws183";

void app_main(void)
{
    /* 1. I2C primero. De este bus cuelgan el AXP2101, el tactil CST816S,
          la IMU QMI8658 y el RTC de la placa. */
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_LOGI(TAG, "i2c up");

    /* 1b. PMU, en cuanto hay bus. Solo lectura: no toca rieles.
           Si falla no se aborta, porque el splash no depende del PMU. */
    esp_err_t pm_err = pm_init();
    if (pm_err != ESP_OK) {
        ESP_LOGW(TAG, "pm_init: %s", esp_err_to_name(pm_err));
    }

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

    /* 6. Telemetria del PMU, ya con la pantalla encendida para no demorar el
          primer frame. */
    pm_log_status();

    /* 7. El resto de los servicios (audio, imu, wifi) va aca.
          Nada de esto debe bloquear el primer frame. */
}
