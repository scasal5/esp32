#pragma once

#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RTC PCF85063A de la placa (I2C 0x51, mismo bus que el AXP2101).
 *
 * Guarda la hora en UTC. La zona horaria se aplica en el sistema (TZ), no en
 * el chip, asi un cambio de zona no obliga a reescribir el RTC.
 */

/* Registra el dispositivo en el bus que ya abrio bsp_i2c_init().
   ESP_ERR_NOT_FOUND si el chip no contesta. */
esp_err_t board_rtc_init(void);

/* Lee la hora del chip. ESP_ERR_INVALID_RESPONSE si el oscilador se detuvo
   (bit OS): la hora no es confiable hasta el proximo board_rtc_set(). */
esp_err_t board_rtc_get(time_t *utc);

/* Escribe la hora en el chip y limpia el bit OS. Rango del chip: 2000..2099. */
esp_err_t board_rtc_set(time_t utc);

/* Copia la hora del chip al reloj del sistema (settimeofday). */
esp_err_t board_rtc_to_system(void);

#ifdef __cplusplus
}
#endif
