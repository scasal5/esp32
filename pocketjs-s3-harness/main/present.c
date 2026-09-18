#include "present.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include <stdio.h>
static uint16_t *bounce[2];
static int fail_after=-1;
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
    if(!bounce[0])return ESP_ERR_NO_MEM;
#if CONFIG_HARNESS_DOUBLE_BOUNCE
    bounce[1]=heap_caps_malloc(WS_BOUNCE_BYTES,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
    if(!bounce[1])return ESP_ERR_NO_MEM;
#endif
#if CONFIG_HARNESS_GOLDEN
    uint16_t *test=heap_caps_malloc(WS_FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!test)return ESP_ERR_NO_MEM;
    for(unsigned i=0;i<WS_PIXELS;i++)test[i]=(uint16_t)(i*37U+11U);
    const ws_rect_t cases[]={{0,0,1,40},{239,0,1,40},{0,283,1,1},{239,283,1,1},
        {0,0,240,40},{1,39,31,40},{2,40,31,40},{1,244,239,40}};
    bool valid=true;
    for(unsigned n=0;n<sizeof(cases)/sizeof(cases[0]);n++){
        ws_rect_t r=cases[n];valid=ws_pack(bounce[0],WS_BOUNCE_BYTES/2,test,r)&&valid;
        for(unsigned y=0;y<r.h;y++)for(unsigned x=0;x<r.w;x++){
            uint16_t pixel=test[(r.y+y)*WS_WIDTH+r.x+x];
            if(bounce[0][y*r.w+x]!=(uint16_t)((pixel<<8)|(pixel>>8)))valid=false;
        }
    }
    valid=valid&&!ws_pack(bounce[0],1,test,(ws_rect_t){0,0,2,1})&&
        !ws_pack(bounce[0],WS_BOUNCE_BYTES/2,test,(ws_rect_t){239,283,2,1});
    heap_caps_free(test);
    printf("{\"type\":\"invariant\",\"test\":\"pack\",\"pass\":%s}\n",valid?"true":"false");
    if(!valid)return ESP_FAIL;
#endif
    return bounce[0]?ESP_OK:ESP_ERR_NO_MEM;
}
void ws_present_inject_failure(void){fail_after=2;}
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
        if(fail_after==0){fail_after=-1;return ESP_FAIL;}
        t=esp_timer_get_time();
        e=ws_transfer(r.x,r.y+row,r.w,rows,bounce[index]);
        m->us[M_SPI]+=(uint32_t)(esp_timer_get_time()-t);
        if(e!=ESP_OK)return e;
        if(fail_after>0)fail_after--;
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
