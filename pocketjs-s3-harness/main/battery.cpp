#include "battery.h"
#include "board_ws183.h"
#include "XPowersLib.h"
#include "esp_timer.h"
#include "freertos/task.h"
static XPowersAXP2101 pmu;
static ws_battery_t latest = {false,false,false,false,-1,-1,0};
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static void sample(void *) {
    xSemaphoreTake(ws_i2c_mutex,portMAX_DELAY);
    bool ready=pmu.begin(ws_i2c,AXP2101_SLAVE_ADDRESS);
    if (ready) {
        pmu.enableBattDetection(); pmu.enableBattVoltageMeasure();
        pmu.enableVbusVoltageMeasure(); pmu.enableSystemVoltageMeasure();
    }
    xSemaphoreGive(ws_i2c_mutex);
    for (;;) {
        ws_battery_t next={false,false,false,false,-1,-1,0};
        xSemaphoreTake(ws_i2c_mutex,portMAX_DELAY);
        next.available=ready && i2c_master_probe(ws_i2c,AXP2101_SLAVE_ADDRESS,50)==ESP_OK;
        if (next.available) {
            next.present=pmu.isBatteryConnect(); next.charging=pmu.isCharging();
            next.vbus=pmu.isVbusIn();
            if (next.present) {next.percent=pmu.getBatteryPercent();next.millivolts=pmu.getBattVoltage();}
        }
        xSemaphoreGive(ws_i2c_mutex);
        next.sampled_us=esp_timer_get_time();
        portENTER_CRITICAL(&lock); latest=next; portEXIT_CRITICAL(&lock);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
extern "C" void ws_battery_start(void) {
    configASSERT(xTaskCreate(sample,"battery",4096,nullptr,3,nullptr)==pdPASS);
}
extern "C" ws_battery_t ws_battery_snapshot(void) {
    portENTER_CRITICAL(&lock); ws_battery_t value=latest; portEXIT_CRITICAL(&lock);
    return value;
}
