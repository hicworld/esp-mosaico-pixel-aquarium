/* Offscreen host for exercising the native scene without ESP-IDF. */
#include <stdbool.h>
#include <stdint.h>

#include "pond_scene.c"

const uint8_t _binary_koi_poses_bin_start[] = {
#embed "../pixel-fish/koi_poses.bin"
};

static uint16_t canvas[POND_DISPLAY_WIDTH * POND_DISPLAY_HEIGHT];
static uint16_t reference[POND_DISPLAY_WIDTH * POND_DISPLAY_HEIGHT];
static bool drawing_reference;
static bool audio_rain;
static int preview_volume = 70;

uint16_t *pond_preview_pixels(void) { return canvas; }
int pond_preview_audio(void) { return audio_rain; }

int pond_preview_pellets(unsigned state)
{
    int count = 0;
    for (unsigned i = 0; i < SCHOOL_MAX_PELLETS; ++i) {
        count += pond.pellets[i].state == state;
    }
    return count;
}

bool pond_preview_damage_matches(void)
{
    drawing_reference = true;
    for (int y = 0; y < FISH_GRID; y += TILE_SIZE) {
        for (int x = 0; x < FISH_GRID; x += TILE_SIZE) {
            draw_dirty_region((fish_box_t){x, y, x + TILE_SIZE, y + TILE_SIZE}, &pending_frame);
        }
    }
    drawing_reference = false;
    for (int i = 0; i < POND_DISPLAY_WIDTH * POND_DISPLAY_HEIGHT; ++i) {
        if (canvas[i] != reference[i]) { return false; }
    }
    return true;
}
void pond_audio_set_rain(bool rain) { audio_rain = rain; }
void pond_audio_adjust_volume(int delta) { preview_volume = fish_clamp(preview_volume + delta, 0, 100); }
unsigned pond_audio_volume(void) { return (unsigned)preview_volume; }
bool pond_display_begin(void) { return true; }
void pond_display_end(void) {}

int pond_display_patch(int x, int y, int width, int height, const uint16_t *pixels)
{
    if (x < 0 || y < 0 || x + width > FISH_GRID || y + height > FISH_GRID) { return -1; }
    uint16_t *target = drawing_reference ? reference : canvas;
    pond_blit_scaled(target, POND_DISPLAY_WIDTH, x, y, width, height, pixels);
    return 0;
}
