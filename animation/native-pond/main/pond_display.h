#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "pond_viewport.h"

esp_err_t pond_display_start(void);

/* Call begin, patch and end from the application task. Coordinates and
 * tightly packed RGB565 pixels describe the 384×384 logical pond. */
bool pond_display_begin(void);
int pond_display_patch(int x, int y, int width, int height, const uint16_t *pixels);
void pond_display_end(void);

/* Returns physical screen coordinates. A release keeps the last pressed position. */
bool pond_display_touch(int *x, int *y);

unsigned pond_display_frames(void);
void pond_display_status(void);
