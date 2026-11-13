#pragma once

enum { POND_RAIN_DROPS = 12, POND_RAIN_STEP_MS = 28, POND_RAIN_FALL_STEPS = 6,
       POND_RAIN_RING_STEPS = 15 };

typedef struct {
    int16_t x, y;
    uint8_t step;
} pond_drop_t;

static pond_drop_t pond_drops[POND_RAIN_DROPS];
static uint32_t pond_rain_started;
static uint64_t pond_rain_colors[WATER_PALETTE_SIZE];

static pond_drop_t pond_rain_drop(int index, uint32_t elapsed)
{
    static const uint16_t centers[POND_RAIN_DROPS][2] = {
        {53, 84}, {143, 77}, {239, 65}, {328, 95}, {86, 176}, {186, 157},
        {294, 192}, {64, 285}, {148, 314}, {226, 251}, {316, 309}, {200, 340}
    };
    const uint32_t period = 29 + (index * 11u % 17u);
    /* Spread phase changes across frames, while preserving each drop's speed. */
    const unsigned offset_ms = (index & 3u) * (POND_RAIN_STEP_MS / 4);
    const unsigned carry = elapsed % POND_RAIN_STEP_MS + offset_ms >= POND_RAIN_STEP_MS;
    const uint32_t tick = elapsed / POND_RAIN_STEP_MS + carry + index * 13u;
    const uint32_t cycle = tick / period;
    const unsigned step = tick % period;
    if (step >= POND_RAIN_FALL_STEPS + POND_RAIN_RING_STEPS) {
        return (pond_drop_t){0};
    }
    uint32_t offset = (cycle + 1) * (uint32_t)(index * 34 + 73);
    offset ^= offset << 13;
    offset ^= offset >> 17;
    offset ^= offset << 5;
    return (pond_drop_t){centers[index][0] + (int)(offset & 31) - 15,
                         centers[index][1] + (int)((offset >> 8) & 31) - 15,
                         step + 1};
}

static int pond_rain_ring_radius(int age)
{
    return 3 + age * 5 / 4;
}

static fish_box_t pond_rain_drop_box(pond_drop_t drop)
{
    if (!drop.step) { return (fish_box_t){0}; }
    const int x = drop.x;
    const int y = drop.y;
    if (drop.step <= POND_RAIN_FALL_STEPS) {
        const int falling = POND_RAIN_FALL_STEPS - drop.step;
        const int head_x = x - 2 * falling;
        const int head_y = y - 6 * falling;
        return box_clip((fish_box_t){head_x - 4, head_y - 9, head_x + 2, head_y + 1});
    }
    const int radius = pond_rain_ring_radius(drop.step - POND_RAIN_FALL_STEPS - 1);
    const int depth = (radius * 2 + 1) / 3;
    return box_clip((fish_box_t){x - radius, y - depth, x + radius + 1, y + depth + 1});
}

static inline uint16_t pond_rain_cool(uint16_t pixel)
{
    const unsigned r = ((pixel >> 11) * 25 + 16) >> 5;
    const unsigned g = (((pixel >> 5) & 63) * 27 + 16) >> 5;
    const unsigned b = (((pixel & 31) * 28 + 16) >> 5) + 2;
    return (r << 11) | (g << 5) | b;
}

static void pond_rain_reset(uint32_t elapsed)
{
    pond_rain_started = elapsed;
    for (unsigned i = 0; i < WATER_PALETTE_SIZE; ++i) {
        const uint64_t color = pond_rain_cool((uint16_t)water_colors[i]);
        pond_rain_colors[i] = color * UINT64_C(0x0001000100010001);
    }
    for (int i = 0; i < POND_RAIN_DROPS; ++i) {
        pond_drops[i] = pond_rain_drop(i, 0);
    }
}

static int pond_rain_update(uint32_t elapsed, fish_box_t *dirty, int capacity)
{
    int count = 0;
    for (int i = 0; i < POND_RAIN_DROPS && count < capacity; ++i) {
        const pond_drop_t old = pond_drops[i];
        const pond_drop_t next = pond_rain_drop(i, elapsed - pond_rain_started);
        if (old.step == next.step && old.x == next.x && old.y == next.y) { continue; }
        const fish_box_t before = pond_rain_drop_box(old);
        const fish_box_t after = pond_rain_drop_box(next);
        if (box_empty(before)) { dirty[count++] = after; }
        else if (box_empty(after)) { dirty[count++] = before; }
        else { dirty[count++] = box_union(before, after); }
        pond_drops[i] = next;
    }
    return count;
}

static inline uint16_t pond_rain_light(uint16_t pixel, int strength)
{
    const int r = pixel >> 11;
    const int g = (pixel >> 5) & 63;
    const int b = pixel & 31;
    return ((r + ((25 - r) * strength >> 4)) << 11) |
           ((g + ((58 - g) * strength >> 4)) << 5) |
            (b + ((31 - b) * strength >> 4));
}

static void pond_rain_paint_drop(fish_box_t tile, uint16_t *pixels, pond_drop_t drop)
{
    const fish_box_t part = box_intersection(tile, pond_rain_drop_box(drop));
    if (box_empty(part)) { return; }
    const int stride = tile.x1 - tile.x0;
    if (drop.step <= POND_RAIN_FALL_STEPS) {
        const int falling = POND_RAIN_FALL_STEPS - drop.step;
        const int head_x = drop.x - 2 * falling;
        const int head_y = drop.y - 6 * falling;
        for (int y = part.y0; y < part.y1; ++y) {
            const int tail = head_y - y;
            const int x = head_x - tail / 3;
            if (x >= part.x0 && x < part.x1) {
                uint16_t *pixel = pixels + (y - tile.y0) * stride + x - tile.x0;
                *pixel = pond_rain_light(*pixel, 10 - tail / 2);
            }
            if (tail < 5 && x - 1 >= part.x0 && x - 1 < part.x1) {
                uint16_t *pixel = pixels + (y - tile.y0) * stride + x - 1 - tile.x0;
                *pixel = pond_rain_light(*pixel, 4);
            }
        }
        return;
    }

    const int age = drop.step - POND_RAIN_FALL_STEPS - 1;
    const int radius = pond_rain_ring_radius(age);
    const int outer = radius * radius;
    const int inner = (radius - 2) * (radius - 2);
    const int echo = radius - 5;
    const int echo_outer = echo * echo;
    const int echo_inner = (echo - 1) * (echo - 1);
    const int strength = 11 - age * 2 / 3;
    for (int y = part.y0; y < part.y1; ++y) {
        const int dy = (y - drop.y) * 3;
        const int yy = dy * dy / 4;
        uint16_t *row = pixels + (y - tile.y0) * stride;
        for (int x = part.x0; x < part.x1; ++x) {
            const int dx = x - drop.x;
            const int distance = dx * dx + yy;
            const int light = distance >= inner && distance <= outer ? strength
                : echo >= 4 && distance >= echo_inner && distance <= echo_outer ? strength / 2 : 0;
            if (light) { row[x - tile.x0] = pond_rain_light(row[x - tile.x0], light); }
        }
    }
}

static void pond_rain_paint(fish_box_t tile, uint16_t *pixels)
{
    water_paint_base(tile, pixels, pond_rain_colors);
    for (int i = 0; i < POND_RAIN_DROPS; ++i) {
        if (pond_drops[i].step) { pond_rain_paint_drop(tile, pixels, pond_drops[i]); }
    }
}
