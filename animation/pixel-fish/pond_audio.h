#pragma once

#include <stdint.h>
#include "platform.h"

enum {
    POND_AUDIO_MUSIC = 1,
    POND_MUSIC_OFF = 0,
    POND_MUSIC_NIGHT = 2,
};

static const char pond_music_key[] = "music";
static uint8_t pond_music_enabled;

static inline void pond_audio_apply(void)
{
    const int preset = pond_music_enabled ? POND_MUSIC_NIGHT : POND_MUSIC_OFF;
    (void)platform_audio(POND_AUDIO_MUSIC, preset);
}

static inline void pond_audio_init(void)
{
    uint8_t saved;
    const int32_t count = platform_kv_get(pond_music_key, sizeof(pond_music_key) - 1, &saved, sizeof(saved));
    pond_music_enabled = count == sizeof(saved) && saved <= 1 ? saved : 1;
    pond_audio_apply();
}

static inline int pond_audio_enabled(void)
{
    return pond_music_enabled;
}

static inline void pond_audio_toggle(void)
{
    pond_music_enabled = !pond_music_enabled;
    (void)platform_kv_set(pond_music_key, sizeof(pond_music_key) - 1, &pond_music_enabled, sizeof(pond_music_enabled));
    pond_audio_apply();
}
