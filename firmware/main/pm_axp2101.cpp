/*
 * Servicio de energia: AXP2101 en modo SOLO LECTURA.
 *
 * XPowersLib es C++ con templates, asi que el C++ queda encerrado en este
 * unico archivo y el resto del firmware consume la API en C de pm.h.
 *
 * Deliberadamente NO se llama a ningun setDC*Voltage / setALDO*Voltage /
 * enable* de riel / disable* / shutdown / sleep. Lo unico que se habilita son
 * los ADC de medicion, que son bits de conversion, no salidas de potencia.
 */

#include "pm.h"

#include "XPowersLib.h"

#include "bsp/esp-bsp.h"
#include "esp_log.h"

static const char *TAG = "pm";

static XPowersAXP2101 s_pmu;
static bool s_ready = false;

extern "C" esp_err_t pm_init(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        ESP_LOGW(TAG, "el bus I2C no esta inicializado; llama a bsp_i2c_init() antes");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_pmu.begin(bus, AXP2101_SLAVE_ADDRESS)) {
        ESP_LOGW(TAG, "el AXP2101 no responde en 0x%02X", AXP2101_SLAVE_ADDRESS);
        return ESP_ERR_NOT_FOUND;
    }

    /* Bits de ADC, no rieles. Sin esto getBatteryPercent() y getBattVoltage()
       devuelven valores sin sentido. */
    s_pmu.enableBattDetection();
    s_pmu.enableBattVoltageMeasure();
    s_pmu.enableVbusVoltageMeasure();
    s_pmu.enableSystemVoltageMeasure();

    s_ready = true;
    ESP_LOGI(TAG, "AXP2101 inicializado (solo lectura, sin tocar rieles)");
    return ESP_OK;
}

extern "C" esp_err_t pm_read(pm_status_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    out->present  = s_pmu.isBatteryConnect();
    out->charging = s_pmu.isCharging();
    out->percent  = s_pmu.getBatteryPercent();
    out->batt_mv  = s_pmu.getBattVoltage();
    out->vbus_mv  = s_pmu.getVbusVoltage();
    out->sys_mv   = s_pmu.getSystemVoltage();

    return ESP_OK;
}

extern "C" void pm_log_status(void)
{
    pm_status_t s;
    esp_err_t err = pm_read(&s);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "sin lectura: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG,
             "present=%d charging=%d percent=%d batt_mv=%u vbus_mv=%u sys_mv=%u",
             (int)s.present, (int)s.charging, s.percent,
             (unsigned)s.batt_mv, (unsigned)s.vbus_mv, (unsigned)s.sys_mv);
}
