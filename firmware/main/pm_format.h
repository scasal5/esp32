#pragma once

#include <stddef.h>

#include "pm.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Texto corto del estado de bateria para la UI:
 *
 *   st == NULL                     "sin PMU"
 *   present == false               "sin bateria"
 *   percent < 0                    "3870 mV" / "3870 mV USB"
 *   percent >= 0                   "100%"    / "98% USB"
 *
 * Sin I2C ni LVGL: se puede probar en el host.
 */
void pm_format_label(const pm_status_t *st, char *buf, size_t n);

#ifdef __cplusplus
}
#endif
