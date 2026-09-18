#pragma once
#include "pocketjs/guest.h"
#include "pocketjs/package.h"
#include "battery.h"
esp_err_t ws_guest_create(pocketjs_guest_t **guest);
void ws_guest_guard_begin(pocketjs_guest_t *guest,int64_t budget_us);
bool ws_guest_guard_end(void);
/* Map (eval_error, timeout) to a control-flow error. Never overwrite eval_error:
 * timeout + any eval result -> ESP_ERR_TIMEOUT; otherwise return eval_error. */
esp_err_t ws_guest_derived(esp_err_t eval_error, bool timeout);
const char *ws_guest_cause(esp_err_t eval_error, bool timeout);
void ws_guest_set_battery(ws_battery_t value);
esp_err_t ws_package_load(void **bytes,pocketjs_package_t **package,pocketjs_package_variant_t *variant);
esp_err_t ws_headless_tests(void);
