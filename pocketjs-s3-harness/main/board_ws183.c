#include "board_ws183.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_timer.h"
#include "esp_check.h"

i2c_master_bus_handle_t ws_i2c;
SemaphoreHandle_t ws_i2c_mutex;
esp_lcd_panel_handle_t ws_panel;
static esp_lcd_touch_handle_t touch;
static SemaphoreHandle_t dma_done;
static bool in_flight;
static portMUX_TYPE irq_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t irq_us;
static uint32_t irq_seq;

static void IRAM_ATTR touch_irq(void *arg) {
    (void)arg;
    portENTER_CRITICAL_ISR(&irq_lock);
    irq_us = esp_timer_get_time();
    ++irq_seq;
    portEXIT_CRITICAL_ISR(&irq_lock);
}
static bool dma_complete(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *arg) {
    (void)io; (void)event; (void)arg;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(dma_done, &wake);
    return wake == pdTRUE;
}
void ws_backlight(bool enabled) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, enabled ? 819 : 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
esp_err_t ws_board_init(void) {
    ws_i2c_mutex = xSemaphoreCreateMutex();
    if (!ws_i2c_mutex) return ESP_ERR_NO_MEM;
    const i2c_master_bus_config_t bus = {.i2c_port=0, .sda_io_num=WS_I2C_SDA,
        .scl_io_num=WS_I2C_SCL, .clk_source=I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt=7, .flags.enable_internal_pullup=true};
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &ws_i2c), "board", "i2c");
    const gpio_config_t button = {.pin_bit_mask=1ULL<<WS_BOOT, .mode=GPIO_MODE_INPUT,
                                  .pull_up_en=GPIO_PULLUP_ENABLE};
    ESP_RETURN_ON_ERROR(gpio_config(&button), "board", "button");
    esp_lcd_panel_io_i2c_config_t io = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    io.scl_speed_hz = 400000;
    esp_lcd_panel_io_handle_t touch_io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(ws_i2c, &io, &touch_io), "board", "touch io");
    const esp_lcd_touch_config_t tc = {.x_max=WS_WIDTH, .y_max=WS_HEIGHT,
        .rst_gpio_num=WS_TOUCH_RST, .int_gpio_num=GPIO_NUM_NC,
        .levels={.reset=0,.interrupt=0}};
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst816s(touch_io, &tc, &touch), "board", "touch");
    gpio_config_t irq = {.pin_bit_mask=1ULL<<WS_TOUCH_IRQ, .mode=GPIO_MODE_INPUT,
        .pull_up_en=GPIO_PULLUP_ENABLE, .intr_type=GPIO_INTR_NEGEDGE};
    ESP_RETURN_ON_ERROR(gpio_config(&irq), "board", "irq");
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    return gpio_isr_handler_add(WS_TOUCH_IRQ, touch_irq, NULL);
}
esp_err_t ws_panel_init(void) {
    const ledc_timer_config_t timer = {.speed_mode=LEDC_LOW_SPEED_MODE,
        .duty_resolution=LEDC_TIMER_10_BIT,.timer_num=LEDC_TIMER_0,.freq_hz=5000,.clk_cfg=LEDC_AUTO_CLK};
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), "board", "ledc");
    const ledc_channel_config_t light = {.gpio_num=WS_BACKLIGHT,.speed_mode=LEDC_LOW_SPEED_MODE,
        .channel=LEDC_CHANNEL_0,.timer_sel=LEDC_TIMER_0,.duty=0};
    ESP_RETURN_ON_ERROR(ledc_channel_config(&light), "board", "light");
    const spi_bus_config_t bus = {.sclk_io_num=WS_LCD_CLK,.mosi_io_num=WS_LCD_MOSI,
        .miso_io_num=-1,.quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=WS_BOUNCE_BYTES};
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO),"board","spi");
    dma_done = xSemaphoreCreateBinary();
    if (!dma_done) return ESP_ERR_NO_MEM;
    const esp_lcd_panel_io_spi_config_t io_config = {.dc_gpio_num=WS_LCD_DC,.cs_gpio_num=WS_LCD_CS,
        .pclk_hz=WS_SPI_HZ,.lcd_cmd_bits=8,.lcd_param_bits=8,.spi_mode=0,.trans_queue_depth=1,
        .on_color_trans_done=dma_complete};
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST,&io_config,&io),"board","panel io");
    const esp_lcd_panel_dev_config_t config = {.reset_gpio_num=WS_LCD_RST,
        .rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,.bits_per_pixel=16};
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io,&config,&ws_panel),"board","panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(ws_panel),"board","reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(ws_panel),"board","init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(ws_panel,true),"board","invert");
    return esp_lcd_panel_disp_on_off(ws_panel,true);
}
esp_err_t ws_transfer_wait(void) {
    if (!in_flight) return ESP_OK;
    /* Timeout is fatal to this presentation: buffer must NOT be recycled. */
    if (!xSemaphoreTake(dma_done,pdMS_TO_TICKS(500))) return ESP_ERR_TIMEOUT;
    in_flight=false;
    return ESP_OK;
}
esp_err_t ws_transfer(int x,int y,int w,int h,const uint16_t *packed) {
    if (in_flight) return ESP_ERR_INVALID_STATE;
    esp_err_t err=esp_lcd_panel_draw_bitmap(ws_panel,x,y,x+w,y+h,packed);
    if (err==ESP_OK) in_flight=true;
    return err;
}
ws_input_t ws_input_read(void) {
    static uint32_t last_seq;
    static bool raw_boot,stable_boot;
    static int64_t boot_since;
    ws_input_t out={.poll_us=esp_timer_get_time()};
    portENTER_CRITICAL(&irq_lock);
    out.sequence=irq_seq; out.irq_us=irq_us;
    portEXIT_CRITICAL(&irq_lock);
    uint32_t delta=out.sequence-last_seq;
    out.coalesced=delta>0 ? delta-1 : 0;
    if (!delta) out.irq_us=0;
    last_seq=out.sequence;
    uint16_t strength; uint8_t count=0;
    xSemaphoreTake(ws_i2c_mutex,portMAX_DELAY);
    if (esp_lcd_touch_read_data(touch)==ESP_OK)
        out.down=esp_lcd_touch_get_coordinates(touch,&out.x,&out.y,&strength,&count,1) && count;
    xSemaphoreGive(ws_i2c_mutex);
    if (out.x>=WS_WIDTH || out.y>=WS_HEIGHT) out.down=false;
    bool now=!gpio_get_level(WS_BOOT);
    if (now!=raw_boot) {raw_boot=now;boot_since=out.poll_us;}
    if (out.poll_us-boot_since>=40000) stable_boot=now;
    out.boot=stable_boot;
    return out;
}
