#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#ifndef CONFIG_HARNESS_WIFI
#define CONFIG_HARNESS_WIFI 0
#endif
#include "board_ws183.h"
#include "battery.h"
#include "present.h"
#include "scheduler.h"
#include "native_wifi.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_spiffs.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#if HARNESS_MODE_pocket
#include "pocket_ui.h"
#endif
#if HARNESS_MODE_headless
#include "guest_host.h"
#endif
static QueueHandle_t commands;
static void console(void *arg) {
    (void)arg;char line[160];unsigned length=0;
    puts("Commands: scenario 0..5 | smoke | soak | report | fail-transfer | wifi <ssid> <password>");
    for(;;){
        char c;if(usb_serial_jtag_read_bytes(&c,1,pdMS_TO_TICKS(100))!=1)continue;
        if(c=='\r'||c=='\n'){
            line[length]=0;
            if(length&&!xQueueSend(commands,line,0))puts("command queue full");
            memset(line,0,sizeof(line));length=0;
        }else if(length<sizeof(line)-1)line[length++]=c;
    }
}
static void native_pattern(uint16_t *fb){
    static const uint16_t colors[]={0xf800,0x07e0,0x001f,0xffff};
    for(unsigned y=0;y<WS_HEIGHT;y++)for(unsigned x=0;x<WS_WIDTH;x++)fb[y*WS_WIDTH+x]=colors[x/60];
    /* Four unique corners, plus alternating pixels to expose stride mistakes. */
    fb[0]=0xffff;fb[239]=0;fb[283*240]=0;fb[WS_PIXELS-1]=0xffff;
}
static void owner(void *arg) {
    (void)arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    const esp_partition_t *running=esp_ota_get_running_partition();
    printf("{\"type\":\"boot\",\"idf\":\"%s\",\"partition\":\"%s\",\"owner_stack\":%u,\"js_stack\":%u,\"js_heap_limit\":%u,\"wifi_enabled\":%s}\n",
        esp_app_get_description()->idf_ver,running->label,CONFIG_HARNESS_STACK_BYTES,
        CONFIG_HARNESS_JS_STACK_BYTES,CONFIG_HARNESS_HEAP_BYTES,CONFIG_HARNESS_WIFI?"true":"false");
#if HARNESS_MODE_headless
    esp_err_t result=ws_headless_tests();
    printf("{\"type\":\"headless_complete\",\"pass\":%s}\n",result==ESP_OK?"true":"false");
    for(;;){esp_task_wdt_reset();vTaskDelay(pdMS_TO_TICKS(100));}
#endif
    ESP_ERROR_CHECK(ws_board_init());ws_battery_start();
    ESP_ERROR_CHECK(ws_panel_init());ESP_ERROR_CHECK(ws_present_init());
    uint16_t *native=heap_caps_malloc(WS_FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(native?ESP_OK:ESP_ERR_NO_MEM);
    native_pattern(native);ws_frame_metrics_t initial={0};
    ESP_ERROR_CHECK(ws_present(native,(ws_rect_t){0,0,WS_WIDTH,WS_HEIGHT},&initial));ws_backlight(true);
    heap_caps_free(native);esp_task_wdt_reset();
    const esp_vfs_spiffs_conf_t storage={.base_path="/assets",.partition_label="assets",.max_files=4,.format_if_mount_failed=false};
    esp_err_t mount=esp_vfs_spiffs_register(&storage);
    size_t total=0,used=0;if(mount==ESP_OK)esp_spiffs_info("assets",&total,&used);
    printf("{\"type\":\"storage\",\"error\":%d,\"total\":%u,\"used\":%u}\n",mount,(unsigned)total,(unsigned)used);
#if CONFIG_HARNESS_WIFI
    printf("{\"type\":\"wifi_start\",\"error\":%d}\n",ws_wifi_start());
#endif
    bool fault=false;
#if HARNESS_MODE_pocket
    esp_err_t startup=mount==ESP_OK?ws_ui_start():mount;
    fault=startup!=ESP_OK;
    printf("{\"type\":\"guest_start\",\"error\":%d}\n",startup);
#endif
    ESP_ERROR_CHECK(ws_clock_start());
    unsigned scenario=0;uint32_t ticks=0,smoke_end=0;
    int64_t start=esp_timer_get_time(),next_memory=start+10000000,soak_end=0,next_reconnect=0;
    uint32_t last_input_seq=0;
    for(;;){
        esp_task_wdt_reset();char command[160];
        while(xQueueReceive(commands,command,0)){
            if(!strncmp(command,"scenario ",9)){unsigned s=(unsigned)atoi(command+9);if(s<6){ws_metrics_dump(scenario);scenario=s;}}
            else if(!strcmp(command,"smoke")){smoke_end=ticks+CONFIG_HARNESS_SMOKE_TICKS;start=esp_timer_get_time();}
            else if(!strcmp(command,"soak")){soak_end=esp_timer_get_time()+(int64_t)CONFIG_HARNESS_SOAK_SECONDS*1000000;next_reconnect=esp_timer_get_time()+600000000;start=esp_timer_get_time();}
            else if(!strcmp(command,"report"))ws_metrics_dump(scenario);
            else if(!strcmp(command,"fail-transfer"))ws_present_inject_failure();
            else if(!strncmp(command,"wifi ",5)){
#if CONFIG_HARNESS_WIFI
                char *pass=strchr(command+5,' ');if(pass){*pass++=0;printf("wifi error=%d\n",ws_wifi_connect(command+5,pass));}
#else
                puts("Wi-Fi is disabled in this build; never initialized.");
#endif
            }
            memset(command,0,sizeof(command));
        }
        ws_frame_metrics_t m={0};int64_t due=ws_clock_wait(&m.skipped);
        int64_t begin=esp_timer_get_time();ws_input_t input=ws_input_read();m.coalesced=input.coalesced;
#if HARNESS_MODE_pocket
        esp_err_t e=ESP_OK;
        if(!fault)e=ws_ui_frame(input,scenario,&m);
        if(e!=ESP_OK){fault=true;printf("{\"type\":\"guest_fault\",\"error\":%d,\"ticks\":%lu}\n",e,(unsigned long)ticks);}
#endif
        int64_t end=esp_timer_get_time();m.us[M_TOTAL]=(uint32_t)(end-begin);m.deadline_met=end<=due+33333;
        if(m.presented&&input.sequence!=last_input_seq&&input.irq_us){
            m.irq_valid=true;m.poll_valid=true;m.us[M_IRQ]=(uint32_t)(end-input.irq_us);m.us[M_POLL]=(uint32_t)(end-input.poll_us);last_input_seq=input.sequence;
        }
        ws_metrics_add(&m);ticks++;
        if(end>=next_memory){
            size_t guest_used=0;
#if HARNESS_MODE_pocket
            guest_used=ws_ui_heap();
#endif
            ws_memory_sample(scenario,ticks,guest_used);next_memory=end+10000000;
            printf("{\"type\":\"wifi\",\"connected\":%s}\n",ws_wifi_connected()?"true":"false");
        }
        if(smoke_end||soak_end){
            unsigned s=((end-start)/70000000)%6;
            if(s!=scenario){ws_metrics_dump(scenario);scenario=s;}
        }
        if(soak_end&&end>=next_reconnect){ws_wifi_reconnect();next_reconnect=end+600000000;}
        if((smoke_end&&ticks>=smoke_end)||(soak_end&&end>=soak_end)){
            ws_metrics_dump(scenario);printf("{\"type\":\"run_complete\",\"fault\":%s,\"elapsed_us\":%lld,\"ticks\":%lu}\n",fault?"true":"false",end-start,(unsigned long)ticks);
            smoke_end=0;soak_end=0;
        }
    }
}
void app_main(void) {
    const esp_task_wdt_config_t watchdog={.timeout_ms=2000,.idle_core_mask=0,.trigger_panic=true};
    ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&watchdog));
    usb_serial_jtag_driver_config_t usb=USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    commands=xQueueCreate(4,160);ESP_ERROR_CHECK(commands?ESP_OK:ESP_ERR_NO_MEM);
    configASSERT(xTaskCreate(console,"console",4096,NULL,3,NULL)==pdPASS);
    configASSERT(xTaskCreatePinnedToCoreWithCaps(owner,"owner",CONFIG_HARNESS_STACK_BYTES,NULL,5,NULL,1,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)==pdPASS);
}
