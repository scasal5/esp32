#include <stdio.h>
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void) {
    const esp_partition_t *p = esp_ota_get_running_partition();
    for (;;) {
        printf("WS183_GATE0_PASS idf=%s partition=%s offset=0x%lx uptime_us=%lld\n",
               esp_app_get_description()->idf_ver, p->label, (unsigned long)p->address,
               (long long)esp_timer_get_time());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
