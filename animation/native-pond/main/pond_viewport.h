#pragma once

#include <stddef.h>
#include <stdint.h>

enum { POND_LOGICAL_SIZE = 384, POND_DISPLAY_WIDTH = 480, POND_DISPLAY_HEIGHT = 480 };

static inline int pond_panel_edge(int logical)
{
    return (5 * logical + 3) / 4;
}

static inline int pond_logical_pixel(int panel)
{
    return 4 * panel / 5;
}

/* Expand each group of four source pixels to five, using global coordinates
 * so independently rendered rectangles meet without seams. */
static inline void pond_blit_scaled(uint16_t *canvas, size_t stride,
                                    int x, int y, int width, int height,
                                    const uint16_t *pixels)
{
    const int start_x = pond_panel_edge(x);
    const size_t row_bytes = (size_t)(pond_panel_edge(x + width) - start_x) * sizeof(*canvas);
    const int to_alignment = (4 - (x & 3)) & 3;
    const int prefix = width < to_alignment ? width : to_alignment;
    const int groups = (width - prefix) / 4;
    const int suffix = (width - prefix) % 4;
    uint16_t *line = canvas + (size_t)pond_panel_edge(y) * stride + start_x;

    for (int row = 0; row < height; ++row) {
        const uint16_t *source = pixels + row * width;
        uint16_t *out = line;
        for (int i = 0; i < prefix; ++i) { *out++ = *source++; }
        for (int i = 0; i < groups; ++i) {
            out[0] = out[1] = source[0];
            out[2] = source[1];
            out[3] = source[2];
            out[4] = source[3];
            out += 5;
            source += 4;
        }
        if (suffix) {
            *out++ = source[0];
            for (int i = 0; i < suffix; ++i) { *out++ = source[i]; }
        }
        if (((y + row) & 3) == 0) {
            __builtin_memcpy(line + stride, line, row_bytes);
            line += stride;
        }
        line += stride;
    }
}
