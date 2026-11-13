#include "pond_scene.h"
#include "pond_audio.h"
#include "pond_display.h"
#include <stdatomic.h>

typedef struct {
    int x, y, w, h;
    uint32_t rgb;
    unsigned slot;
} scene_cmd_t;

static int platform_image(int x, int y, int w, int h, const uint16_t *pixels)
{
    return pond_display_patch(x, y, w, h, pixels);
}

#include "fish_art.h"
#include "pond_rain.h"

_Static_assert((int)POND_LOGICAL_SIZE == (int)FISH_GRID, "logical pond and renderer must agree");

enum { TAP_MS = 350, TAP_RADIUS = 20, SWIPE_DISTANCE = 42, VOLUME_STEP = 10,
       CATCH_UP_STEPS = 3 };

static school_t pond, previous, view;
static uint32_t started, last_step;
static const unsigned orientation = FISH_SWAP_AXES;
static bool initialized, rainy;
static atomic_bool requested_rain, shown_rain;
static atomic_uint visible_fish;

static struct {
    uint32_t start;
    int x, y;
    bool down, moved;
} touch;

void pond_scene_request_weather(bool rain)
{
    atomic_store(&requested_rain, rain);
}

bool pond_scene_is_rain(void)
{
    return atomic_load(&shown_rain);
}

unsigned pond_scene_visible_fish(void)
{
    return atomic_load(&visible_fish);
}

static void native_paint_water(fish_box_t tile, uint16_t *pixels, uint32_t phase)
{
    if (rainy) { pond_rain_paint(tile, pixels); }
    else { water_paint_tile(tile, pixels, phase); }
}

static void native_weather_damage(pond_frame_t *frame, uint32_t elapsed)
{
    if (rainy) {
        fish_box_t weather[POND_RAIN_DROPS];
        const int count = pond_rain_update(elapsed, weather, POND_RAIN_DROPS);
        for (int i = 0; i < count; ++i) {
            add_dirty_region(frame->regions, &frame->region_count, weather[i]);
        }
    } else if (elapsed - previous_water_update >= WATER_STEP_MS) {
        ++frame->water_phase;
        frame->water_update = elapsed;
        add_dirty_region(frame->regions, &frame->region_count, water_glint_box(frame->water_phase));
    }
}

static void advance(uint32_t elapsed)
{
    const uint32_t step = elapsed / FISH_FRAME_MS;
    uint32_t steps = step - last_step;
    if (steps > CATCH_UP_STEPS) { steps = CATCH_UP_STEPS; }
    for (uint32_t left = steps; left; --left) {
        previous = pond;
        school_step(&pond, (step - left + 1) * FISH_FRAME_MS);
    }
    last_step = step;
    school_interpolate(&view, &previous, &pond, elapsed % FISH_FRAME_MS);
    refresh_poses(&view, elapsed, orientation);
}

static int feed_dots(scene_cmd_t dots[MAX_FOOD_DOTS])
{
    int count = 0;
    for (unsigned slot = 0; slot < SCHOOL_MAX_PELLETS; ++slot) {
        const pond_pellet_t *pellet = &view.pellets[slot];
        if (pellet->state == PELLET_EMPTY) { continue; }
        int x = pellet->x, y = pellet->y;
        orient_point(orientation, &x, &y);
        const int size = pellet->state == PELLET_EATING ? 2 : 4;
        const uint32_t color = pellet->state == PELLET_EATING ? 0xb38649 : 0xdfad67;
        dots[count++] = (scene_cmd_t){ORIGIN_X + x - size / 2, ORIGIN_Y + y - size / 2,
                                      size, size, color, slot};
    }
    return count;
}

bool pond_scene_frame(uint32_t now)
{
    const bool wanted = atomic_load(&requested_rain);
    const bool rebuild = !initialized || wanted != rainy;
    if (!initialized) {
        started = now;
        school_reset(&pond, now);
        previous = pond;
        pond_reset_art();
    }
    const uint32_t elapsed = now - started;
    if (rebuild || !pond_frame_pending()) { advance(elapsed); }
    if (!pond_display_begin()) { return false; }
    bool ok = true;
    if (rebuild) {
        rainy = wanted;
        pond_forget_frame();
        if (rainy) { pond_rain_reset(elapsed); }
        for (int i = 0; i < 9; ++i) {
            if (!draw_backdrop_tile(i)) { ok = false; break; }
        }
        if (ok) {
            initialized = true;
            atomic_store(&shown_rain, rainy);
            pond_audio_set_rain(rainy);
        }
    }
    if (ok) {
        if (pond_frame_pending()) {
            ok = pond_continue_frame();
        } else {
            scene_cmd_t dots[MAX_FOOD_DOTS];
            const int count = feed_dots(dots);
            ok = pond_draw_koi(&view, elapsed, orientation, dots, count, 1);
            if (ok) { atomic_store(&visible_fish, (unsigned)pending_frame.fish_count); }
        }
    }
    if (!ok) { initialized = false; last_step = 0; }
    pond_display_end();
    return ok;
}

void pond_scene_touch(bool down, int x, int y, uint32_t now)
{
    if (!touch.down) {
        if (down) {
            touch.start = now;
            touch.x = x;
            touch.y = y;
            touch.down = true;
            touch.moved = false;
        }
        return;
    }

    // The touch reader retains the last pressed coordinates on release.
    const int dx = x - touch.x, dy = y - touch.y;
    if (dx * dx + dy * dy > TAP_RADIUS * TAP_RADIUS) { touch.moved = true; }
    if (down) { return; }

    touch.down = false;
    if (!initialized) { return; }

    const int horizontal = fish_abs(dx), vertical = fish_abs(dy);
    if (horizontal >= SWIPE_DISTANCE && horizontal * 2 >= vertical * 3) {
        pond_scene_request_weather(!atomic_load(&requested_rain));
        return;
    }
    if (vertical >= SWIPE_DISTANCE && vertical * 2 >= horizontal * 3) {
        pond_audio_adjust_volume(dy < 0 ? VOLUME_STEP : -VOLUME_STEP);
        return;
    }
    if (touch.moved || now - touch.start > TAP_MS) { return; }

    if (touch.x < 0 || touch.x >= POND_DISPLAY_WIDTH ||
        touch.y < 0 || touch.y >= POND_DISPLAY_HEIGHT) { return; }
    int px = pond_logical_pixel(touch.x), py = pond_logical_pixel(touch.y);
    if (orientation & FISH_FLIP_X) { px = FISH_GRID - 1 - px; }
    if (orientation & FISH_FLIP_Y) { py = FISH_GRID - 1 - py; }
    if (orientation & FISH_SWAP_AXES) {
        const int old_x = px;
        px = py;
        py = old_x;
    }
    school_feed(&pond, px, py);
}
