#include "native_wifi.h"
#include <string.h>
#include <stdatomic.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
static atomic_bool connected;
static bool started,configured;
static void event(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg;(void)data;
    if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP)atomic_store(&connected,true);
    if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&connected,false);
        if(configured)esp_wifi_connect();
    }
}
esp_err_t ws_wifi_connect(const char *ssid,const char *password) {
    if(!started||!ssid||!password||strlen(ssid)>32||strlen(password)>63)return ESP_ERR_INVALID_ARG;
    wifi_config_t config={0};
    memcpy(config.sta.ssid,ssid,strlen(ssid));memcpy(config.sta.password,password,strlen(password));
    esp_err_t e=esp_wifi_set_config(WIFI_IF_STA,&config);
    memset(&config,0,sizeof(config));
    if(e!=ESP_OK)return e;
    configured=true;
    return esp_wifi_connect();
}
esp_err_t ws_wifi_start(void) {
    if(started)return ESP_OK;
    esp_err_t e=nvs_flash_init();if(e!=ESP_OK)return e; /* Never erase NVS on failure. */
    if((e=esp_netif_init())!=ESP_OK)return e;
    e=esp_event_loop_create_default();if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return e;
    if(!esp_netif_create_default_wifi_sta())return ESP_ERR_NO_MEM;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();init.nvs_enable=0;
    if((e=esp_wifi_init(&init))!=ESP_OK)return e;
    if((e=esp_wifi_set_storage(WIFI_STORAGE_RAM))!=ESP_OK)return e;
    if((e=esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,NULL))!=ESP_OK)return e;
    if((e=esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,NULL))!=ESP_OK)return e;
    if((e=esp_wifi_set_mode(WIFI_MODE_STA))!=ESP_OK)return e;
    if((e=esp_wifi_start())!=ESP_OK)return e;
    started=true;
    nvs_handle_t nvs;
    e=nvs_open("ws183_wifi",NVS_READONLY,&nvs);
    if(e!=ESP_OK)e=nvs_open("wifi",NVS_READONLY,&nvs);
    if(e==ESP_OK) {
        char ssid[33]={0},password[64]={0};size_t s=sizeof(ssid),p=sizeof(password);
        esp_err_t a=nvs_get_str(nvs,"ssid",ssid,&s),b=nvs_get_str(nvs,"pass",password,&p);
        nvs_close(nvs);
        if(a==ESP_OK&&b==ESP_OK)e=ws_wifi_connect(ssid,password);
        memset(password,0,sizeof(password));
    }
    return e==ESP_ERR_NVS_NOT_FOUND?ESP_OK:e;
}
bool ws_wifi_connected(void){return atomic_load(&connected);}
void ws_wifi_reconnect(void){if(started&&configured)esp_wifi_disconnect();}
