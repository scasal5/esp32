/*
 * RTC PCF85063A (NXP), identificado en esta placa por escaneo I2C: responde en
 * 0x51, su mapa de registros se repite cada 18 bytes (0x00..0x11) y Timer_mode
 * (0x11) arranca en 0x18, el valor de reset del PCF85063A.
 *
 * Solo toca los registros de hora (0x04..0x0A) y el bit 12_24 de Control_1.
 * No usa alarmas, timer ni CLKOUT.
 */

#include "board_rtc.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "bsp/esp-bsp.h"

static const char *TAG = "rtc";

#define PCF85063_ADDR       0x51
#define PCF85063_TIMEOUT_MS 100

#define REG_CONTROL_1       0x00
#define CONTROL_1_12_24     0x02    /* 1 = modo 12 h; se fuerza 24 h */
#define REG_SECONDS         0x04    /* 0x04..0x0A: seg, min, hora, dia, dia sem, mes, anio */
#define SECONDS_OS          0x80    /* oscilador detenido: hora no confiable */

static i2c_master_dev_handle_t s_dev = NULL;

static uint8_t bcd_to_bin(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

static uint8_t bin_to_bcd(uint8_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

/* Dias desde 1970-01-01 de una fecha gregoriana. Evita mktime(), que aplica
   la TZ del sistema, y timegm(), que newlib no garantiza. */
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static esp_err_t read_reg(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, PCF85063_TIMEOUT_MS);
}

esp_err_t board_rtc_init(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (i2c_master_probe(bus, PCF85063_ADDR, PCF85063_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "el PCF85063A no responde en 0x%02X", PCF85063_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    const i2c_device_config_t cfg = {
        .device_address = PCF85063_ADDR,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        s_dev = NULL;
        return err;
    }

    ESP_LOGI(TAG, "PCF85063A en 0x%02X", PCF85063_ADDR);
    return ESP_OK;
}

esp_err_t board_rtc_get(time_t *utc)
{
    if (utc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t b[7];
    esp_err_t err = read_reg(REG_SECONDS, b, sizeof(b));
    if (err != ESP_OK) {
        return err;
    }
    if (b[0] & SECONDS_OS) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    unsigned sec = bcd_to_bin(b[0] & 0x7F);
    unsigned min = bcd_to_bin(b[1] & 0x7F);
    unsigned hour = bcd_to_bin(b[2] & 0x3F);
    unsigned day = bcd_to_bin(b[3] & 0x3F);
    unsigned month = bcd_to_bin(b[5] & 0x1F);
    int year = 2000 + bcd_to_bin(b[6]);

    if (sec > 59 || min > 59 || hour > 23 || day < 1 || day > 31 || month < 1 || month > 12) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *utc = (time_t)(days_from_civil(year, month, day) * 86400 + hour * 3600 + min * 60 + sec);
    return ESP_OK;
}

esp_err_t board_rtc_set(time_t utc)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    struct tm tm;
    if (gmtime_r(&utc, &tm) == NULL || tm.tm_year < 100 || tm.tm_year > 199) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Modo 24 h: si el chip quedo en 12 h, el registro de horas usa otro formato. */
    uint8_t ctrl;
    esp_err_t err = read_reg(REG_CONTROL_1, &ctrl, 1);
    if (err != ESP_OK) {
        return err;
    }
    if (ctrl & CONTROL_1_12_24) {
        const uint8_t w[2] = { REG_CONTROL_1, (uint8_t)(ctrl & ~CONTROL_1_12_24) };
        err = i2c_master_transmit(s_dev, w, sizeof(w), PCF85063_TIMEOUT_MS);
        if (err != ESP_OK) {
            return err;
        }
    }

    /* Una sola transaccion desde 0x04: escribir los segundos limpia el bit OS. */
    const uint8_t w[8] = {
        REG_SECONDS,
        bin_to_bcd((uint8_t)tm.tm_sec),
        bin_to_bcd((uint8_t)tm.tm_min),
        bin_to_bcd((uint8_t)tm.tm_hour),
        bin_to_bcd((uint8_t)tm.tm_mday),
        (uint8_t)tm.tm_wday,
        bin_to_bcd((uint8_t)(tm.tm_mon + 1)),
        bin_to_bcd((uint8_t)(tm.tm_year - 100)),
    };
    return i2c_master_transmit(s_dev, w, sizeof(w), PCF85063_TIMEOUT_MS);
}

esp_err_t board_rtc_to_system(void)
{
    time_t utc;
    esp_err_t err = board_rtc_get(&utc);
    if (err != ESP_OK) {
        return err;
    }

    const struct timeval tv = { .tv_sec = utc, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) != 0) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "hora del sistema cargada desde el RTC (epoch %lld)", (long long)utc);
    return ESP_OK;
}
