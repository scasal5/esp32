#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    bool available, present, charging, vbus;
    int percent, millivolts;
    int64_t sampled_us;
} ws_battery_t;
void ws_battery_start(void);
ws_battery_t ws_battery_snapshot(void);
#ifdef __cplusplus
}
#endif
