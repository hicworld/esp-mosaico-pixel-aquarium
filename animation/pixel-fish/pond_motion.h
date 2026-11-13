#pragma once

#include <stdint.h>

enum { FISH_GRID = 384, FISH_DELAY_MS = 5000, FISH_FRAME_MS = 50 };
enum { KOI_COUNT = 3, SUBPIXEL = 256, FEED_MS = 4200 };

typedef struct {
    int x, y;
    unsigned heading, tail_phase;
    int bend, speed;
    unsigned next_stop, feed_slot;
    int at_food;
} koi_t;

typedef struct {
    koi_t fish[KOI_COUNT];
    uint32_t feed_start;
    int feed_x, feed_y, feeding;
} pond_t;

static const unsigned spawn_ms[KOI_COUNT] = {0, 1150, 2100};

static inline int fish_clamp(int value, int low, int high)
{
    if (value < low) { return low; }
    if (value > high) { return high; }
    return value;
}

static inline int fish_abs(int value)
{
    return value < 0 ? -value : value;
}

static const uint16_t fish_quarter_wave[] = {
      0,   6,  13,  19,  25,  31,  38,  44,  50,  56,  62,  68,  74,
     80,  86,  92,  98, 104, 109, 115, 121, 126, 132, 137, 142, 147,
    152, 157, 162, 167, 172, 177, 181, 185, 190, 194, 198, 202, 206,
    209, 213, 216, 220, 223, 226, 229, 231, 234, 237, 239, 241, 243,
    245, 247, 248, 250, 251, 252, 253, 254, 255, 255, 256, 256, 256,
};

static inline int fish_sin(unsigned angle)
{
    angle &= 255;
    const unsigned offset = angle & 63;
    const int amplitude = fish_quarter_wave[(angle & 64) ? 64 - offset : offset];
    return angle & 128 ? -amplitude : amplitude;
}

static inline int fish_cos(unsigned angle)
{
    return fish_sin(angle + 64);
}

/* The angle is measured clockwise from right, in 256 steps per turn. */
static inline unsigned fish_direction(int dx, int dy)
{
    const int x = fish_abs(dx), y = fish_abs(dy);
    unsigned low = 0, high = 64;
    while (low < high) {
        const unsigned mid = (low + high) / 2;
        if (y * fish_quarter_wave[64 - mid] > x * fish_quarter_wave[mid]) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    if (low != 0 &&
        fish_abs(y * fish_quarter_wave[65 - low] - x * fish_quarter_wave[low - 1]) <
        fish_abs(y * fish_quarter_wave[64 - low] - x * fish_quarter_wave[low])) {
        --low;
    }
    const unsigned angle = dx < 0 ? 128 - low : low;
    return (dy < 0 ? 0u - angle : angle) & 255;
}

static inline int fish_heading_error(unsigned heading, unsigned target)
{
    return (int)((target - heading + 128u) & 255) - 128;
}

static inline unsigned fish_interpolate_angle(unsigned before, unsigned after, int fraction_ms)
{
    const int advance = fish_heading_error(before, after) * fraction_ms / FISH_FRAME_MS;
    return (before + (unsigned)advance) & 255;
}

/* Sample two simulation ticks for drawing; the next tick still uses after. */
static inline void pond_interpolate(pond_t *output, const pond_t *before,
                                    const pond_t *after, unsigned fraction_ms)
{
    const int fraction = (int)fraction_ms;
    for (unsigned i = 0; i < KOI_COUNT; ++i) {
        const koi_t *first = &before->fish[i], *last = &after->fish[i];
        koi_t sample = *last;
        sample.x = first->x + (last->x - first->x) * fraction / FISH_FRAME_MS;
        sample.y = first->y + (last->y - first->y) * fraction / FISH_FRAME_MS;
        sample.heading = fish_interpolate_angle(first->heading, last->heading, fraction);
        sample.tail_phase = fish_interpolate_angle(first->tail_phase, last->tail_phase, fraction);
        sample.bend = first->bend + (last->bend - first->bend) * fraction / FISH_FRAME_MS;
        output->fish[i] = sample;
    }
    output->feed_start = after->feed_start;
    output->feed_x = after->feed_x;
    output->feed_y = after->feed_y;
    output->feeding = after->feeding;
}

static inline int fish_distance(int dx, int dy)
{
    const int x = fish_abs(dx), y = fish_abs(dy);
    return x >= y ? x + 3 * y / 8 : y + 3 * x / 8;
}

static inline void pond_reset(pond_t *pond)
{
    pond->fish[0] = (koi_t){ .x = -18 * SUBPIXEL, .y = 110 * SUBPIXEL,
                            .heading = 250, .speed = 570 };
    pond->fish[1] = (koi_t){ .x = 402 * SUBPIXEL, .y = 236 * SUBPIXEL,
                            .heading = 128, .tail_phase = 78, .speed = 520 };
    pond->fish[2] = (koi_t){ .x = -22 * SUBPIXEL, .y = 303 * SUBPIXEL,
                            .heading = 250, .tail_phase = 150, .speed = 630 };
    pond->feed_start = 0;
    pond->feeding = 0;
}

static inline void pond_feed(pond_t *pond, uint32_t elapsed, int x, int y)
{
    static const uint8_t slot_angles[KOI_COUNT] = {0, 85, 171};
    static const uint8_t slot_order[6][KOI_COUNT] = {
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2},
        {1, 2, 0}, {2, 0, 1}, {2, 1, 0},
    };
    pond->feed_x = fish_clamp(x, 88, 295);
    pond->feed_y = fish_clamp(y, 88, 295);
    pond->feed_start = elapsed;
    pond->feeding = 1;

    /* Arrange the fish around the food with short approaches and room for their tails. */
    int best_cost = INT32_MAX;
    unsigned best_rotation = 0, best_order = 0;
    for (unsigned rotation = 0; rotation < 84; rotation += 21) {
        int slot_cost[KOI_COUNT][KOI_COUNT];
        for (unsigned j = 0; j < KOI_COUNT; ++j) {
            const unsigned slot = rotation + slot_angles[j];
            const int goal_x = pond->feed_x * SUBPIXEL + 43 * fish_cos(slot);
            const int goal_y = pond->feed_y * SUBPIXEL + 43 * fish_sin(slot);
            for (unsigned i = 0; i < KOI_COUNT; ++i) {
                const koi_t *fish = &pond->fish[i];
                const int dx = (goal_x - fish->x) / SUBPIXEL;
                const int dy = (goal_y - fish->y) / SUBPIXEL;
                const unsigned approach = fish_direction(dx, dy);
                const int first_turn = fish_heading_error(fish->heading, approach);
                const int last_turn = fish_heading_error(approach, slot + 128);
                slot_cost[i][j] = dx * dx + dy * dy + first_turn * first_turn + last_turn * last_turn / 2;
            }
        }
        for (unsigned order = 0; order < 6; ++order) {
            const uint8_t *indices = slot_order[order];
            const int cost = slot_cost[0][indices[0]] + slot_cost[1][indices[1]] + slot_cost[2][indices[2]];
            if (cost < best_cost) {
                best_cost = cost;
                best_rotation = rotation;
                best_order = order;
            }
        }
    }
    for (unsigned i = 0; i < KOI_COUNT; ++i) {
        pond->fish[i].feed_slot = best_rotation + slot_angles[slot_order[best_order][i]];
        pond->fish[i].at_food = 0;
    }
}

static inline void koi_turn(koi_t *fish, int error, int limit)
{
    int turn = fish_clamp(error / 3, -limit, limit);
    if (turn == 0 && error != 0) { turn = error < 0 ? -1 : 1; }

    const int acceleration = limit > 7 ? 3 : 2;
    fish->bend += fish_clamp(turn - fish->bend, -acceleration, acceleration);
    if (fish_abs(error) < fish_abs(fish->bend) && error * fish->bend >= 0) {
        fish->bend = error;
    }
    fish->heading = (fish->heading + (unsigned)fish->bend) & 255;
}

static inline void koi_swim(koi_t *fish, int error, int speed, int turning_limit)
{
    koi_turn(fish, error, turning_limit);
    const int acceleration = turning_limit > 7 ? 300 : 230;
    fish->speed += fish_clamp(speed - fish->speed, -300, acceleration);
    fish->x += fish->speed * fish_cos(fish->heading) / SUBPIXEL;
    fish->y += fish->speed * fish_sin(fish->heading) / SUBPIXEL;
}

static inline void koi_feed_step(koi_t *fish, const pond_t *pond, uint32_t age, unsigned species)
{
    const unsigned slot = fish->feed_slot;
    const int radius = 43 + fish_sin(age / 7u + species * 86u) / 80;
    const int dx = pond->feed_x * SUBPIXEL + radius * fish_cos(slot) - fish->x;
    const int dy = pond->feed_y * SUBPIXEL + radius * fish_sin(slot) - fish->y;
    const int distance = fish_distance(dx, dy) / SUBPIXEL;

    if (fish->at_food || distance < 18) {
        fish->at_food = 1;
        const unsigned facing_food = fish_direction(pond->feed_x * SUBPIXEL - fish->x,
                                                    pond->feed_y * SUBPIXEL - fish->y);
        koi_turn(fish, fish_heading_error(fish->heading, facing_food), 7);
        fish->x += fish_clamp(dx / 6, -340, 340);
        fish->y += fish_clamp(dy / 6, -340, 340);
        fish->speed += fish_clamp(250 - fish->speed, -160, 100);
        return;
    }

    const int error = fish_heading_error(fish->heading, fish_direction(dx, dy));
    const int speed = fish_clamp(distance * 30, 450, 2560);
    const int turning_speed = 2560 - fish_abs(error) * 14;
    koi_swim(fish, error, speed < turning_speed ? speed : turning_speed, 12);
}

static inline void koi_patrol_step(koi_t *fish, uint32_t elapsed, unsigned species)
{
    enum { ROUTE_STOPS = 6 };
    static const int16_t route[KOI_COUNT][ROUTE_STOPS][2] = {
        {{130, 102}, {237, 77}, {305, 135}, {262, 215}, {144, 209}, {78, 142}},
        {{276, 229}, {209, 299}, {106, 282}, {91, 199}, {188, 149}, {296, 172}},
        {{126, 295}, {235, 317}, {306, 260}, {278, 168}, {169, 125}, {82, 207}},
    };
    static const int16_t cruise_speed[KOI_COUNT] = {625, 550, 660};
    const int16_t *goal = route[species][fish->next_stop];
    int dx = goal[0] * SUBPIXEL - fish->x;
    int dy = goal[1] * SUBPIXEL - fish->y;
    if (fish_distance(dx, dy) < 34 * SUBPIXEL) {
        fish->next_stop = (fish->next_stop + 1) % ROUTE_STOPS;
        goal = route[species][fish->next_stop];
        dx = goal[0] * SUBPIXEL - fish->x;
        dy = goal[1] * SUBPIXEL - fish->y;
    }

    const int error = fish_heading_error(fish->heading, fish_direction(dx, dy));
    const int glide = fish_sin(elapsed / (9u + species * 2u) + species * 69u) / 3;
    const int speed = cruise_speed[species] + glide - fish_abs(error) * 2;
    koi_swim(fish, error, speed, 6);
}

static inline void pond_step(pond_t *pond, uint32_t elapsed)
{
    /* Ignore catch-up ticks timestamped just before the last tap. */
    const uint32_t age = elapsed - pond->feed_start;
    const int after_feed = age < UINT32_C(0x80000000);
    if (pond->feeding && after_feed && age >= FEED_MS) { pond->feeding = 0; }
    const int gathering = pond->feeding && after_feed;
    for (unsigned i = 0; i < KOI_COUNT; ++i) {
        if (elapsed < spawn_ms[i]) { continue; }
        koi_t *fish = &pond->fish[i];
        if (gathering) {
            koi_feed_step(fish, pond, age, i);
        } else {
            koi_patrol_step(fish, elapsed, i);
        }
        const unsigned beats = 8u + i * 2u + (unsigned)(fish->speed / 145);
        fish->tail_phase = (fish->tail_phase + beats) & 255;
    }
}

