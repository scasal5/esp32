#pragma once
#include <stdint.h>
#include "esp_err.h"
esp_err_t ws_clock_start(void);
int64_t ws_clock_wait(uint32_t *skipped);
static inline int64_t ws_deadline(int64_t origin, uint64_t tick) {
    return origin+(int64_t)(tick*1000000ULL/30);
}
