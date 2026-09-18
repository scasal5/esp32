#include <stddef.h>
#include "metrics.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
/* Fixed RAM, 1 ms bins through 250 ms; final bin is overflow, never a false p95. */
static uint32_t hist[M_COUNT][252],counts[M_COUNT],maximum[M_COUNT];
static uint64_t sum[M_COUNT],bytes,planned;
static uint32_t frames,presents,possible,misses,impossible_misses,skipped,coalesced;
static int64_t last_present,window_start,window_end;
void ws_metrics_add(const ws_frame_metrics_t *f) {
    int64_t now=esp_timer_get_time();
    if(!frames)window_start=now;
    window_end=now;
    frames++;bytes+=f->bytes;planned+=f->planned;presents+=f->presented;
    skipped+=f->skipped;coalesced+=f->coalesced;
    bool can=f->planned<=100000;
    possible+=can;
    if (!f->deadline_met) {if (can) misses++;else impossible_misses++;}
    for (int i=0;i<M_COUNT;i++) {
        if ((i==M_IRQ&&!f->irq_valid)||(i==M_POLL&&!f->poll_valid)) continue;
        if(i==M_CADENCE&&(!f->presented||!last_present))continue;
        uint32_t v=i==M_CADENCE?(uint32_t)(now-last_present):f->us[i],bin=(v+999)/1000;
        if(bin>251)bin=251;
        hist[i][bin]++;counts[i]++;sum[i]+=v;if(v>maximum[i])maximum[i]=v;
    }
    last_present=f->presented?now:0;
}
void ws_metrics_dump(unsigned scenario) {
    static const char *names[]={"turn","prepare","render","pack","spi_wait","total","irq_to_present","poll_to_present","present_interval"};
    printf("{\"type\":\"scenario\",\"scenario\":%u,\"elapsed_us\":%lld,\"frames\":%lu,\"presented\":%lu,"
           "\"bytes\":%llu,\"planned_bytes\":%llu,\"payload_possible\":%lu,\"possible_misses\":%lu,"
           "\"impossible_misses\":%lu,\"skipped\":%lu,\"coalesced\":%lu,\"timing\":{",
           scenario,window_end-window_start,(unsigned long)frames,(unsigned long)presents,bytes,planned,(unsigned long)possible,
           (unsigned long)misses,(unsigned long)impossible_misses,(unsigned long)skipped,(unsigned long)coalesced);
    for(int i=0;i<M_COUNT;i++) {
        uint32_t cumulative=0,p95=0;
        for(unsigned b=0;b<252;b++){cumulative+=hist[i][b];if(cumulative*100ULL>=counts[i]*95ULL){p95=b*1000;break;}}
        if(p95==251000)p95=maximum[i]; /* Overflow is bounded by observed max. */
        printf("%s\"%s\":{\"count\":%lu,\"sum_us\":%llu,\"p95_upper_us\":%lu,\"max_us\":%lu}",
               i?",":"",names[i],(unsigned long)counts[i],sum[i],(unsigned long)p95,(unsigned long)maximum[i]);
    }
    printf("}}\n");
    ws_metrics_reset();
}
void ws_metrics_reset(void) {
    memset(hist,0,sizeof(hist));memset(counts,0,sizeof(counts));memset(sum,0,sizeof(sum));memset(maximum,0,sizeof(maximum));
    frames=presents=possible=misses=impossible_misses=skipped=coalesced=0;bytes=planned=0;
    last_present=window_start=window_end=0;
}
void ws_memory_sample(unsigned scenario,uint32_t ticks,size_t guest_used) {
    /* Monotonico desde el boot y nunca reseteado: un hueco en seq es una linea
       que se comio el USB, no una muestra que no se tomo. Sin esto, el soak no
       puede distinguir "no hubo fuga" de "se perdio la evidencia". */
    static uint32_t seq;
    const uint32_t cap=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    printf("{\"type\":\"memory\",\"seq\":%lu,\"time_us\":%lld,\"scenario\":%u,\"ticks\":%lu,"
           "\"internal_free\":%u,\"internal_min\":%u,\"largest_internal_block\":%u,"
           "\"psram_free\":%u,\"guest_heap_used\":%u,\"owner_stack_free\":%u}\n",
           (unsigned long)seq++,esp_timer_get_time(),scenario,(unsigned long)ticks,(unsigned)heap_caps_get_free_size(cap),
           (unsigned)heap_caps_get_minimum_free_size(cap),(unsigned)heap_caps_get_largest_free_block(cap),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned)guest_used,
           (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
