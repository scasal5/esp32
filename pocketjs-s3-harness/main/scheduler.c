#include "scheduler.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static esp_timer_handle_t timer;
static TaskHandle_t owner;
static int64_t origin;
static uint64_t next_tick=1;
static void wake(void *arg) {(void)arg;xTaskNotifyGive(owner);}
esp_err_t ws_clock_start(void) {
    owner=xTaskGetCurrentTaskHandle();origin=esp_timer_get_time();next_tick=1;
    esp_timer_create_args_t args={.callback=wake,.name="frame-clock"};
    return esp_timer_create(&args,&timer);
}
int64_t ws_clock_wait(uint32_t *skipped) {
    int64_t now=esp_timer_get_time();
    *skipped=0;
    /* A late frame never causes an unbounded catch-up loop. */
    uint64_t earliest=(uint64_t)(now-origin)*30/1000000+1;
    if (earliest>next_tick) {*skipped=(uint32_t)(earliest-next_tick);next_tick=earliest;}
    int64_t due=ws_deadline(origin,next_tick++);
    int64_t delay=due-esp_timer_get_time();
    if (delay>0) {
        ESP_ERROR_CHECK(esp_timer_start_once(timer,(uint64_t)delay));
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    }
    return due;
}
