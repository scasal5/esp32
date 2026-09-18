#pragma once
/* Frozen from firmware/display.c and Waveshare BSP 2.0.0 (MIT).
 * Board port, not a binary-compatible reuse of the IDF 5.5 BSP. */
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#define WS_WIDTH 240
#define WS_HEIGHT 284
#define WS_PIXELS (WS_WIDTH * WS_HEIGHT)
#define WS_FRAME_BYTES (WS_PIXELS * 2)
#define WS_SPI_HZ 24000000
#define WS_STRIP_ROWS 40
#define WS_BOUNCE_BYTES (WS_WIDTH * WS_STRIP_ROWS * 2)
#define WS_I2C_SCL 14
#define WS_I2C_SDA 15
#define WS_LCD_CS 5
#define WS_LCD_CLK 6
#define WS_LCD_DC 4
#define WS_LCD_MOSI 7
#define WS_LCD_RST 38
#define WS_BACKLIGHT 40
#define WS_TOUCH_RST 39
#define WS_TOUCH_IRQ 13
#define WS_BOOT 0
typedef struct {
    bool down, boot;
    uint16_t x, y;
    int64_t irq_us, poll_us;
    uint32_t sequence, coalesced;
} ws_input_t;
#ifdef __cplusplus
extern "C" {
#endif
extern i2c_master_bus_handle_t ws_i2c;
extern SemaphoreHandle_t ws_i2c_mutex;
extern esp_lcd_panel_handle_t ws_panel;
esp_err_t ws_board_init(void);
esp_err_t ws_panel_init(void);
esp_err_t ws_transfer(int x, int y, int w, int h, const uint16_t *packed);
esp_err_t ws_transfer_wait(void);
void ws_backlight(bool enabled);
ws_input_t ws_input_read(void);
#ifdef __cplusplus
}
#endif
