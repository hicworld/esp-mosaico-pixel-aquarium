#pragma once

#include <stdbool.h>

void pond_audio_start(void);
void pond_audio_set_rain(bool rain);
void pond_audio_adjust_volume(int delta);
unsigned pond_audio_volume(void);
void pond_audio_status(void);
