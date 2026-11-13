#pragma once

#include "pond_motion.h"

enum { SCHOOL_FISH_COUNT = 8, SCHOOL_MAX_PELLETS = 24 };
enum { PELLET_EMPTY, PELLET_AVAILABLE, PELLET_EATING };

typedef struct {
    int x, y;
    unsigned state, owner;
    uint32_t id, started, duration;
} pond_pellet_t;

typedef struct {
    uint32_t target_id, rest_rng;
    int entering, reapproach;
    int entry_x, entry_y;
    unsigned escape_heading;
    unsigned rest_wait, rest_ticks;
} school_route_t;

typedef struct {
    koi_t fish[SCHOOL_FISH_COUNT];
    pond_pellet_t pellets[SCHOOL_MAX_PELLETS];
    school_route_t route[SCHOOL_FISH_COUNT];
    uint32_t rng, elapsed, next_pellet;
} school_t;

enum { SCHOOL_NO_PELLET = SCHOOL_MAX_PELLETS, SCHOOL_ROUTE_STOPS = 6 };
enum { SCHOOL_MOUTH_RADIUS = 11 * SUBPIXEL, SCHOOL_PATROL_TURN = 4 };

static const int16_t school_patrol[SCHOOL_FISH_COUNT][SCHOOL_ROUTE_STOPS][2] = {
    {{174, 75}, {277, 92}, {315, 158}, {251, 214}, {116, 200}, {61, 141}},
    {{321, 130}, {327, 216}, {253, 275}, {133, 233}, {147, 133}, {253, 89}},
    {{116, 317}, {223, 302}, {280, 228}, {215, 180}, {106, 193}, {65, 268}},
    {{190, 321}, {85, 300}, {71, 208}, {164, 137}, {270, 170}, {312, 260}},
    {{145, 169}, {111, 98}, {213, 78}, {300, 120}, {265, 245}, {154, 251}},
    {{-65, 145}, {59, 111}, {179, 161}, {150, 279}, {35, 316}, {-68, 266}},
    {{448, 195}, {337, 238}, {242, 204}, {251, 102}, {361, 61}, {450, 102}},
    {{115, 447}, {64, 332}, {108, 232}, {219, 252}, {285, 363}, {249, 451}},
};

static const uint16_t school_cruise_speed[SCHOOL_FISH_COUNT] = {
    375, 690, 500, 830, 560, 630, 760, 445,
};

static inline uint32_t school_random(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

static inline int school_inside(const koi_t *fish, int inset)
{
    const int first = inset * SUBPIXEL, last = (FISH_GRID - 1 - inset) * SUBPIXEL;
    return fish->x >= first && fish->x <= last && fish->y >= first && fish->y <= last;
}

static inline void school_reset(school_t *school, uint32_t seed)
{
    static const int16_t start[SCHOOL_FISH_COUNT][2] = {
        {108, 104}, {257, 97}, {79, 261}, {250, 284}, {155, 222},
        {-72, 222}, {456, 117}, {228, 455},
    };
    *school = (school_t){0};
    school->rng = seed ? seed : UINT32_C(0x6f7d2385);
    for (unsigned i = 0; i < SCHOOL_FISH_COUNT; ++i) {
        koi_t *fish = &school->fish[i];
        school_route_t *route = &school->route[i];
        fish->x = start[i][0] * SUBPIXEL;
        fish->y = start[i][1] * SUBPIXEL;
        fish->heading = fish_direction(school_patrol[i][0][0] - start[i][0],
                                       school_patrol[i][0][1] - start[i][1]);
        fish->tail_phase = school_random(&school->rng) & 255;
        fish->speed = school_cruise_speed[i] * 7 / 8;
        fish->feed_slot = SCHOOL_NO_PELLET;
        route->rest_rng = school->rng;
        route->rest_wait = 38 + i * 12 + school_random(&route->rest_rng) % 47;
    }
}

static inline int school_food_slot(const school_t *school)
{
    int oldest = -1;
    uint32_t oldest_age = 0;
    for (int i = 0; i < SCHOOL_MAX_PELLETS; ++i) {
        const pond_pellet_t *pellet = &school->pellets[i];
        if (pellet->state == PELLET_EMPTY) { return i; }
        const uint32_t age = school->next_pellet - pellet->id;
        if (pellet->state == PELLET_AVAILABLE && (oldest < 0 || age > oldest_age)) {
            oldest = i;
            oldest_age = age;
        }
    }
    return oldest;
}

static inline int school_food_spacing(const school_t *school, int skip, int x, int y)
{
    int nearest = FISH_GRID;
    for (int i = 0; i < SCHOOL_MAX_PELLETS; ++i) {
        const pond_pellet_t *pellet = &school->pellets[i];
        if (i == skip || pellet->state == PELLET_EMPTY) { continue; }
        const int distance = fish_distance(x - pellet->x, y - pellet->y);
        if (distance < nearest) { nearest = distance; }
    }
    return nearest;
}

static inline void school_feed(school_t *school, int x, int y)
{
    const int center_x = fish_clamp(x, 25, FISH_GRID - 26);
    const int center_y = fish_clamp(y, 25, FISH_GRID - 26);
    const unsigned count = 3 + school_random(&school->rng) % 4;
    const unsigned rotation = school_random(&school->rng) & 255;
    for (unsigned i = 0; i < count; ++i) {
        const int slot = school_food_slot(school);
        if (slot < 0) { break; }
        int best_x = center_x, best_y = center_y, best_spacing = -1;
        for (unsigned attempt = 0; attempt < 18; ++attempt) {
            const uint32_t random = school_random(&school->rng);
            const unsigned angle = attempt == 0 ? rotation + i * 256 / count + (random & 15) : random & 255;
            const int radius = 19 + (int)((random >> 8) % (attempt < 9 ? 40u : 63u));
            const int px = fish_clamp(center_x + radius * fish_cos(angle) / SUBPIXEL, 9, FISH_GRID - 10);
            const int py = fish_clamp(center_y + radius * fish_sin(angle) / SUBPIXEL, 9, FISH_GRID - 10);
            const int spacing = school_food_spacing(school, slot, px, py);
            if (spacing > best_spacing) {
                best_spacing = spacing;
                best_x = px;
                best_y = py;
            }
            if (best_spacing >= 18) { break; }
        }
        /* A burst of taps can fill the same small area. Keep pellet centers distinct. */
        while (!school_food_spacing(school, slot, best_x, best_y)) {
            best_x = best_x == FISH_GRID - 10 ? 9 : best_x + 1;
        }
        school->pellets[slot] = (pond_pellet_t){
            .x = best_x, .y = best_y, .state = PELLET_AVAILABLE, .owner = SCHOOL_FISH_COUNT,
            .id = ++school->next_pellet, .started = school->elapsed,
        };
    }

    for (unsigned i = 0; i < SCHOOL_FISH_COUNT; ++i) {
        if (school_inside(&school->fish[i], 0) && !school->route[i].entering) { continue; }
        school_route_t *route = &school->route[i];
        route->entering = 1;
        route->entry_x = fish_clamp(x, 96, FISH_GRID - 97);
        route->entry_y = fish_clamp(y, 96, FISH_GRID - 97);
    }
}

static inline void school_turn(koi_t *fish, unsigned heading, int limit)
{
    int error = fish_heading_error(fish->heading, heading);
    /* Keep the same turn direction while the target is almost directly behind. */
    if (fish_abs(error) >= 120 && error * fish->bend < 0) {
        error += fish->bend > 0 ? 256 : -256;
    }
    int turn = fish_clamp(error / 3, -limit, limit);
    if (!turn && fish_abs(error) > 1) { turn = error < 0 ? -1 : 1; }
    fish->bend += fish_clamp(turn - fish->bend, -1, 1);
    if ((error >= 0 && fish->bend > error) || (error <= 0 && fish->bend < error)) {
        fish->bend = error;
    }
    fish->heading = (fish->heading + (unsigned)fish->bend) & 255;
}

static inline void school_move(koi_t *fish)
{
    fish->x += fish->speed * fish_cos(fish->heading) / SUBPIXEL;
    fish->y += fish->speed * fish_sin(fish->heading) / SUBPIXEL;
}

static inline void school_swim(koi_t *fish, unsigned heading, int speed, int turn_limit)
{
    const int error = fish_abs(fish_heading_error(fish->heading, heading));
    school_turn(fish, heading, turn_limit);
    const int requested_speed = speed - error * speed / 242;
    const int change = requested_speed - fish->speed;
    if (turn_limit == SCHOOL_PATROL_TURN) {
        const int eased = (change + (change > 0 ? 4 : -4)) / 5;
        fish->speed += fish_clamp(eased, -68, 53);
    } else {
        fish->speed += fish_clamp(change, -175, 145);
    }
    school_move(fish);
}

static inline int school_rest(koi_t *fish, school_route_t *route)
{
    if (!route->rest_ticks) {
        if (route->rest_wait) {
            --route->rest_wait;
            return 0;
        }
        if (!school_inside(fish, 68)) { return 0; }
        route->rest_ticks = 22 + school_random(&route->rest_rng) % 26;
        route->rest_wait = 90 + school_random(&route->rest_rng) % 146;
    }

    /* Coast on the current heading before beginning the stationary pause. */
    fish->bend += fish_clamp(-fish->bend, -1, 1);
    fish->heading = (fish->heading + (unsigned)fish->bend) & 255;
    fish->speed -= (fish->speed + 5) / 6;
    if (fish->speed < 36) {
        fish->speed = 0;
        --route->rest_ticks;
    }
    school_move(fish);
    return 1;
}

static inline void school_select_patrol(koi_t *fish, unsigned index)
{
    int nearest = INT32_MAX;
    for (unsigned i = 0; i < SCHOOL_ROUTE_STOPS; ++i) {
        const int16_t *point = school_patrol[index][i];
        if (point[0] < 56 || point[0] >= FISH_GRID - 56 ||
            point[1] < 56 || point[1] >= FISH_GRID - 56) { continue; }
        const int distance = fish_distance(point[0] * SUBPIXEL - fish->x, point[1] * SUBPIXEL - fish->y);
        if (distance < nearest) {
            nearest = distance;
            fish->next_stop = i;
        }
    }
}

static inline void school_free_swim(school_t *school, unsigned index)
{
    koi_t *fish = &school->fish[index];
    school_route_t *route = &school->route[index];
    if (route->entering && !school_inside(fish, 55)) {
        const int dx = route->entry_x * SUBPIXEL - fish->x;
        const int dy = route->entry_y * SUBPIXEL - fish->y;
        school_swim(fish, fish_direction(dx, dy), 1740, 6);
        return;
    }
    if (route->entering) {
        school_select_patrol(fish, index);
        route->entering = 0;
    }
    if (school_rest(fish, route)) { return; }
    const int16_t *target = school_patrol[index][fish->next_stop];
    int dx = target[0] * SUBPIXEL - fish->x;
    int dy = target[1] * SUBPIXEL - fish->y;
    const int outside = target[0] < 0 || target[0] >= FISH_GRID || target[1] < 0 || target[1] >= FISH_GRID;
    if (fish_distance(dx, dy) < (outside ? 18 : 34) * SUBPIXEL) {
        fish->next_stop = (fish->next_stop + 1) % SCHOOL_ROUTE_STOPS;
        target = school_patrol[index][fish->next_stop];
        dx = target[0] * SUBPIXEL - fish->x;
        dy = target[1] * SUBPIXEL - fish->y;
    }
    const int cruise = school_cruise_speed[index];
    const unsigned phase = school->elapsed / (8 + 2 * (index % 5)) + index * 39;
    const int glide = cruise * fish_sin(phase) / (SUBPIXEL * 4);
    school_swim(fish, fish_direction(dx, dy), cruise + glide, SCHOOL_PATROL_TURN);
}

static inline int school_mouth_reach(unsigned fish_index)
{
    static const uint8_t reach[KOI_COUNT] = {42, 40, 38};
    return reach[fish_index % KOI_COUNT];
}

static inline unsigned school_nearest_food(const school_t *school, const koi_t *fish)
{
    unsigned target = SCHOOL_NO_PELLET;
    int nearest = INT32_MAX;
    const int x = fish->x / SUBPIXEL, y = fish->y / SUBPIXEL;
    for (unsigned j = 0; j < SCHOOL_MAX_PELLETS; ++j) {
        const pond_pellet_t *pellet = &school->pellets[j];
        if (pellet->state != PELLET_AVAILABLE) { continue; }
        const int dx = pellet->x - x, dy = pellet->y - y;
        const int distance = dx * dx + dy * dy;
        if (distance < nearest) {
            nearest = distance;
            target = j;
        }
    }
    return target;
}

static inline void school_seek(school_t *school, unsigned index)
{
    koi_t *fish = &school->fish[index];
    school_route_t *route = &school->route[index];
    if (fish->feed_slot == SCHOOL_NO_PELLET ||
        school->pellets[fish->feed_slot].state != PELLET_AVAILABLE ||
        school->pellets[fish->feed_slot].id != route->target_id) {
        fish->feed_slot = school_nearest_food(school, fish);
        route->reapproach = 0;
        if (fish->feed_slot != SCHOOL_NO_PELLET) {
            route->target_id = school->pellets[fish->feed_slot].id;
        }
    }
    if (fish->feed_slot == SCHOOL_NO_PELLET) {
        school_free_swim(school, index);
        return;
    }
    route->rest_ticks = 0;
    if (route->rest_wait < 54) { route->rest_wait = 54; }
    const pond_pellet_t *pellet = &school->pellets[fish->feed_slot];
    const int dx = pellet->x * SUBPIXEL - fish->x;
    const int dy = pellet->y * SUBPIXEL - fish->y;
    const int distance = fish_distance(dx, dy) / SUBPIXEL;
    const int reach = school_mouth_reach(index);
    if (!route->reapproach && distance < reach - 6) {
        route->reapproach = 1;
        route->escape_heading = fish->heading;
    }
    if (route->reapproach && distance < reach + 22) {
        school_swim(fish, route->escape_heading, 1100, 5);
        return;
    }
    route->reapproach = 0;
    const int speed = fish_clamp((distance - reach) * 25 + 420, 420, 1930);
    school_swim(fish, fish_direction(dx, dy), speed, 6);
}

static inline void school_eat(koi_t *fish, const pond_pellet_t *pellet, int reach)
{
    const int dx = pellet->x * SUBPIXEL - reach * fish_cos(fish->heading) - fish->x;
    const int dy = pellet->y * SUBPIXEL - reach * fish_sin(fish->heading) - fish->y;
    fish->bend = 0;
    fish->speed += fish_clamp(0 - fish->speed, -175, 0);
    fish->x += fish_clamp(dx / 5, -230, 230);
    fish->y += fish_clamp(dy / 5, -230, 230);
}

typedef struct { int x, y; } school_mouth_t;

static inline school_mouth_t school_mouth(const koi_t *fish, int reach)
{
    return (school_mouth_t){fish->x + reach * fish_cos(fish->heading),
                            fish->y + reach * fish_sin(fish->heading)};
}

static inline void school_step(school_t *school, uint32_t elapsed)
{
    school->elapsed = elapsed;
    for (unsigned j = 0; j < SCHOOL_MAX_PELLETS; ++j) {
        pond_pellet_t *pellet = &school->pellets[j];
        const uint32_t age = elapsed - pellet->started;
        if (pellet->state == PELLET_EATING && age < UINT32_C(0x80000000) && age >= pellet->duration) {
            pellet->state = PELLET_EMPTY;
        }
    }

    unsigned claimant[SCHOOL_MAX_PELLETS];
    int arrival[SCHOOL_MAX_PELLETS], closest[SCHOOL_MAX_PELLETS];
    for (unsigned j = 0; j < SCHOOL_MAX_PELLETS; ++j) {
        claimant[j] = SCHOOL_FISH_COUNT;
        arrival[j] = SUBPIXEL + 1;
        closest[j] = INT32_MAX;
    }
    for (unsigned i = 0; i < SCHOOL_FISH_COUNT; ++i) {
        koi_t *fish = &school->fish[i];
        const int reach = school_mouth_reach(i);
        const pond_pellet_t *meal = fish->at_food && fish->feed_slot < SCHOOL_MAX_PELLETS ?
                                    &school->pellets[fish->feed_slot] : 0;
        if (meal && meal->state == PELLET_EATING && meal->owner == i &&
            meal->id == school->route[i].target_id) {
            school_eat(fish, meal, reach);
        } else {
            fish->at_food = 0;
            const school_mouth_t before = school_mouth(fish, reach);
            school_seek(school, i);
            const school_mouth_t after = school_mouth(fish, reach);
            int best_time = SUBPIXEL + 1, best_distance = INT32_MAX;
            unsigned candidate = SCHOOL_NO_PELLET;
            for (unsigned j = 0; j < SCHOOL_MAX_PELLETS; ++j) {
                const pond_pellet_t *pellet = &school->pellets[j];
                if (pellet->state != PELLET_AVAILABLE) { continue; }
                const int x = pellet->x * SUBPIXEL, y = pellet->y * SUBPIXEL;
                const int start = fish_distance(x - before.x, y - before.y);
                const int end = fish_distance(x - after.x, y - after.y);
                if (start > SCHOOL_MOUTH_RADIUS && end > SCHOOL_MOUTH_RADIUS) { continue; }
                const int time = start <= SCHOOL_MOUTH_RADIUS ? 0 :
                                 (start - SCHOOL_MOUTH_RADIUS) * SUBPIXEL / (start - end);
                if (time < best_time || (time == best_time && end < best_distance)) {
                    candidate = j;
                    best_time = time;
                    best_distance = end;
                }
            }
            if (candidate != SCHOOL_NO_PELLET &&
                (best_time < arrival[candidate] ||
                 (best_time == arrival[candidate] && best_distance < closest[candidate]))) {
                claimant[candidate] = i;
                arrival[candidate] = best_time;
                closest[candidate] = best_distance;
            }
        }
        const unsigned beats = 2 + i % KOI_COUNT + (unsigned)(fish->speed / 104);
        fish->tail_phase = (fish->tail_phase + beats) & 255;
    }

    /* Targets remain available until a mouth reaches them. Compare the
     * crossing time within the tick when two fish arrive together. */
    for (unsigned j = 0; j < SCHOOL_MAX_PELLETS; ++j) {
        const unsigned i = claimant[j];
        if (i == SCHOOL_FISH_COUNT) { continue; }
        pond_pellet_t *pellet = &school->pellets[j];
        pellet->state = PELLET_EATING;
        pellet->owner = i;
        pellet->started = elapsed;
        pellet->duration = 500 + school_random(&school->rng) % 851;
        school->fish[i].feed_slot = j;
        school->fish[i].at_food = 1;
        school->route[i].target_id = pellet->id;
        school->route[i].reapproach = 0;
    }
}

static inline void school_interpolate(school_t *output, const school_t *before,
                                      const school_t *after, unsigned fraction_ms)
{
    const int fraction = (int)fraction_ms;
    for (unsigned i = 0; i < SCHOOL_FISH_COUNT; ++i) {
        const koi_t *first = &before->fish[i], *last = &after->fish[i];
        koi_t sample = *last;
        sample.x = first->x + (last->x - first->x) * fraction / FISH_FRAME_MS;
        sample.y = first->y + (last->y - first->y) * fraction / FISH_FRAME_MS;
        sample.heading = fish_interpolate_angle(first->heading, last->heading, fraction);
        sample.tail_phase = fish_interpolate_angle(first->tail_phase, last->tail_phase, fraction);
        sample.bend = first->bend + (last->bend - first->bend) * fraction / FISH_FRAME_MS;
        output->fish[i] = sample;
        output->route[i] = after->route[i];
    }
    for (unsigned i = 0; i < SCHOOL_MAX_PELLETS; ++i) { output->pellets[i] = after->pellets[i]; }
    output->rng = after->rng;
    output->elapsed = after->elapsed;
    output->next_pellet = after->next_pellet;
}

