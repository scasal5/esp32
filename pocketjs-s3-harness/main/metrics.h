#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
enum {M_TURN,M_PREPARE,M_RENDER,M_PACK,M_SPI,M_TOTAL,M_IRQ,M_POLL,M_COUNT};
typedef struct {uint32_t us[M_COUNT],bytes,planned,regions,skipped,coalesced;
    bool presented,deadline_met,irq_valid,poll_valid;} ws_frame_metrics_t;
void ws_metrics_add(const ws_frame_metrics_t *frame);
void ws_metrics_dump(unsigned scenario);
void ws_memory_sample(unsigned scenario,uint32_t ticks,size_t guest_used);
