/*
 * Boton BOOT (el de arriba): GPIO0, pull-up, activo en bajo.
 *
 * GPIO0 es strap de download solo durante el reset. Con el firmware andando es
 * un boton comun. Aca se configura como entrada y nunca se maneja: el pin no
 * queda forzado a 0 en un reset.
 */

#include "menu_button.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

ESP_EVENT_DEFINE_BASE(UI_EVENT);

static const char *TAG = "menu_button";

#define BTN_MENU     GPIO_NUM_0   /* BOOT. PWR no tiene GPIO. */
#define DEBOUNCE_MS  40

/* La task pasa casi todo el tiempo bloqueada. Prioridad por encima de la de
   LVGL (4) para que el click no espere a que termine un render. */
#define BTN_TASK_STACK  3072
#define BTN_TASK_PRIO   5

static TaskHandle_t s_task = NULL;

/* Flanco a bajo: se apaga la interrupcion para que los rebotes no despierten
   otra vez a la task, y se la avisa. */
static void btn_isr(void *arg)
{
    (void)arg;

    gpio_intr_disable(BTN_MENU);
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &woken);
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void btn_task(void *arg)
{
    (void)arg;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Si sigue en bajo pasado el debounce, es un click real. */
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
        if (gpio_get_level(BTN_MENU) == 0) {
            esp_err_t err = esp_event_post(UI_EVENT, UI_EVENT_MENU, NULL, 0, 0);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "UI_EVENT_MENU no publicado: %s", esp_err_to_name(err));
            }

            /* Un click por pulsacion: esperar a que se suelte, con su propio
               debounce, antes de volver a escuchar flancos. */
            while (gpio_get_level(BTN_MENU) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
        }

        gpio_intr_enable(BTN_MENU);
    }
}

esp_err_t menu_button_start(void)
{
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << BTN_MENU,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    /* La task existe antes que el handler: la ISR siempre tiene a quien avisar. */
    if (xTaskCreate(btn_task, "menu_button", BTN_TASK_STACK, NULL, BTN_TASK_PRIO,
                    &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    /* El servicio de ISR de GPIO ya lo instala esp_lcd_touch al registrar la
       interrupcion del tactil, dentro de bsp_display_start(). Llamar a
       gpio_install_isr_service() otra vez funciona, pero ESP-IDF lo loguea como
       error. Solo se instala si todavia no existe (por ejemplo, un tactil sin
       pin de interrupcion). */
    err = gpio_isr_handler_add(BTN_MENU, btn_isr, NULL);
    if (err == ESP_ERR_INVALID_STATE) {
        err = gpio_install_isr_service(0);
        if (err == ESP_OK) {
            err = gpio_isr_handler_add(BTN_MENU, btn_isr, NULL);
        }
    }
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "BOOT (GPIO%d) listo como boton de menu", (int)BTN_MENU);
    return ESP_OK;
}
