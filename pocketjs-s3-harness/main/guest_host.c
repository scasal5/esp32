#include "guest_host.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "pocketjs/guest_quickjs.h"
#include "host_contract.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
static ws_battery_t snapshot;
static int64_t snapshot_age_ms;
static esp_timer_handle_t deadline_timer;
static portMUX_TYPE guard_lock=portMUX_INITIALIZER_UNLOCKED;
static pocketjs_guest_t *guard_guest;
static int64_t guard_deadline;
static bool expired;
static void interrupt_due(void *arg) {
    (void)arg;
    portENTER_CRITICAL(&guard_lock);
    if(guard_guest&&esp_timer_get_time()>=guard_deadline){expired=true;pocketjs_guest_interrupt(guard_guest);}
    portEXIT_CRITICAL(&guard_lock);
}
void ws_guest_guard_begin(pocketjs_guest_t *guest,int64_t budget_us) {
    if(!deadline_timer){
        esp_timer_create_args_t config={.callback=interrupt_due,.name="guest-budget"};
        ESP_ERROR_CHECK(esp_timer_create(&config,&deadline_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(deadline_timer,1000));
    }
    portENTER_CRITICAL(&guard_lock);
    guard_guest=guest;guard_deadline=esp_timer_get_time()+budget_us;expired=false;
    portEXIT_CRITICAL(&guard_lock);
}
bool ws_guest_guard_end(void) {
    portENTER_CRITICAL(&guard_lock);
    bool was_expired=expired || (guard_guest&&esp_timer_get_time()>=guard_deadline);
    guard_guest=NULL;
    portEXIT_CRITICAL(&guard_lock);
    return was_expired;
}
esp_err_t ws_guest_derived(esp_err_t eval_error, bool timeout) {
    return timeout?ESP_ERR_TIMEOUT:eval_error;
}
const char *ws_guest_cause(esp_err_t eval_error, bool timeout) {
    if(eval_error==ESP_OK&&!timeout)return "ok";
    return timeout?"timeout":"js";
}
void ws_guest_set_battery(ws_battery_t value){
    snapshot=value;snapshot_age_ms=value.sampled_us?(esp_timer_get_time()-value.sampled_us)/1000:0;
}
static JSValue battery_read(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;(void)argc;(void)argv;
    JSValue result=JS_NewObject(ctx);
    if(JS_IsException(result))return result;
#define SET(name,value) do{if(JS_SetPropertyStr(ctx,result,name,value)<0){JS_FreeValue(ctx,result);return JS_EXCEPTION;}}while(0)
    SET("available",JS_NewBool(ctx,snapshot.available));SET("present",JS_NewBool(ctx,snapshot.present));
    SET("charging",JS_NewBool(ctx,snapshot.charging));SET("vbus",JS_NewBool(ctx,snapshot.vbus));
    SET("percent",snapshot.available&&snapshot.percent>=0&&snapshot.percent<=100?JS_NewInt32(ctx,snapshot.percent):JS_NULL);
    SET("millivolts",snapshot.available&&snapshot.millivolts>=0?JS_NewInt32(ctx,snapshot.millivolts):JS_NULL);
    SET("ageMs",JS_NewInt64(ctx,snapshot_age_ms));
#undef SET
    return result;
}
static esp_err_t install(JSContext *ctx,void *arg) {
    (void)arg;
    JSValue global=JS_GetGlobalObject(ctx),battery=JS_NewObject(ctx);
    int a=JS_SetPropertyStr(ctx,battery,"apiVersion",JS_NewInt32(ctx,1));
    int b=JS_SetPropertyStr(ctx,battery,"read",JS_NewCFunction(ctx,battery_read,"read",0));
    int c=JS_SetPropertyStr(ctx,global,"battery",battery);
    JS_FreeValue(ctx,global);
    return a<0||b<0||c<0?ESP_FAIL:ESP_OK;
}
static esp_err_t immutable_fixture(JSContext *ctx,void *arg) {
    (void)arg;
    static uint8_t original[]={1,2,3,4};
    JSValue global=JS_GetGlobalObject(ctx);
    JSValue buffer=JS_NewArrayBuffer(ctx,original,sizeof(original),NULL,NULL,false);
    if(JS_IsException(buffer)){JS_FreeValue(ctx,global);return ESP_FAIL;}
    if(JS_SetImmutableArrayBuffer(buffer,true)<0){JS_FreeValue(ctx,buffer);JS_FreeValue(ctx,global);return ESP_FAIL;}
    int result=JS_SetPropertyStr(ctx,global,"__immutable",buffer);
    JS_FreeValue(ctx,global);
    return result<0?ESP_FAIL:ESP_OK;
}
esp_err_t ws_guest_create(pocketjs_guest_t **guest) {
    pocketjs_guest_config_t config;pocketjs_guest_config_defaults(&config);
    config.heap_limit=CONFIG_HARNESS_HEAP_BYTES;config.stack_limit=CONFIG_HARNESS_JS_STACK_BYTES;config.prefer_psram=true;
    esp_err_t e=pocketjs_guest_create(&config,guest);
    if(e!=ESP_OK)return e;
    e=pocketjs_guest_quickjs_install_once(*guest,"ws183.battery.v1",install,NULL);
    if(e==ESP_OK){
        JSContext *ctx=pocketjs_guest_quickjs_context(*guest);
        printf("{\"type\":\"guest_memory_policy\",\"prefer_psram\":true,\"context_in_psram\":%s,\"runtime_in_psram\":%s}\n",
            esp_ptr_external_ram(ctx)?"true":"false",esp_ptr_external_ram(JS_GetRuntime(ctx))?"true":"false");
    }
    if(e!=ESP_OK){pocketjs_guest_destroy(*guest);*guest=NULL;}
    return e;
}
esp_err_t ws_package_load(void **bytes,pocketjs_package_t **package,pocketjs_package_variant_t *variant) {
    *bytes=NULL;*package=NULL;
    FILE *f=fopen("/assets/harness.pocket","rb");if(!f)return ESP_ERR_NOT_FOUND;
    struct stat st;
    if(fstat(fileno(f),&st)||st.st_size<=0||st.st_size>2*1024*1024){fclose(f);return ESP_ERR_INVALID_SIZE;}
    *bytes=heap_caps_malloc(st.st_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!*bytes){fclose(f);return ESP_ERR_NO_MEM;}
    size_t got=fread(*bytes,1,st.st_size,f);fclose(f);
    esp_err_t e=got==(size_t)st.st_size?pocketjs_package_open(*bytes,got,0,package):ESP_FAIL;
    if(e==ESP_OK)e=pocketjs_package_select(*package,&ws_contract,variant);
    if(e!=ESP_OK){if(*package)pocketjs_package_close(*package);*package=NULL;heap_caps_free(*bytes);*bytes=NULL;}
    return e;
}
esp_err_t ws_headless_tests(void) {
    const esp_vfs_spiffs_conf_t storage={.base_path="/assets",.partition_label="assets",.max_files=4,.format_if_mount_failed=false};
    esp_err_t mount=esp_spiffs_mounted("assets")?ESP_OK:esp_vfs_spiffs_register(&storage);
    void *bytes=NULL;pocketjs_package_t *loaded=NULL;
    pocketjs_package_variant_t variant={.struct_size=sizeof(variant)};
    esp_err_t admission=mount==ESP_OK?ws_package_load(&bytes,&loaded,&variant):mount;
    bool admitted=admission==ESP_OK;
    printf("{\"type\":\"headless\",\"test\":\"package_admission\",\"pass\":%s,\"error\":%d}\n",admitted?"true":"false",admission);
    if(admitted){
        pocketjs_package_host_contract_t wrong=ws_contract;wrong.tick_hz++;
        bool rejected=pocketjs_package_select(loaded,&wrong,&variant)!=ESP_OK;
#define REJECT_FIELD(field,value) do{wrong=ws_contract;wrong.field=(value);rejected=(pocketjs_package_select(loaded,&wrong,&variant)!=ESP_OK)&&rejected;}while(0)
        REJECT_FIELD(target_id,"another-host");
        REJECT_FIELD(host_abi,ws_contract.host_abi+1);
        REJECT_FIELD(logical_width,ws_contract.logical_width+1);
        REJECT_FIELD(physical_height,ws_contract.physical_height+1);
        REJECT_FIELD(raster_density,ws_contract.raster_density+1);
        REJECT_FIELD(presentation,POCKETJS_PRESENTATION_FIT);
#undef REJECT_FIELD
        wrong=ws_contract;wrong.profile_hash[0]^=1;
        rejected=rejected&&pocketjs_package_select(loaded,&wrong,&variant)!=ESP_OK;
        pocketjs_package_t *bad=NULL;
        rejected=rejected&&pocketjs_package_open(bytes,16,0,&bad)!=ESP_OK;
        if(bad)pocketjs_package_close(bad);
        printf("{\"type\":\"headless\",\"test\":\"package_rejections\",\"pass\":%s}\n",rejected?"true":"false");
        admitted=rejected;
        pocketjs_package_close(loaded);heap_caps_free(bytes);
    }
    static const struct {const char *name,*js;bool frame,fail;} cases[]={
        {"battery","if(battery.apiVersion!==1||battery.read().percent!==42)throw Error('battery');globalThis.frame=()=>{}",false,false},
        {"version","if(battery.apiVersion!==2)throw Error('battery version');globalThis.frame=()=>{}",false,true},
        {"eval_runaway","while(true){}",false,true},
        {"frame_runaway","globalThis.frame=()=>{while(true){}}",true,true},
        {"promise_runaway","globalThis.frame=()=>{Promise.resolve().then(()=>{while(true){}})}",true,true},
        {"promise_chain","globalThis.frame=()=>{function again(){Promise.resolve().then(again)};again()}",true,true},
        {"stack","function recurse(){return 1+recurse()};recurse()",false,true},
        {"heap","let keep=[];while(true)keep.push(new ArrayBuffer(65536))",false,true},
        {"immutable_reverse","let a=new Uint8Array(__immutable),caught=false;try{a.reverse()}catch(e){caught=true};if(!caught||a[0]!==1)throw Error('reverse');globalThis.frame=()=>{}",false,false},
        {"immutable_species","let a=new Uint8Array([7,8,9,10]),out=new Uint8Array(__immutable);a.constructor={[Symbol.species]:function(){return out}};let caught=false;try{a.map(x=>x+1)}catch(e){caught=true};if(!caught||out[0]!==1)throw Error('species');if(out.subarray(0,1)[0]!==1)throw Error('subarray');globalThis.frame=()=>{}",false,false},
    };
    ws_guest_set_battery((ws_battery_t){.available=true,.present=true,.percent=42,.millivolts=3800,.sampled_us=esp_timer_get_time()});
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        pocketjs_guest_t *guest=NULL;esp_err_t e=ws_guest_create(&guest);if(e!=ESP_OK)return e;
        e=pocketjs_guest_quickjs_install(guest,immutable_fixture,NULL);
        if(e!=ESP_OK){pocketjs_guest_destroy(guest);return e;}
        esp_task_wdt_reset();
        int64_t begun=esp_timer_get_time();
        ws_guest_guard_begin(guest,500000);
        e=pocketjs_guest_eval(guest,cases[i].js,strlen(cases[i].js),cases[i].name);
        bool timeout=ws_guest_guard_end();
        if(cases[i].frame&&(e!=ESP_OK||timeout)){
            printf("{\"type\":\"headless\",\"test\":\"%s_setup\",\"pass\":false}\n",cases[i].name);
            pocketjs_guest_destroy(guest);return ESP_FAIL;
        }
        if(cases[i].frame&&e==ESP_OK&&!timeout) {
            pocketjs_guest_frame_t frame={.struct_size=sizeof(frame)};
            begun=esp_timer_get_time();
            ws_guest_guard_begin(guest,20000);e=pocketjs_guest_frame(guest,&frame);timeout=ws_guest_guard_end();
        }
        int64_t elapsed=esp_timer_get_time()-begun;
        bool passed=((e!=ESP_OK||timeout)==cases[i].fail);
        if(strstr(cases[i].name,"runaway")||!strcmp(cases[i].name,"promise_chain"))
            passed=passed&&timeout&&elapsed<(cases[i].frame?100000:750000);
        if(!strcmp(cases[i].name,"heap")||!strcmp(cases[i].name,"stack"))passed=passed&&!timeout;
        printf("{\"type\":\"headless\",\"test\":\"%s\",\"pass\":%s,\"timeout\":%s,\"elapsed_us\":%lld,\"stack_free\":%u}\n",
               cases[i].name,passed?"true":"false",timeout?"true":"false",elapsed,(unsigned)uxTaskGetStackHighWaterMark(NULL));
        pocketjs_guest_destroy(guest);esp_task_wdt_reset();
        if(!passed)return ESP_FAIL;
        vTaskDelay(1);
    }
    return admitted?ESP_OK:ESP_FAIL;
}
