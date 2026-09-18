#include "pocket_ui.h"
#include "guest_host.h"
#include "host_contract.h"
#include "pocketjs/ui_qjs.h"
#include "pocketjs/render_rgb565.h"
#include "pocketjs/guest_quickjs.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_rom_crc.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdio.h>
static pocketjs_guest_t *guest;
static pocketjs_package_t *package;
static void *package_bytes;
static pocketjs_ui_core_t *core;
static pocketjs_ui_qjs_t *binding;
static pocketjs_rgb565_renderer_t *renderer;
static pocketjs_rgb565_target_t *target;
static uint16_t *fb,*reference;
static unsigned scenario_last=99;
static int64_t static_since;
static bool static_reported;
static bool recovery;
static unsigned golden_cases;
void ws_ui_stop(void) {
    ws_guest_guard_end();
    if(renderer&&target)pocketjs_rgb565_abort(renderer,target);
    if(target)pocketjs_rgb565_target_destroy(target);target=NULL;
    if(renderer)pocketjs_rgb565_renderer_destroy(renderer);renderer=NULL;
    if(guest)pocketjs_guest_destroy(guest);guest=NULL;
    if(binding)pocketjs_ui_qjs_destroy(binding);binding=NULL;
    if(core)pocketjs_ui_core_destroy(core);core=NULL;
    if(package)pocketjs_package_close(package);package=NULL;
    heap_caps_free(package_bytes);package_bytes=NULL;
    heap_caps_free(fb);fb=NULL;heap_caps_free(reference);reference=NULL;
}
esp_err_t ws_ui_start(void) {
    pocketjs_package_variant_t variant={.struct_size=sizeof(variant)};
    esp_err_t e=ws_package_load(&package_bytes,&package,&variant);if(e!=ESP_OK)return e;
    e=ws_guest_create(&guest);if(e!=ESP_OK)goto fail;
    pocketjs_ui_core_config_t config;pocketjs_ui_core_config_defaults(&config);
    config.logical_width=WS_WIDTH;config.logical_height=WS_HEIGHT;config.raster_density=1;config.tick_hz=30;
    e=pocketjs_ui_core_create(&config,&core);if(e!=ESP_OK)goto fail;
    const pocketjs_ui_qjs_config_t b={.struct_size=sizeof(b),.target_id=ws_contract.target_id,.host_abi=ws_contract.host_abi};
    e=pocketjs_ui_qjs_create(guest,core,&b,&binding);if(e!=ESP_OK)goto fail;
    e=pocketjs_ui_qjs_feed_pak(binding,variant.pak.data,variant.pak.size);if(e!=ESP_OK)goto fail;
    e=pocketjs_ui_qjs_mount(binding);if(e!=ESP_OK)goto fail;
#if CONFIG_HARNESS_GOLDEN
    {
        JSContext *ctx=pocketjs_guest_quickjs_context(guest);JSValue global=JS_GetGlobalObject(ctx);
        JS_SetPropertyStr(ctx,global,"__wsGolden",JS_TRUE);JS_FreeValue(ctx,global);
    }
#endif
    ws_guest_guard_begin(guest,500000);
    e=pocketjs_guest_eval(guest,(const char *)variant.javascript.data,variant.javascript.size-1,"harness");
    if(ws_guest_guard_end())e=ESP_ERR_TIMEOUT;
    if(e!=ESP_OK)goto fail;
    pocketjs_rgb565_renderer_config_t rc;pocketjs_rgb565_renderer_config_defaults(&rc);rc.scale=1;
    e=pocketjs_rgb565_renderer_create(&rc,&renderer);if(e!=ESP_OK)goto fail;
    e=pocketjs_rgb565_target_create(&target);if(e!=ESP_OK)goto fail;
    fb=heap_caps_calloc(WS_PIXELS,2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!fb){e=ESP_ERR_NO_MEM;goto fail;}
#if CONFIG_HARNESS_GOLDEN
    reference=heap_caps_calloc(WS_PIXELS,2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!reference){e=ESP_ERR_NO_MEM;goto fail;}
#endif
    return ESP_OK;
fail:ws_ui_stop();return e;
}
static esp_err_t set_scenario(unsigned scenario) {
    JSContext *ctx=pocketjs_guest_quickjs_context(guest);
    JSValue global=JS_GetGlobalObject(ctx);
    int result=JS_SetPropertyStr(ctx,global,"__wsScenario",JS_NewUint32(ctx,scenario));
    JS_FreeValue(ctx,global);
    return result<0?ESP_FAIL:ESP_OK;
}
esp_err_t ws_ui_frame(ws_input_t in,unsigned scenario,ws_frame_metrics_t *m) {
    if(!guest)return ESP_ERR_INVALID_STATE;
    bool changed=scenario!=scenario_last;
    int64_t t=esp_timer_get_time();ws_guest_guard_begin(guest,20000);
    esp_err_t e=set_scenario(scenario);if(e!=ESP_OK){ws_guest_guard_end();return e;}
    ws_guest_set_battery(ws_battery_snapshot());
    pocketjs_ui_touch_t touch={.id=0,.x=in.x,.y=in.y};
    pocketjs_ui_input_t input={.struct_size=sizeof(input),.buttons=in.boot?0x2000U:0,
        .touches=in.down?&touch:NULL,.touch_count=in.down?1:0};
    pocketjs_ui_frame_view_t frame={.struct_size=sizeof(frame)};
    e=pocketjs_ui_turn(binding,&input,&frame);
    if(ws_guest_guard_end())e=ESP_ERR_TIMEOUT;
    m->us[M_TURN]=(uint32_t)(esp_timer_get_time()-t);
    if(e!=ESP_OK)return e; /* No mutation reaches physical display on guest fault. */
    pocketjs_rgb565_damage_plan_t plan={.struct_size=sizeof(plan)};
    t=esp_timer_get_time();e=pocketjs_rgb565_prepare(renderer,target,&frame,&plan);
    m->us[M_PREPARE]=(uint32_t)(esp_timer_get_time()-t);
    if(e!=ESP_OK)return e;
    m->regions=plan.region_count;
    for(unsigned i=0;i<plan.region_count;i++)m->planned+=plan.regions[i].width*plan.regions[i].height*2;
    if(changed){static_since=esp_timer_get_time();static_reported=false;}
    if(scenario==0&&!changed&&!recovery&&plan.region_count) {
        printf("{\"type\":\"invariant\",\"test\":\"static\",\"pass\":false}\n");
        pocketjs_rgb565_abort(renderer,target);return ESP_FAIL;
    }
    if(scenario==0&&!static_reported&&esp_timer_get_time()-static_since>=60000000) {
        printf("{\"type\":\"invariant\",\"test\":\"static\",\"pass\":true}\n");static_reported=true;
    }
    for(unsigned i=0;i<plan.region_count;i++) {
        pocketjs_rgb565_rect_t r=plan.regions[i];
        /* Battery panel occupies y=64..160; outside damage is a correctness failure. */
        if(scenario==2&&!changed&&!recovery&&(r.y<64||r.y+r.height>160)){
            pocketjs_rgb565_abort(renderer,target);return ESP_FAIL;
        }
        for(unsigned row=0;row<r.height;row+=WS_STRIP_ROWS){
            unsigned h=r.height-row;if(h>WS_STRIP_ROWS)h=WS_STRIP_ROWS;
            pocketjs_rgb565_rect_t strip={r.x,r.y+row,r.width,h};
            pocketjs_rgb565_render_stats_t stats={.struct_size=sizeof(stats)};
            t=esp_timer_get_time();
            e=pocketjs_rgb565_render_strip(renderer,&frame,fb+strip.y*WS_WIDTH,WS_WIDTH*h,strip,NULL,&stats);
            m->us[M_RENDER]+=(uint32_t)(esp_timer_get_time()-t);
            if(e!=ESP_OK)goto abort;
        }
    }
#if CONFIG_HARNESS_GOLDEN
    {
        pocketjs_rgb565_render_stats_t stats={.struct_size=sizeof(stats)};
        e=pocketjs_rgb565_render_strip(renderer,&frame,reference,WS_PIXELS,
            (pocketjs_rgb565_rect_t){0,0,WS_WIDTH,WS_HEIGHT},NULL,&stats);
        if(e!=ESP_OK)goto abort;
        if(esp_rom_crc32_le(0,(uint8_t *)fb,WS_FRAME_BYTES)!=esp_rom_crc32_le(0,(uint8_t *)reference,WS_FRAME_BYTES)||
           memcmp(fb,reference,WS_FRAME_BYTES)){e=ESP_FAIL;goto abort;}
        if(scenario==3&&++golden_cases==16)puts("{\"type\":\"invariant\",\"test\":\"golden\",\"pass\":true}");
    }
#endif
    for(unsigned i=0;i<plan.region_count;i++) {
        pocketjs_rgb565_rect_t r=plan.regions[i];
        e=ws_present(fb,(ws_rect_t){r.x,r.y,r.width,r.height},m);
        if(e==ESP_FAIL)e=ESP_ERR_NOT_FINISHED; /* injected transfer failure, recover next frame */
        if(e!=ESP_OK)goto abort;
    }
    e=pocketjs_rgb565_commit(renderer,target,&frame);
    if(e!=ESP_OK)goto abort;
    if(recovery)puts("{\"type\":\"invariant\",\"test\":\"transfer_recovery\",\"pass\":true}");
    scenario_last=scenario;recovery=false;m->presented=plan.region_count!=0;
    return ESP_OK;
abort:
    pocketjs_rgb565_abort(renderer,target);pocketjs_rgb565_target_invalidate(target);recovery=true;
    return e;
}
size_t ws_ui_heap(void) {
    pocketjs_guest_stats_t stats={.struct_size=sizeof(stats)};
    return guest&&pocketjs_guest_stats(guest,&stats)==ESP_OK?stats.heap_used:0;
}
