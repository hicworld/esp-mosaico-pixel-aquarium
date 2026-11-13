#include "pond_display.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "bsp/esp_mosaico.h"
#include "esp_heap_caps.h"
#include "esp_lv_adapter_koi.h"
#include "lvgl.h"

_Static_assert(POND_DISPLAY_WIDTH == BSP_LCD_H_RES && POND_DISPLAY_HEIGHT == BSP_LCD_V_RES,
               "pond canvas must fill the display");

enum {
    TOUCH_Y_SHIFT = 9,
    TOUCH_COORD_MASK = (1u << TOUCH_Y_SHIFT) - 1u,
    TOUCH_PRESSED = 1u << (2 * TOUCH_Y_SHIFT),
    REFRESH_MS = 16,
};

static lv_obj_t *s_canvas;
static lv_draw_buf_t *s_canvas_buffer;
static lv_timer_t *s_refresh_timer;
static lv_indev_read_cb_t s_read_touch;
static atomic_uint s_touch;
static bool s_batch_open;
static bool s_batch_changed;

static void read_touch(lv_indev_t *indev, lv_indev_data_t *data)
{
    s_read_touch(indev, data);
    unsigned sample = atomic_load_explicit(&s_touch, memory_order_relaxed);
    if (data->state == LV_INDEV_STATE_PRESSED) {
        unsigned x = (unsigned)data->point.x & TOUCH_COORD_MASK;
        unsigned y = (unsigned)data->point.y & TOUCH_COORD_MASK;
        sample = TOUCH_PRESSED | x | (y << TOUCH_Y_SHIFT);
    } else {
        sample &= ~TOUCH_PRESSED;
    }
    atomic_store_explicit(&s_touch, sample, memory_order_relaxed);
}

esp_err_t pond_display_start(void)
{
    esp_err_t err = bsp_power_init();
    if (err != ESP_OK) return err;

    lv_display_t *display = bsp_display_start();
    if (!display) return ESP_FAIL;
    err = bsp_display_brightness_set(35);
    if (err != ESP_OK) return err;

    if (!bsp_display_lock(-1)) return ESP_ERR_INVALID_STATE;
    lv_indev_t *touch = bsp_display_get_input_dev();
    s_read_touch = touch ? lv_indev_get_read_cb(touch) : NULL;
    s_refresh_timer = lv_display_get_refr_timer(display);
    if (!s_read_touch || !s_refresh_timer) {
        bsp_display_unlock();
        return ESP_ERR_NOT_FOUND;
    }

    size_t stride = lv_draw_buf_width_to_stride(POND_DISPLAY_WIDTH, LV_COLOR_FORMAT_RGB565);
    size_t size = stride * POND_DISPLAY_HEIGHT;
    void *pixels = heap_caps_aligned_alloc(128, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        bsp_display_unlock();
        return ESP_ERR_NO_MEM;
    }
    memset(pixels, 0, size);

    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_remove_style_all(screen);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    s_canvas = lv_canvas_create(screen);
    lv_obj_remove_style_all(s_canvas);
    lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_canvas_set_buffer(s_canvas, pixels, POND_DISPLAY_WIDTH, POND_DISPLAY_HEIGHT, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_update_layout(s_canvas);
    s_canvas_buffer = lv_canvas_get_draw_buf(s_canvas);
    lv_draw_buf_flush_cache(s_canvas_buffer, NULL);
    lv_indev_set_read_cb(touch, read_touch);

    lv_display_set_render_mode(display, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_timer_set_period(s_refresh_timer, REFRESH_MS);
    koi_panel_set_mode(true);
    lv_timer_ready(s_refresh_timer);
    koi_panel_wake();
    bsp_display_unlock();
    return ESP_OK;
}

bool pond_display_begin(void)
{
    if (!s_canvas_buffer || s_batch_open || !bsp_display_lock(-1)) return false;
    s_batch_open = true;
    s_batch_changed = false;
    return true;
}

int pond_display_patch(int x, int y, int width, int height, const uint16_t *pixels)
{
    if (!s_batch_open || !pixels || x < 0 || y < 0 || width <= 0 || height <= 0 ||
        width > POND_LOGICAL_SIZE - x || height > POND_LOGICAL_SIZE - y) return -1;

    const size_t stride = s_canvas_buffer->header.stride / sizeof(*pixels);
    pond_blit_scaled((uint16_t *)s_canvas_buffer->data, stride, x, y, width, height, pixels);

    lv_area_t area = {
        pond_panel_edge(x), pond_panel_edge(y),
        pond_panel_edge(x + width) - 1, pond_panel_edge(y + height) - 1,
    };
    lv_draw_buf_flush_cache(s_canvas_buffer, &area);
    lv_obj_invalidate_area(s_canvas, &area);
    s_batch_changed = true;
    return 0;
}

void pond_display_end(void)
{
    if (!s_batch_open) return;
    if (s_batch_changed) {
        lv_timer_ready(s_refresh_timer);
        koi_panel_wake();
    }
    s_batch_open = false;
    bsp_display_unlock();
}

bool pond_display_touch(int *x, int *y)
{
    unsigned sample = atomic_load_explicit(&s_touch, memory_order_relaxed);
    if (x) *x = (int)(sample & TOUCH_COORD_MASK);
    if (y) *y = (int)((sample >> TOUCH_Y_SHIFT) & TOUCH_COORD_MASK);
    return (sample & TOUCH_PRESSED) != 0;
}

unsigned pond_display_frames(void)
{
    koi_panel_stats_t stats;
    koi_panel_snapshot(&stats);
    return stats.frames;
}

void pond_display_status(void)
{
    koi_panel_stats_t stats;
    koi_panel_snapshot(&stats);
    printf("POND_LCD flush_fps=%u frames=%u areas=%u shared=%u extra_waits=%u timeouts=%u"
           " te_us=%lu wait_us=%lu dma_us=%lu gap_us=%lu max_gap_us=%lu pixels=%lu\n",
           stats.fps, stats.frames, stats.areas, stats.shared, stats.extra_waits, stats.timeouts,
           (unsigned long)stats.te_period_us, (unsigned long)stats.wait_us,
           (unsigned long)stats.dma_us, (unsigned long)stats.frame_gap_us,
           (unsigned long)stats.max_frame_gap_us, (unsigned long)stats.frame_pixels);
}
