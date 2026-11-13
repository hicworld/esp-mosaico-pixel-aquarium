#pragma once

#include "pond_motion.h"
#include "koi_textures.h"

enum { FISH_FLIP_X = 1, FISH_FLIP_Y = 2, FISH_SWAP_AXES = 4, FISH_ORIENTATIONS = 8 };
enum { TILE_SIZE = 128 };
#ifdef KOI_NATIVE_FIRMWARE
enum { ORIGIN_X = 0, ORIGIN_Y = 0 };
#include "pond_school.h"
typedef school_t koi_school_t;
enum { DRAW_FISH_COUNT = SCHOOL_FISH_COUNT, FOOD_FOOTPRINT = SCHOOL_FISH_COUNT,
       MAX_FOOD_DOTS = SCHOOL_MAX_PELLETS, FOOTPRINT_COUNT = FOOD_FOOTPRINT + MAX_FOOD_DOTS,
       MAX_DIRTY_REGIONS = 2 * FOOTPRINT_COUNT + 16 };
#else
enum { ORIGIN_X = 48, ORIGIN_Y = 8 };
typedef pond_t koi_school_t;
enum { DRAW_FISH_COUNT = KOI_COUNT, FOOD_FOOTPRINT = KOI_COUNT, FOOTPRINT_COUNT = KOI_COUNT + 1,
       MAX_DIRTY_REGIONS = 2 * FOOTPRINT_COUNT, MAX_FOOD_DOTS = 4 };
#endif
enum { PATCH_WORK_BUDGET = 15000, WATER_BATCH_BUDGET = 22000,
       WATER_PATCH_LIMIT = 24000, WATER_STEP_MS = 38 };

typedef struct { int x0, y0, x1, y1; } fish_box_t;
typedef struct { uint16_t start, count; } koi_row_t;
typedef struct {
    const uint8_t *pose;
    int flip_x, flip_y;
    koi_row_t rows[TILE_SIZE];
    uint16_t spans[KOI_MAX_SPANS];
    uint16_t pixels[KOI_MAX_PIXELS];
} koi_cache_t;

typedef struct {
    fish_box_t box;
    const koi_cache_t *cache;
} koi_sprite_t;

typedef struct { const uint8_t *pose; int opposite, reflected; } koi_pose_t;

typedef uint32_t pixel_pair_t __attribute__((aligned(2), may_alias));

static koi_cache_t koi_cache[DRAW_FISH_COUNT];
#ifdef KOI_NATIVE_FIRMWARE
typedef struct { const uint8_t *pose; int flip_x, flip_y; } koi_pose_stamp_t;
static koi_pose_stamp_t previous_poses[DRAW_FISH_COUNT];
#else
static unsigned next_art_update;
static int previous_dot_count;
#endif
static uint16_t tile_pixels[TILE_SIZE * TILE_SIZE];
static fish_box_t previous_footprints[FOOTPRINT_COUNT];
static uint32_t previous_water_phase, previous_water_update;

typedef struct {
    koi_sprite_t fish[DRAW_FISH_COUNT];
#ifdef KOI_NATIVE_FIRMWARE
    koi_pose_stamp_t poses[DRAW_FISH_COUNT];
#endif
    fish_box_t next[FOOTPRINT_COUNT], regions[MAX_DIRTY_REGIONS];
    scene_cmd_t dots[MAX_FOOD_DOTS];
    int fish_count, dot_count, region_count, next_region, failures;
    uint32_t water_phase, water_update;
    int water_enabled;
} pond_frame_t;

static pond_frame_t pending_frame;

static inline int fish_min(int a, int b) { return a < b ? a : b; }
static inline int fish_max(int a, int b) { return a > b ? a : b; }

static inline int box_empty(fish_box_t box)
{
    return box.x0 >= box.x1 || box.y0 >= box.y1;
}

static inline int tile_fits(fish_box_t box)
{
    return box.x1 - box.x0 <= TILE_SIZE && box.y1 - box.y0 <= TILE_SIZE;
}

static inline int boxes_overlap(fish_box_t a, fish_box_t b)
{
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

static inline fish_box_t box_union(fish_box_t a, fish_box_t b)
{
    return (fish_box_t){fish_min(a.x0, b.x0), fish_min(a.y0, b.y0),
                        fish_max(a.x1, b.x1), fish_max(a.y1, b.y1)};
}

static inline fish_box_t box_clip(fish_box_t box)
{
    return (fish_box_t){fish_max(0, box.x0), fish_max(0, box.y0),
                        fish_min(FISH_GRID, box.x1), fish_min(FISH_GRID, box.y1)};
}

static inline void orient_point(unsigned orientation, int *x, int *y)
{
    if (orientation & FISH_SWAP_AXES) {
        int old_x = *x;
        *x = *y;
        *y = old_x;
    }
    if (orientation & FISH_FLIP_X) { *x = FISH_GRID - 1 - *x; }
    if (orientation & FISH_FLIP_Y) { *y = FISH_GRID - 1 - *y; }
}

static koi_pose_t koi_pose(const koi_t *fish, unsigned species, unsigned orientation)
{
    int heading = (int)fish->heading;
    int reflected = 0;
    if (orientation & FISH_SWAP_AXES) { heading = 64 - heading; reflected ^= 1; }
    if (orientation & FISH_FLIP_X) { heading = 128 - heading; reflected ^= 1; }
    if (orientation & FISH_FLIP_Y) { heading = -heading; reflected ^= 1; }
    if (reflected) { heading = -heading; }
    const unsigned direction = (((unsigned)heading & 255) * KOI_DIRECTIONS + 128) / 256 % KOI_DIRECTIONS;
    const unsigned stroke = (fish->tail_phase * KOI_STROKES + 128) / 256 % KOI_STROKES;
    const unsigned angles = KOI_DIRECTIONS / 2;
    const uint8_t *pose = koi_atlas + koi_offsets[(species * KOI_STROKES + stroke) * angles
                                                + direction % angles];
    return (koi_pose_t){pose, direction >= angles, reflected};
}

static void pond_forget_frame(void)
{
    __builtin_memset(previous_footprints, 0, sizeof(previous_footprints));
#ifdef KOI_NATIVE_FIRMWARE
    __builtin_memset(previous_poses, 0, sizeof(previous_poses));
#else
    previous_dot_count = 0;
#endif
    previous_water_phase = previous_water_update = 0;
    pending_frame.region_count = pending_frame.next_region = pending_frame.failures = 0;
}

static void pond_reset_art(void)
{
    for (unsigned i = 0; i < DRAW_FISH_COUNT; ++i) { koi_cache[i].pose = 0; }
#ifndef KOI_NATIVE_FIRMWARE
    next_art_update = 0;
#endif
    pond_forget_frame();
}

static void cache_pose(koi_cache_t *cache, koi_pose_t desired, unsigned species)
{
    const uint8_t *source = desired.pose + 4;
    const uint32_t *normal = koi_words[species], *reverse = koi_reversed_words[species];
    const int width = desired.pose[2], height = desired.pose[3];
    const int flip_x = desired.opposite, flip_y = desired.opposite ^ desired.reflected;
#ifndef KOI_NATIVE_FIRMWARE
    __builtin_memset(cache->pixels, 0, (unsigned)(width * height) * sizeof(*cache->pixels));
#endif
    unsigned next_span = 0;
    for (int y = 0; y < height; ++y) {
        const int image_y = flip_y ? height - 1 - y : y;
        const unsigned span_count = *source++;
        cache->rows[image_y] = (koi_row_t){(uint16_t)next_span, (uint16_t)span_count};
        uint16_t *line = cache->pixels + image_y * width;
        for (unsigned span = 0; span < span_count; ++span) {
            const int x = source[0], length = source[1], count = length / 2;
            const unsigned span_index = next_span + (flip_x ? span_count - 1 - span : span);
            const int image_x = flip_x ? width - x - length : x;
            cache->spans[span_index] = (uint16_t)(image_x | length << 8);
            source += 2;
            if (flip_x) {
                uint16_t *dest = line + width - x;
                for (int i = 0; i < count; ++i) {
                    *(pixel_pair_t *)(dest - 2 - 2*i) = reverse[source[i]];
                }
                if (length & 1) { dest[-length] = (uint16_t)normal[source[count]]; }
            } else {
                uint16_t *dest = line + x;
                for (int i = 0; i < count; ++i) {
                    *(pixel_pair_t *)(dest + 2*i) = normal[source[i]];
                }
                if (length & 1) { dest[length - 1] = (uint16_t)normal[source[count]]; }
            }
            source += (length + 1) / 2;
        }
        next_span += span_count;
    }
    cache->pose = desired.pose;
    cache->flip_x = flip_x;
    cache->flip_y = flip_y;
}

static int refresh_poses(const koi_school_t *pond, uint32_t elapsed, unsigned orientation)
{
#ifdef KOI_NATIVE_FIRMWARE
    (void)elapsed;
    int updated = 0;
    for (unsigned i = 0; i < DRAW_FISH_COUNT; ++i) {
        const unsigned species = i % KOI_COUNT;
        const koi_pose_t desired = koi_pose(&pond->fish[i], species, orientation);
        koi_cache_t *cache = &koi_cache[i];
        if (desired.pose != cache->pose || desired.opposite != cache->flip_x ||
            (desired.opposite ^ desired.reflected) != cache->flip_y) {
            cache_pose(cache, desired, species);
            ++updated;
        }
    }
    return updated;
#else
    // The Wasm host expands only one pose per timer tick.
    for (unsigned i = 0; i < KOI_COUNT; ++i) {
        if (elapsed >= spawn_ms[i] && !koi_cache[i].pose) {
            cache_pose(&koi_cache[i], koi_pose(&pond->fish[i], i, orientation), i);
            return 1;
        }
    }
    for (unsigned offset = 0; offset < KOI_COUNT; ++offset) {
        const unsigned i = (next_art_update + offset) % KOI_COUNT;
        if (elapsed < spawn_ms[i]) { continue; }
        const koi_pose_t desired = koi_pose(&pond->fish[i], i, orientation);
        if (desired.pose != koi_cache[i].pose || desired.opposite != koi_cache[i].flip_x) {
            cache_pose(&koi_cache[i], desired, i);
            next_art_update = (i + 1) % KOI_COUNT;
            return 1;
        }
    }
    return 0;
#endif
}

static koi_sprite_t koi_sprite(const koi_t *fish, const koi_cache_t *cache, unsigned orientation)
{
    int x = (fish->x + SUBPIXEL / 2) >> 8, y = (fish->y + SUBPIXEL / 2) >> 8;
    orient_point(orientation, &x, &y);
    const uint8_t *pose = cache->pose;
    const int width = pose[2], height = pose[3];
    const int left = (int8_t)pose[0], top = (int8_t)pose[1];
    x += cache->flip_x ? 1 - left - width : left;
    y += cache->flip_y ? 1 - top - height : top;
    return (koi_sprite_t){{x, y, x + width, y + height}, cache};
}

static fish_box_t box_intersection(fish_box_t a, fish_box_t b)
{
    return (fish_box_t){fish_max(a.x0, b.x0), fish_max(a.y0, b.y0),
                        fish_min(a.x1, b.x1), fish_min(a.y1, b.y1)};
}

#include "water_art.h"

#ifdef KOI_NATIVE_FIRMWARE
static void native_paint_water(fish_box_t tile, uint16_t *pixels, uint32_t phase);
static void native_weather_damage(pond_frame_t *frame, uint32_t elapsed);
#else
#define native_paint_water water_paint_tile
#endif

static int draw_backdrop_tile(int index)
{
    const int x = index % 3 * TILE_SIZE, y = index / 3 * TILE_SIZE;
    native_paint_water((fish_box_t){x, y, x + TILE_SIZE, y + TILE_SIZE}, tile_pixels, 0);
    return platform_image(ORIGIN_X + x, ORIGIN_Y + y, TILE_SIZE, TILE_SIZE, tile_pixels) == 0;
}

/* Use opaque row copies wherever no earlier fish can be erased. Only
 * rows that overlap an earlier fish need to preserve transparent gaps. */
static void blit_koi(const koi_sprite_t *fish, fish_box_t tile, int stride, fish_box_t covered,
                     int transparent)
{
    const koi_cache_t *cache = fish->cache;
    const fish_box_t overlap = box_intersection(fish->box, tile);
    const int width = cache->pose[2];
    const int x0 = overlap.x0 - fish->box.x0, x1 = overlap.x1 - fish->box.x0;
    int row = overlap.y0 - fish->box.y0;
    const uint16_t *pixels = cache->pixels + row * width;
    uint16_t *line = tile_pixels + (overlap.y0 - tile.y0) * stride + (overlap.x0 - tile.x0);

    // A dirty strip usually clips the fish vertically only. Copy opaque runs
    // directly when their full horizontal extent lies inside the strip.
    if (transparent && x0 == 0 && x1 == width) {
        for (int y = overlap.y0; y < overlap.y1; ++y, ++row, line += stride, pixels += width) {
            const koi_row_t span_row = cache->rows[row];
            const uint16_t *span = cache->spans + span_row.start;
            const uint16_t *end = span + span_row.count;
            for (; span != end; ++span) {
                const unsigned x = *span & 255;
                const unsigned length = *span >> 8;
                __builtin_memcpy(line + x, pixels + x, length * sizeof(*line));
            }
        }
        return;
    }
    const fish_box_t behind = transparent ? overlap : box_intersection(overlap, covered);
    const unsigned row_bytes = (unsigned)(x1 - x0) * sizeof(*line);
    if (box_empty(behind)) {
        for (int y = overlap.y0; y < overlap.y1; ++y, line += stride, pixels += width) {
            __builtin_memcpy(line, pixels + x0, row_bytes);
        }
        return;
    }
    for (int y = overlap.y0; y < overlap.y1; ++y, ++row, line += stride, pixels += width) {
        if (y < behind.y0 || y >= behind.y1) {
            __builtin_memcpy(line, pixels + x0, row_bytes);
            continue;
        }
        const koi_row_t spans = cache->rows[row];
        for (unsigned i = spans.start; i < (unsigned)spans.start + spans.count; ++i) {
            const unsigned span = cache->spans[i];
            const int x = span & 255;
            if (x >= x1) { break; }
            const int start = fish_max(x, x0), end = fish_min(x + (int)(span >> 8), x1);
            if (start < end) {
                __builtin_memcpy(line + (start - x0), pixels + start,
                                 (unsigned)(end - start) * sizeof(*line));
            }
        }
    }
}

static fish_box_t food_box(const scene_cmd_t *dot)
{
    const int x = dot->x - ORIGIN_X, y = dot->y - ORIGIN_Y;
    return (fish_box_t){x, y, x + dot->w, y + dot->h};
}

static void blit_food(fish_box_t tile, const scene_cmd_t *dots, int dot_count, int only_empty)
{
    const int width = tile.x1 - tile.x0;
    for (int i = 0; i < dot_count; ++i) {
        const fish_box_t dot = box_intersection(tile, food_box(&dots[i]));
        if (box_empty(dot)) { continue; }
        const uint32_t rgb = dots[i].rgb;
        const uint16_t color = (uint16_t)(((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f));
        for (int y = dot.y0; y < dot.y1; ++y) {
            uint16_t *row = tile_pixels + (y - tile.y0) * width;
            for (int x = dot.x0; x < dot.x1; ++x) {
                uint16_t *pixel = row + (x - tile.x0);
                if (!only_empty || !*pixel) { *pixel = color; }
            }
        }
    }
}

static int draw_dirty_region(fish_box_t tile, const pond_frame_t *frame)
{
    const int width = tile.x1 - tile.x0, height = tile.y1 - tile.y0;
    if (frame->water_enabled) {
        native_paint_water(tile, tile_pixels, frame->water_phase);
        blit_food(tile, frame->dots, frame->dot_count, 0);
    } else {
        __builtin_memset(tile_pixels, 0, (unsigned)(width * height) * sizeof(*tile_pixels));
    }

    fish_box_t covered = {0, 0, 0, 0};
    for (int i = 0; i < frame->fish_count; ++i) {
        const koi_sprite_t *fish = &frame->fish[i];
        if (boxes_overlap(tile, fish->box)) {
            const fish_box_t part = box_intersection(tile, fish->box);
            blit_koi(fish, tile, width, covered, frame->water_enabled);
            covered = box_empty(covered) ? part : box_union(covered, part);
        }
    }
    if (!frame->water_enabled) { blit_food(tile, frame->dots, frame->dot_count, 1); }

    return platform_image(ORIGIN_X + tile.x0, ORIGIN_Y + tile.y0, width, height, tile_pixels) == 0;
}

#ifdef KOI_NATIVE_FIRMWARE
static inline int box_equal(fish_box_t a, fish_box_t b)
{
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

static inline int box_area(fish_box_t box)
{
    return (box.x1 - box.x0) * (box.y1 - box.y0);
}
#endif

static void add_dirty_region(fish_box_t *regions, int *count, fish_box_t region)
{
    for (int i = 0; i < *count;) {
        const fish_box_t other = regions[i];
#ifndef KOI_NATIVE_FIRMWARE
        if (!boxes_overlap(region, other)) { ++i; continue; }
#endif
        const fish_box_t merged = box_union(region, other);
#ifdef KOI_NATIVE_FIRMWARE
        if (box_equal(merged, other)) { return; }
        // Trade a small amount of extra pixel data for fewer LCD transfers.
        const int merge = tile_fits(merged) && box_area(merged) <= box_area(region) + box_area(other) + 384;
#else
        const int merge = tile_fits(merged);
#endif
        if (merge) {
            region = merged;
            regions[i] = regions[--*count];
            i = 0;
        } else {
            ++i;
        }
    }
    regions[(*count)++] = region;
}

static void add_footprint_change(fish_box_t *regions, int *count, fish_box_t old, fish_box_t next)
{
    if (!box_empty(old) && !box_empty(next) && boxes_overlap(old, next)) {
        const fish_box_t swept = box_union(old, next);
        if (tile_fits(swept)) {
            add_dirty_region(regions, count, swept);
            return;
        }
    }
    if (!box_empty(old)) { add_dirty_region(regions, count, old); }
    if (!box_empty(next)) { add_dirty_region(regions, count, next); }
}

static int pond_frame_pending(void)
{
    return pending_frame.next_region < pending_frame.region_count;
}

#ifndef KOI_NATIVE_FIRMWARE
static int patch_work(fish_box_t tile, const koi_sprite_t *fish, int count, int water)
{
    int work = 750;
    if (water) { work += (tile.x1 - tile.x0) * (tile.y1 - tile.y0) / 2; }
    fish_box_t covered = {0};
    for (int i = 0; i < count; ++i) {
        const fish_box_t part = box_intersection(tile, fish[i].box);
        if (box_empty(part)) { continue; }
        work += 32 * (part.y1 - part.y0);
        const fish_box_t shared = water ? part : box_intersection(part, covered);
        if (!box_empty(shared)) { work += 180 * (shared.y1 - shared.y0); }
        covered = box_empty(covered) ? part : box_union(covered, part);
    }
    return work;
}
#endif

/* Divide heavy crossings over multiple callbacks. Each patch still uses the
 * same finished frame and no pose cache is changed until it is published. */
static int pond_continue_frame(void)
{
    pond_frame_t *frame = &pending_frame;
#ifndef KOI_NATIVE_FIRMWARE
    const int budget = frame->water_enabled ? WATER_BATCH_BUDGET : PATCH_WORK_BUDGET;
    int work = 0;
#endif
    while (pond_frame_pending()) {
        fish_box_t *region = &frame->regions[frame->next_region];
        fish_box_t tile = *region;
#ifndef KOI_NATIVE_FIRMWARE
        int cost = patch_work(tile, frame->fish, frame->fish_count, frame->water_enabled);
        // A crowded tile can cost more than one callback's instruction budget.
        // Submit a strip and resume the same composited frame on the next call.
        while (frame->water_enabled && cost > WATER_PATCH_LIMIT && tile.y1 - tile.y0 > 4) {
            const int rows = (tile.y1 - tile.y0) * WATER_PATCH_LIMIT / cost;
            tile.y1 = tile.y0 + fish_max(4, rows & ~3);
            cost = patch_work(tile, frame->fish, frame->fish_count, 1);
        }
        if (work && work + cost > budget) { return 1; }
#endif
        if (!draw_dirty_region(tile, frame)) {
            // A contended display lock can recover without clearing the canvas.
            return ++frame->failures < 3;
        }
        frame->failures = 0;
        if (tile.y1 < region->y1) { region->y0 = tile.y1; }
        else { ++frame->next_region; }
#ifndef KOI_NATIVE_FIRMWARE
        work += cost;
#endif
    }
    __builtin_memcpy(previous_footprints, frame->next, sizeof(previous_footprints));
#ifdef KOI_NATIVE_FIRMWARE
    __builtin_memcpy(previous_poses, frame->poses, sizeof(previous_poses));
#else
    previous_dot_count = frame->dot_count;
#endif
    previous_water_phase = frame->water_phase;
    previous_water_update = frame->water_update;
    return 1;
}

/* The canvas persists across timer callbacks. Overlapping writes contain
 * identical final pixels, including the background under a departing fish. */
static int pond_draw_koi(const koi_school_t *pond, uint32_t elapsed, unsigned orientation,
                         const scene_cmd_t *dots, int dot_count, int water_enabled)
{
    pond_frame_t *frame = &pending_frame;
    __builtin_memset(frame->next, 0, sizeof(frame->next));
    frame->fish_count = 0;
    frame->water_enabled = water_enabled;
    frame->water_phase = previous_water_phase;
    frame->water_update = previous_water_update;
    frame->region_count = frame->next_region = frame->failures = 0;
    for (unsigned i = 0; i < DRAW_FISH_COUNT; ++i) {
#ifndef KOI_NATIVE_FIRMWARE
        if (elapsed < spawn_ms[i] || !koi_cache[i].pose) { continue; }
#else
        frame->poses[i] = (koi_pose_stamp_t){0};
#endif
        const koi_sprite_t sprite = koi_sprite(&pond->fish[i], &koi_cache[i], orientation);
        const fish_box_t box = box_clip(sprite.box);
        if (!box_empty(box)) {
            frame->next[i] = box;
            frame->fish[frame->fish_count++] = sprite;
#ifdef KOI_NATIVE_FIRMWARE
            frame->poses[i] = (koi_pose_stamp_t){sprite.cache->pose, sprite.cache->flip_x, sprite.cache->flip_y};
#endif
        }
    }

    frame->dot_count = dot_count;
    if (dot_count) {
        __builtin_memcpy(frame->dots, dots, (unsigned)dot_count * sizeof(*dots));
#ifdef KOI_NATIVE_FIRMWARE
        for (int i = 0; i < dot_count; ++i) {
            frame->next[FOOD_FOOTPRINT + dots[i].slot] = food_box(&dots[i]);
        }
#else
        frame->next[FOOD_FOOTPRINT] = food_box(&dots[0]);
        for (int i = 1; i < dot_count; ++i) {
            frame->next[FOOD_FOOTPRINT] = box_union(frame->next[FOOD_FOOTPRINT], food_box(&dots[i]));
        }
#endif
    }

#ifdef KOI_NATIVE_FIRMWARE
    for (int i = 0; i < FOOTPRINT_COUNT; ++i) {
        int changed = !box_equal(previous_footprints[i], frame->next[i]);
        if (i < DRAW_FISH_COUNT) {
            const koi_pose_stamp_t old = previous_poses[i], next = frame->poses[i];
            changed |= old.pose != next.pose || old.flip_x != next.flip_x || old.flip_y != next.flip_y;
        }
        if (changed) {
            add_footprint_change(frame->regions, &frame->region_count, previous_footprints[i], frame->next[i]);
        }
    }
    native_weather_damage(frame, elapsed);
#else
    for (int i = 0; i < KOI_COUNT; ++i) {
        add_footprint_change(frame->regions, &frame->region_count, previous_footprints[i], frame->next[i]);
    }
    const fish_box_t old_food = previous_footprints[FOOD_FOOTPRINT];
    const fish_box_t new_food = frame->next[FOOD_FOOTPRINT];
    if (dot_count != previous_dot_count || old_food.x0 != new_food.x0 || old_food.y0 != new_food.y0 ||
        old_food.x1 != new_food.x1 || old_food.y1 != new_food.y1) {
        add_footprint_change(frame->regions, &frame->region_count, old_food, new_food);
    }
    if (water_enabled && frame->region_count < 4 && elapsed - previous_water_update >= WATER_STEP_MS) {
        const fish_box_t glint = water_glint_box(frame->water_phase + 1);
        int needs_patch = 1;
        int can_update = 1;
        for (int i = 0; i < frame->region_count; ++i) {
            const fish_box_t region = frame->regions[i];
            if (region.x0 <= glint.x0 && region.x1 >= glint.x1 &&
                region.y0 <= glint.y0 && region.y1 >= glint.y1) {
                needs_patch = 0;
                break;
            }
            if (patch_work(region, frame->fish, frame->fish_count, 1) > WATER_PATCH_LIMIT) {
                can_update = 0;
            }
        }
        // Let crowded fish patches finish first. Keep an isolated glint small
        // instead of growing a nearby fish patch to the union of both areas.
        if (!needs_patch || can_update) {
            ++frame->water_phase;
            frame->water_update = elapsed;
            if (needs_patch) { frame->regions[frame->region_count++] = glint; }
        }
    }
#endif
    return pond_continue_frame();
}
