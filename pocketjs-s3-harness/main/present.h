#pragma once
#include <stddef.h>
#include "board_ws183.h"
#include "metrics.h"
typedef struct {unsigned x,y,w,h;} ws_rect_t;
bool ws_pack(uint16_t *dst,size_t capacity,const uint16_t *fb,ws_rect_t r);
esp_err_t ws_present_init(void);
esp_err_t ws_present(const uint16_t *fb,ws_rect_t r,ws_frame_metrics_t *m);
void ws_present_inject_failure(void);
