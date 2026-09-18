#include "present.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "sdkconfig.h"
static uint16_t *bounce[2];
static bool inject_failure;
bool ws_pack(uint16_t *dst,size_t capacity,const uint16_t *fb,ws_rect_t r) {
    if(!r.w||!r.h||r.x>=WS_WIDTH||r.y>=WS_HEIGHT||r.w>WS_WIDTH-r.x||r.h>WS_HEIGHT-r.y||capacity<(size_t)r.w*r.h) return false;
    for(unsigned y=0;y<r.h;y++)for(unsigned x=0;x<r.w;x++) {
        uint16_t pixel=fb[(r.y+y)*WS_WIDTH+r.x+x];
        dst[y*r.w+x]=(uint16_t)((pixel<<8)|(pixel>>8));
    }
    return true;
}
esp_err_t ws_present_init(void) {
    bounce[0]=heap_caps_malloc(WS_BOUNCE_BYTES,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
#if CONFIG_HARNESS_DOUBLE_BOUNCE
    bounce[1]=heap_caps_malloc(WS_BOUNCE_BYTES,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
    if(!bounce[1])return ESP_ERR_NO_MEM;
#endif
    return bounce[0]?ESP_OK:ESP_ERR_NO_MEM;
}
void ws_present_inject_failure(void){inject_failure=true;}
esp_err_t ws_present(const uint16_t *fb,ws_rect_t r,ws_frame_metrics_t *m) {
    unsigned index=0,pending_bytes=0;
    for(unsigned row=0;row<r.h;row+=WS_STRIP_ROWS) {
        unsigned rows=r.h-row;if(rows>WS_STRIP_ROWS)rows=WS_STRIP_ROWS;
        int64_t t=esp_timer_get_time();
        if(!ws_pack(bounce[index],WS_BOUNCE_BYTES/2,fb,(ws_rect_t){r.x,r.y+row,r.w,rows}))return ESP_ERR_INVALID_ARG;
        m->us[M_PACK]+=(uint32_t)(esp_timer_get_time()-t);
        t=esp_timer_get_time();
        esp_err_t e=ws_transfer_wait();
        m->us[M_SPI]+=(uint32_t)(esp_timer_get_time()-t);
        if(e!=ESP_OK)return e;
        m->bytes+=pending_bytes;pending_bytes=0;
        if(inject_failure){inject_failure=false;return ESP_FAIL;}
        t=esp_timer_get_time();
        e=ws_transfer(r.x,r.y+row,r.w,rows,bounce[index]);
        m->us[M_SPI]+=(uint32_t)(esp_timer_get_time()-t);
        if(e!=ESP_OK)return e;
        pending_bytes=r.w*rows*2;
#if CONFIG_HARNESS_DOUBLE_BOUNCE
        index^=1;
#else
        t=esp_timer_get_time();e=ws_transfer_wait();m->us[M_SPI]+=(uint32_t)(esp_timer_get_time()-t);
        if(e!=ESP_OK)return e;
        m->bytes+=pending_bytes;pending_bytes=0;
#endif
    }
    int64_t t=esp_timer_get_time();esp_err_t e=ws_transfer_wait();m->us[M_SPI]+=(uint32_t)(esp_timer_get_time()-t);
    if(e==ESP_OK)m->bytes+=pending_bytes;
    return e;
}
