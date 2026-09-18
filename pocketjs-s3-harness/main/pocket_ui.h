#pragma once
#include "present.h"
esp_err_t ws_ui_start(void);
esp_err_t ws_ui_frame(ws_input_t input,unsigned scenario,ws_frame_metrics_t *metrics);
size_t ws_ui_heap(void);
void ws_ui_stop(void);
bool ws_ui_fault_test(const char *kind);
