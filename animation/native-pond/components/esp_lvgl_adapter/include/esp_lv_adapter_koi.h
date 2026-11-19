#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    unsigned fps, frames, areas, shared, extra_waits, timeouts;
    uint32_t te_period_us, wait_us, dma_us;
    uint32_t frame_gap_us, max_frame_gap_us;
    uint32_t frame_pixels;
} koi_panel_stats_t;

/* Mode and wake operations are called under the adapter display mutex. */
void koi_panel_set_mode(bool enabled);
bool koi_panel_mode(void);
void koi_panel_wake(void);
void koi_panel_snapshot(koi_panel_stats_t *out);
void koi_panel_record(bool last, bool shared, bool extra_wait, bool timeout,
                      uint32_t period, uint32_t wait, uint32_t dma, uint32_t pixels);
