#include "platform.h"
#include "fish_art.h"
#include "pond_audio.h"

enum {
    MAX_COMMANDS = 8,
    EVENT_TIMER = 1,
    EVENT_TAP = 2,
    EVENT_LONG_PRESS = 7,
    EVENT_PREPARE = 9,
    EVENT_DRAIN = 10,
    FAST_REFRESH = 60,
    REPLAY_TAP_MS = 350,
    REPLAY_TAP_RADIUS = 36,
    MAX_CATCH_UP_STEPS = 3,
    CONTROL_CORNER = 48,
    BACKDROP_TILES = 9,
};

static scene_cmd_t commands[MAX_COMMANDS];
static int command_count;
static uint32_t reveal_started;
static uint32_t previous_frame;
static unsigned orientation;
static int aligning;
static pond_t pond, previous_pond, fast_view;
static uint32_t prepared_at;
static int fast_prepared;
static uint32_t last_tap_ms;
static int last_tap_x, last_tap_y;
static int has_last_tap;
static int feed_needs_frame, frame_deferred;
static int canvas_ready;
static int water_mode, backdrop_tile;

static void add_rect(int x, int y, int width, int height, uint32_t rgb)
{
    if (orientation & FISH_SWAP_AXES) {
        int old_x = x, old_width = width;
        x = y;
        y = old_x;
        width = height;
        height = old_width;
    }
    if (orientation & FISH_FLIP_X) { x = FISH_GRID - x - width; }
    if (orientation & FISH_FLIP_Y) { y = FISH_GRID - y - height; }
    scene_cmd_t *command = &commands[command_count++];
    command->kind = SCENE_RECT;
    command->rgb = rgb;
    command->x = ORIGIN_X + x;
    command->y = ORIGIN_Y + y;
    command->w = width;
    command->h = height;
}

static int present_scene(void)
{
    // The platform requires at least one scene command, including for black frames.
    if (command_count == 0) { add_rect(0, 0, 1, 1, 0); }
    const int ready = platform_scene(commands, command_count, 0) == 0;
    command_count = 0;
    return ready;
}

static void draw_corner_icon(int sound)
{
    enum { SIDE = 20 };
    const uint16_t light = pond_audio_enabled() || !sound ? 0x84f3 : 0x426a;
    for (int y = 0; y < SIDE; ++y) {
        for (int x = 0; x < SIDE; ++x) {
            int lit;
            if (sound) {
                lit = (x >= 10 && x <= 11 && y >= 4 && y <= 14)
                    || (x >= 10 && x <= 15 && y >= 4 && y <= 6)
                    || (x >= 5 && x <= 11 && y >= 12 && y <= 15);
                if (!pond_audio_enabled() && x + y == 18 && x >= 3 && x <= 16) { lit = 1; }
            } else {
                const int wave = ((x + 1) & 7) < 4 ? 0 : 1;
                lit = x >= 3 && x <= 16 &&
                    (y == 6 + wave || y == 10 + wave || y == 14 + wave);
            }
            tile_pixels[y * SIDE + x] = lit ? light : 0;
        }
    }
    (void)platform_image(sound ? 14 : 446, 14, SIDE, SIDE, tile_pixels);
}

static void create_canvas(void)
{
    if (water_mode) { add_rect(0, 0, FISH_GRID, FISH_GRID, 0x082326); }
    canvas_ready = present_scene();
    backdrop_tile = 0;
    if (canvas_ready && water_mode) {
        draw_corner_icon(1);
        draw_corner_icon(0);
    }
}

static int backdrop_pending(void)
{
    return water_mode && backdrop_tile < BACKDROP_TILES;
}

static void paint_backdrop(void)
{
    if (!canvas_ready) { create_canvas(); }
    if (canvas_ready && draw_backdrop_tile(backdrop_tile)) {
        ++backdrop_tile;
    }
}

static void show_pond(const pond_t *view, uint32_t elapsed)
{
    if (!canvas_ready) {
        // Recover if the host was unable to allocate the canvas or write a patch.
        create_canvas();
        pond_forget_frame();
        return;
    }
    const uint32_t age = elapsed - view->feed_start;
    if (view->feeding && age < FEED_MS) {
        static const int8_t offsets[][2] = {{-8, -1}, {6, -7}, {-2, 10}, {10, 6}};
        const int count = age < 1600 ? 4 : age < 2800 ? 3 : 1;
        for (int i = 0; i < count; ++i) {
            const int size = 3 - (i & 1);
            add_rect(view->feed_x + offsets[i][0], view->feed_y + offsets[i][1],
                     size, size, 0xc58b43);
        }
    }
    canvas_ready = pond_draw_koi(view, elapsed, orientation, commands, command_count, water_mode);
    command_count = 0;
}

static void start_reveal(uint32_t now)
{
    reveal_started = now;
    previous_frame = UINT32_MAX;
    has_last_tap = 0;
    feed_needs_frame = frame_deferred = 0;
    pond_reset(&pond);
    previous_pond = pond;
    fast_prepared = 0;
    pond_reset_art();
    create_canvas();
}

static void show_alignment(void)
{
    const uint32_t amber = 0xffb94d, white = 0xfff8de;
    add_rect(96, 112, 24, 160, amber);
    add_rect(120, 112, 72, 24, amber);
    add_rect(120, 184, 56, 24, amber);
    add_rect(256, 112, 24, 160, white);
    add_rect(232, 144, 24, 24, white);
    add_rect(280, 144, 24, 24, white);
    add_rect(208, 168, 24, 24, white);
    add_rect(304, 168, 24, 24, white);
    (void)present_scene();
}

static void handle_tap(int x, int y)
{
    if (aligning) {
        orientation = (orientation + 1) % FISH_ORIENTATIONS;
        show_alignment();
        return;
    }

    if (y < CONTROL_CORNER && x < CONTROL_CORNER) {
        pond_audio_toggle();
        has_last_tap = 0;
        if (water_mode && canvas_ready) { draw_corner_icon(1); }
        return;
    }
    if (y < CONTROL_CORNER && x >= 480 - CONTROL_CORNER) {
        water_mode = !water_mode;
        const uint8_t saved = (uint8_t)water_mode;
        (void)platform_kv_set("pond", 4, &saved, sizeof(saved));
        start_reveal(platform_millis());
        return;
    }

    uint32_t now = platform_millis();
    uint32_t elapsed = now - reveal_started;
    int dx = x - last_tap_x, dy = y - last_tap_y;
    int double_tap = has_last_tap && now - last_tap_ms <= REPLAY_TAP_MS &&
                     dx * dx + dy * dy <= REPLAY_TAP_RADIUS * REPLAY_TAP_RADIUS;
    if (elapsed < FISH_DELAY_MS || double_tap) {
        start_reveal(now);
        return;
    }
    has_last_tap = 1;
    last_tap_ms = now;
    last_tap_x = x;
    last_tap_y = y;

    x = fish_clamp(x - ORIGIN_X, 0, FISH_GRID - 1);
    y = fish_clamp(y - ORIGIN_Y, 0, FISH_GRID - 1);
    if (orientation & FISH_FLIP_X) { x = FISH_GRID - 1 - x; }
    if (orientation & FISH_FLIP_Y) { y = FISH_GRID - 1 - y; }
    if (orientation & FISH_SWAP_AXES) {
        int previous_x = x;
        x = y;
        y = previous_x;
    }
    elapsed -= FISH_DELAY_MS;
    pond_feed(&pond, elapsed, x, y);
    feed_needs_frame = 1;
}

void app_init(void)
{
    uint8_t saved = 0;
    if (platform_kv_get("orient", 6, &saved, sizeof(saved)) == sizeof(saved) && saved < FISH_ORIENTATIONS) {
        orientation = saved;
    } else {
        // USB-C points toward the right end of the drawer. The reflector swaps
        // the screen axes as seen from the front of the assembled aquarium.
        orientation = FISH_SWAP_AXES;
    }
    saved = 1;
    water_mode = platform_kv_get("pond", 4, &saved, sizeof(saved)) == sizeof(saved) ? saved != 0 : 1;
    aligning = 0;
    pond_audio_init();
    start_reveal(platform_millis());
}

static int advance_pond(uint32_t elapsed)
{
    const uint32_t frame = elapsed / FISH_FRAME_MS;
    if (frame == previous_frame) { return 0; }
    uint32_t steps = previous_frame == UINT32_MAX ? 1 : frame - previous_frame;
    if (steps > MAX_CATCH_UP_STEPS) { steps = MAX_CATCH_UP_STEPS; }
    for (uint32_t left = steps; left > 0; --left) {
        previous_pond = pond;
        pond_step(&pond, (frame - left + 1) * FISH_FRAME_MS);
    }
    previous_frame = frame;
    return 1;
}

static void prepare_fast_frame(uint32_t now)
{
    const uint32_t elapsed = now - reveal_started;
    fast_prepared = 0;
    if (elapsed < FISH_DELAY_MS || backdrop_pending()) { return; }

    const uint32_t age = elapsed - FISH_DELAY_MS;
    (void)advance_pond(age);
    if (canvas_ready && pond_frame_pending()) { return; }
    pond_interpolate(&fast_view, &previous_pond, &pond, age % FISH_FRAME_MS);
    (void)refresh_poses(&fast_view, age, orientation);
    prepared_at = now;
    fast_prepared = 1;
}

static void present_fast_frame(uint32_t now)
{
    if (canvas_ready && pond_frame_pending()) {
        canvas_ready = pond_continue_frame();
        return;
    }
    if (!fast_prepared || prepared_at != now) { return; }
    fast_prepared = 0;
    feed_needs_frame = frame_deferred = 0;
    show_pond(&fast_view, now - reveal_started - FISH_DELAY_MS);
}

void app_event(int type, int a, int b)
{
    if (type == EVENT_LONG_PRESS) {
        aligning = !aligning;
        has_last_tap = 0;
        if (aligning) {
            show_alignment();
        } else {
            uint8_t saved = (uint8_t)orientation;
            (void)platform_kv_set("orient", 6, &saved, sizeof(saved));
            start_reveal(platform_millis());
        }
        return;
    }
    if (type == EVENT_TAP) {
        handle_tap(a, b);
        return;
    }
    if (aligning) { return; }
    if (type == EVENT_TIMER && backdrop_pending()) {
        paint_backdrop();
        return;
    }
    const uint32_t now = (uint32_t)a;
    if (b == FAST_REFRESH) {
        // New firmware separates pose expansion, presentation and patch draining.
        if (type == EVENT_PREPARE) { prepare_fast_frame(now); }
        else if (type == EVENT_TIMER) { present_fast_frame(now); }
        else if (type == EVENT_DRAIN && canvas_ready && pond_frame_pending()) {
            canvas_ready = pond_continue_frame();
        }
        return;
    }
    if (type != EVENT_TIMER) { return; }

    uint32_t elapsed = now - reveal_started;
    if (elapsed < FISH_DELAY_MS) { return; }
    elapsed -= FISH_DELAY_MS;
    if (!advance_pond(elapsed)) { return; }
    if (canvas_ready && pond_frame_pending()) {
        canvas_ready = pond_continue_frame();
        return;
    }
    // On older hosts, pose expansion and composition share the same callback.
    if (!feed_needs_frame && !frame_deferred && refresh_poses(&pond, elapsed, orientation)) {
        frame_deferred = 1;
        return;
    }
    feed_needs_frame = frame_deferred = 0;
    show_pond(&pond, elapsed);
}
