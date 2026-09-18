#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"   /* brightness/backlight viven aca, no en esp-bsp.h */

#include <stdlib.h>
#include <time.h>

#include "app_ajustes.h"
#include "app_flappy.h"
#include "board_rtc.h"
#include "boot_splash.h"
#include "display.h"
#include "fondo_ui.h"
#include "menu_button.h"
#include "pm.h"
#include "shell.h"
#include "splash_gif.h"
#include "svc_wifi.h"
#include "time_console.h"
#include "wifi_scan_ui.h"

static const char *TAG = "ws183";

/*
 * Tarjetas que todavia no son una app. Sin open() el shell no las abre: quedan
 * en el lanzador avisando que vienen. Cada una es una fase de la hoja de ruta.
 */
#if CONFIG_WS183_APP_ASPECTO
static const os_app_t app_aspecto = {
    .id = "aspecto", .icon = LV_SYMBOL_EYE_OPEN, .name = "Aspecto",
};
#endif

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

    /* 1c. Hora: el RTC PCF85063A guarda UTC y la zona es UTC-3 fija
           (Argentina no tiene horario de verano). Si el RTC nunca se seteo
           (bit OS) o no responde, la hora queda sin cargar hasta un settime.
           No es fatal: nada del arranque depende de la hora. */
    setenv("TZ", "<-03>3", 1);
    tzset();
    esp_err_t rtc_err = board_rtc_init();
    if (rtc_err == ESP_OK) {
        rtc_err = board_rtc_to_system();
    }
    if (rtc_err != ESP_OK) {
        ESP_LOGW(TAG, "rtc: %s (usar settime o scripts/set_time.ps1)", esp_err_to_name(rtc_err));
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

    /* 3. Panel + LVGL. No es bsp_display_start(): ese arma un solo buffer sin
          DMA. Ver main/display.c. */
    lv_display_t *disp = display_start();
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

    /* 7. Imagen de inicio (fase 1a), en su propia task y con la luz ya
          encendida: si no hay GIF usable, queda el splash de texto. */
    splash_gif_start();

    /* 7b. Consola por USB-Serial-JTAG: settime / time. Va al final, con la
           pantalla ya encendida, para que su prompt no se mezcle con el
           arranque. */
    time_console_start();

    /* 7c. Shell: BOOT publica UI_EVENT_MENU y el shell decide si abre el
           lanzador, lo cierra o cierra la app. Las apps se registran aca, a
           mano y detras de su opcion de Kconfig: quien lee este archivo ve
           todo lo que arranca. Si algo falla, la placa se queda sin menu pero
           sigue mostrando la pantalla de inicio. */
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "event loop: %s", esp_err_to_name(err));
    } else {
        err = shell_init();
        if (err == ESP_OK) {
#if CONFIG_WS183_APP_FONDO
            esp_err_t fondo_err = fondo_ui_init();
            if (fondo_err != ESP_OK) {
                ESP_LOGW(TAG, "fondo ui: %s", esp_err_to_name(fondo_err));
            } else {
                shell_register_app(&app_fondo);
            }
#endif
#if CONFIG_WS183_APP_ASPECTO
            shell_register_app(&app_aspecto);
#endif
#if CONFIG_WS183_APP_WIFI
            esp_err_t wifi_ui_err = wifi_scan_ui_init();
            if (wifi_ui_err != ESP_OK) {
                ESP_LOGW(TAG, "wifi ui: %s", esp_err_to_name(wifi_ui_err));
            } else {
                shell_register_app(&app_wifi);
            }
#endif
#if CONFIG_WS183_APP_FLAPPY
            shell_register_app(&app_flappy);
#endif
#if CONFIG_WS183_APP_AJUSTES
            shell_register_app(&app_ajustes);
#endif
            err = menu_button_start();
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "shell: %s", esp_err_to_name(err));
        }
    }

    /* 8. WiFi despues del primer frame: inicia STA. Si hay credenciales en
          NVS, intenta reconectar. El scan y el SoftAP se disparan desde la UI. */
    esp_err_t wifi_err = svc_wifi_start();
    if (wifi_err != ESP_OK) {
        ESP_LOGW(TAG, "svc_wifi_start: %s", esp_err_to_name(wifi_err));
    } else {
        svc_wifi_register_console();
    }
}
