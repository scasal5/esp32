#include "pm_format.h"

#include <stdio.h>

void pm_format_label(const pm_status_t *st, char *buf, size_t n)
{
    if (buf == NULL || n == 0) {
        return;
    }

    if (st == NULL) {
        snprintf(buf, n, "sin PMU");
        return;
    }
    if (!st->present) {
        snprintf(buf, n, "sin bateria");
        return;
    }

    const char *usb = st->charging ? " USB" : "";
    if (st->percent < 0) {
        /* El PMU no pudo estimar el porcentaje: la tension medida siempre es
           un dato real. */
        snprintf(buf, n, "%u mV%s", (unsigned)st->batt_mv, usb);
    } else {
        snprintf(buf, n, "%d%%%s", st->percent, usb);
    }
}
