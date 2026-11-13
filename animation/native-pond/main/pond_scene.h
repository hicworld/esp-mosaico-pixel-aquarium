#pragma once
#include <stdbool.h>
#include <stdint.h>

bool pond_scene_frame(uint32_t now);
void pond_scene_request_weather(bool rain);
bool pond_scene_is_rain(void);
unsigned pond_scene_visible_fish(void);
void pond_scene_touch(bool down, int x, int y, uint32_t now);
