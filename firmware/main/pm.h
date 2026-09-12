#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Lectura del AXP2101. Todos los campos vienen del PMU; ninguno se escribe. */
typedef struct {
    bool     present;   /* hay bateria conectada */
    bool     charging;  /* el PMU esta cargando */
    bool     vbus;      /* hay USB en VBUS */
    int      percent;   /* 0..100, o -1 si el PMU no puede estimarlo */
    uint16_t batt_mv;   /* tension de bateria */
    uint16_t vbus_mv;   /* tension de VBUS (USB) */
    uint16_t sys_mv;    /* tension de sistema */
} pm_status_t;

/*
 * Inicializa el AXP2101 sobre el bus I2C que ya abrio bsp_i2c_init().
 *
 * SOLO LECTURA: habilita los ADC de medicion y nada mas. No configura, no
 * enciende y no apaga ningun riel (DCDC / ALDO / BLDO). La placa arranca con
 * los rieles ya configurados y tocarlos a ciegas apaga el panel, el codec,
 * la PSRAM o el propio USB.
 *
 * Devuelve ESP_ERR_NOT_FOUND si el PMU no contesta. No es fatal: hoy el
 * splash no depende del PMU.
 */
esp_err_t pm_init(void);

/* Lee el estado actual. ESP_ERR_INVALID_STATE si pm_init() no tuvo exito. */
esp_err_t pm_read(pm_status_t *out);

/* Loguea una linea con el estado actual. No falla. */
void pm_log_status(void);

#ifdef __cplusplus
}
#endif
