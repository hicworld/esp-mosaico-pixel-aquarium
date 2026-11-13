#pragma once

#include "water_texture.h"
#ifdef KOI_NATIVE_FIRMWARE
#include "water_reflection.h"
#endif

enum { WATER_SCALE = FISH_GRID / WATER_MAP_SIZE, WATER_GLINT_COUNT = 4,
       WATER_PALETTE_SIZE = sizeof water_colors / sizeof *water_colors };
_Static_assert(WATER_SCALE == 4, "water pixels are expanded four at a time");

typedef uint64_t water_quad_t __attribute__((aligned(2), may_alias));

static const fish_box_t water_glints[WATER_GLINT_COUNT] = {
    {76, 83, 118, 105},
    {250, 94, 292, 116},
    {130, 215, 172, 237},
    {268, 278, 310, 300},
};

/* One fixed window changes on each phase transition. A tile can therefore
 * rebuild any overlapping part of the current water without frame history. */
static fish_box_t water_glint_box(uint32_t phase)
{
    return water_glints[(phase - 1u) % WATER_GLINT_COUNT];
}

static inline uint64_t water_color_at(const uint8_t *source, int x, const uint64_t *palette)
{
    const unsigned pair = source[x >> 3];
    return palette[(pair >> ((x & 4) ? 4 : 0)) & 15];
}

static void water_paint_row(const uint8_t *source, int x, int end, uint16_t *out,
                           const uint64_t *palette)
{
    if (x & 3) {
        const uint16_t color = (uint16_t)water_color_at(source, x, palette);
        const int edge = fish_min(end, (x | 3) + 1);
        while (x < edge) {
            *out++ = color;
            ++x;
        }
    }
    if ((x & 4) && x + 4 <= end) {
        *(water_quad_t *)out = water_color_at(source, x, palette);
        out += 4;
        x += 4;
    }
    const uint8_t *pairs = source + (x >> 3);
    while (x + 8 <= end) {
        const unsigned pair = *pairs++;
        ((water_quad_t *)out)[0] = palette[pair & 15];
        ((water_quad_t *)out)[1] = palette[pair >> 4];
        out += 8;
        x += 8;
    }
    if (x + 4 <= end) {
        *(water_quad_t *)out = water_color_at(source, x, palette);
        out += 4;
        x += 4;
    }
    if (x < end) {
        const uint16_t color = (uint16_t)water_color_at(source, x, palette);
        while (x < end) {
            *out++ = color;
            ++x;
        }
    }
}

static void water_paint_glint(fish_box_t tile, uint16_t *pixels, int width,
                              unsigned index, uint32_t phase)
{
    static const uint8_t pulse[8] = {1, 2, 3, 4, 3, 2, 1, 0};
    static const uint8_t ripple_y[2][42] = {
        {8, 8, 7, 7, 6, 6, 5, 4, 4, 3, 3, 2, 2, 2, 2, 2, 2, 3, 3, 3, 4,
         4, 5, 5, 6, 7, 7, 8, 8, 8, 8, 8, 7, 7, 6, 6, 5, 5, 4, 4, 4, 3},
        {5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 9, 9, 9, 8, 8, 8, 7, 7, 7, 6,
         6, 5, 5, 4, 4, 3, 3, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6, 7},
    };
    const fish_box_t box = water_glints[index];
    if (!boxes_overlap(tile, box)) { return; }
    const unsigned step = (phase / WATER_GLINT_COUNT +
                           (phase % WATER_GLINT_COUNT > index) + 2u * index) & 7u;
    const unsigned peak = pulse[step];
    if (!peak) { return; }
    const int low_y = fish_max(tile.y0, box.y0);
    const int high_y = fish_min(tile.y1, box.y1);

    for (int branch = 0; branch < 2; ++branch) {
        const int left = box.x0 + (branch ? 7 : 0);
        const int right = box.x1 - (branch ? 4 : 0);
        const int x0 = fish_max(tile.x0, left);
        const int x1 = fish_min(tile.x1, right);
        const int base_y = box.y0 + 2 + branch * 7 + (int)(step >> 1);
        for (int x = x0; x < x1; ++x) {
            const int column = x - left;
            const int y = base_y + ripple_y[branch][column];
            if (y + 1 < low_y || y - 1 >= high_y) { continue; }

            const int feather = fish_min(x - left, right - 1 - x) / 3;
            int amount = fish_min((int)peak, feather);
            if (branch && amount > 1) { --amount; }
            if (((column + (int)step * 4 + (int)index * 3) & 15) < 4 && amount > 1) {
                ++amount;
            }
            if (amount > 0) {
                /* The palette leaves room for additive highlights. Fade the
                 * light across three rows so the glint has a softer edge. */
                const unsigned glow = (unsigned)amount >> 1;
                const uint16_t tint = (uint16_t)((glow << 11) |
                                                ((unsigned)amount << 6) |
                                                (unsigned)amount);
                const uint16_t halo = (uint16_t)(((glow >> 1) << 11) |
                                                (glow << 6) | glow);
                const int column_offset = x - tile.x0;
                if (y >= low_y && y < high_y) {
                    pixels[(y - tile.y0) * width + column_offset] += tint;
                }
                if (glow && y - 1 >= low_y && y - 1 < high_y) {
                    pixels[(y - 1 - tile.y0) * width + column_offset] += halo;
                }
                if (glow && y + 1 >= low_y && y + 1 < high_y) {
                    pixels[(y + 1 - tile.y0) * width + column_offset] += halo;
                }
            }
        }
    }
}

#ifdef KOI_NATIVE_FIRMWARE
static void water_paint_reflection_row(const uint8_t *source, int x, int end, uint16_t *out)
{
    if (x & 3) {
        const uint16_t color = (uint16_t)water_reflection_colors[source[x >> 2]];
        const int edge = fish_min(end, (x | 3) + 1);
        while (x < edge) {
            *out++ = color;
            ++x;
        }
    }
    const uint8_t *cells = source + (x >> 2);
    while (x + 4 <= end) {
        *(water_quad_t *)out = water_reflection_colors[*cells++];
        out += 4;
        x += 4;
    }
    if (x < end) {
        const uint16_t color = (uint16_t)water_reflection_colors[*cells];
        while (x < end) {
            *out++ = color;
            ++x;
        }
    }
}
#endif

/* Expand one map row, then copy it within its four-pixel band. */
static inline void water_paint_map(fish_box_t tile, uint16_t *pixels, const uint64_t *palette,
                                  const uint8_t *map, int map_stride)
{
    const int width = tile.x1 - tile.x0;
    uint16_t *out = pixels;
    for (int y = tile.y0; y < tile.y1;) {
        const uint8_t *source = map + (y >> 2) * map_stride;
        const int rows = fish_min(4 - (y & 3), tile.y1 - y);
#ifdef KOI_NATIVE_FIRMWARE
        if (map_stride == WATER_MAP_SIZE) {
            water_paint_reflection_row(source, tile.x0, tile.x1, out);
        } else
#endif
        {
            water_paint_row(source, tile.x0, tile.x1, out, palette);
        }
        for (int row = 1; row < rows; ++row) {
            __builtin_memcpy(out + row * width, out, (unsigned)width * sizeof(*out));
        }
        y += rows;
        out += rows * width;
    }
}

static inline void water_paint_base(fish_box_t tile, uint16_t *pixels, const uint64_t *palette)
{
    water_paint_map(tile, pixels, palette, water_map, WATER_MAP_SIZE / 2);
}

static void water_paint_tile(fish_box_t tile, uint16_t *pixels, uint32_t phase)
{
#ifdef KOI_NATIVE_FIRMWARE
    water_paint_map(tile, pixels, water_reflection_colors, water_reflection_map, WATER_MAP_SIZE);
#else
    water_paint_base(tile, pixels, water_colors);
#endif
    const int width = tile.x1 - tile.x0;
    for (unsigned i = 0; i < WATER_GLINT_COUNT; ++i) {
        water_paint_glint(tile, pixels, width, i, phase);
    }
}
