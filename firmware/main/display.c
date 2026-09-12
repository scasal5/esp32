#include "display.h"

#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "esp_lvgl_port.h"

static const char *TAG = "display";

/*
 * Reloj del SPI del panel. El BSP fija 24 MHz, y a 16 bits por pixel una
 * pantalla entera son 240*284*16 = 1,09 Mbit: 45 ms de pura transferencia por
 * frame. Es el piso que ninguna optimizacion de CPU puede bajar.
 *
 * Se sube por escalones y se mide: 40 MHz deja ese piso en 27 ms. Mas arriba
 * depende del FPC de 1,83", y Waveshare se quedo en 24 por algo. Si aparece
 * nieve o lineas corridas, bajar un escalon. Es la unica constante que hay que
 * tocar.
 */
#define DISPLAY_SPI_CLK_HZ  (24 * 1000 * 1000)

/*
 * Altura de cada tira de render, en lineas. Con doble buffer son dos de estas
 * en RAM interna: 40 lineas * 240 px * 2 bytes = 19 KB cada una. El BSP usa una
 * sola tira de 100 lineas (48 KB), que es mas grande pero no se puede solapar
 * con la transferencia.
 */
#define DISPLAY_BUF_LINES   40

static lv_display_t *lcd_init(void)
{
    ESP_LOGI(TAG, "SPI a %d MHz, %d lineas x2",
             DISPLAY_SPI_CLK_HZ / 1000000, DISPLAY_BUF_LINES);

    /* max_transfer_sz para la tira completa: el default de 4092 bytes la
       partiria en una docena de transacciones. */
    const spi_bus_config_t bus_cfg = {
        .sclk_io_num = BSP_LCD_PCLK,
        .mosi_io_num = BSP_LCD_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = BSP_LCD_H_RES * DISPLAY_BUF_LINES * sizeof(uint16_t),
    };
    esp_err_t err = spi_bus_initialize(BSP_LCD_SPI_NUM, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return NULL;
    }

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = BSP_LCD_DC,
        .cs_gpio_num = BSP_LCD_CS,
        .pclk_hz = DISPLAY_SPI_CLK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM,
                                   &io_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel io: %s", esp_err_to_name(err));
        return NULL;
    }

    /* Misma secuencia que bsp_display_new(): el panel no lleva offsets, y la
       inversion de color es de esta placa, no del driver. */
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order = BSP_LCD_COLOR_SPACE,
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
        .flags = { .reset_active_high = 0 },
    };
    esp_lcd_panel_handle_t panel = NULL;
    err = esp_lcd_new_panel_st7789(io, &panel_cfg, &panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "st7789: %s", esp_err_to_name(err));
        return NULL;
    }
    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_disp_on_off(panel, true);
    esp_lcd_panel_invert_color(panel, true);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = BSP_LCD_H_RES * DISPLAY_BUF_LINES,
        .double_buffer = true,
        .monochrome = false,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            /* El BSP deja sw_rotate en true con rotacion identidad: es una
               pasada de copia por cada tira, a cambio de nada. */
            .sw_rotate = false,
            .buff_dma = true,
            .buff_spiram = false,
            .full_refresh = 0,
            .direct_mode = 0,
            .swap_bytes = true,
        },
    };
    return lvgl_port_add_disp(&disp_cfg);
}

lv_display_t *display_start(void)
{
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    esp_err_t err = lvgl_port_init(&port_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init: %s", esp_err_to_name(err));
        return NULL;
    }

    lv_display_t *disp = lcd_init();
    if (disp == NULL) {
        return NULL;
    }

    /* Tactil por el BSP, sin cambios. */
    esp_lcd_touch_handle_t tp = NULL;
    err = bsp_touch_new(NULL, &tp);
    if (err != ESP_OK || tp == NULL) {
        ESP_LOGW(TAG, "sin tactil: %s", esp_err_to_name(err));
    } else {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = disp,
            .handle = tp,
        };
        if (lvgl_port_add_touch(&touch_cfg) == NULL) {
            ESP_LOGW(TAG, "lvgl_port_add_touch fallo");
        }
    }

    return disp;
}
