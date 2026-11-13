#include "pond_audio.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>

#include "bsp/esp_mosaico.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "third_party/minimp3/minimp3.h"

enum {
    SAMPLE_RATE = 24000,
    OUTPUT_CHANNELS = 2,
    DEFAULT_VOLUME = 70,
    MAX_VOLUME = 100,
    AUDIO_STACK_BYTES = 32768,
    AUDIO_TASK_PRIORITY = 3,
};

extern const uint8_t sunny_begin[] __asm__("_binary_pond_sunny_mp3_start");
extern const uint8_t sunny_end[] __asm__("_binary_pond_sunny_mp3_end");
extern const uint8_t rain_begin[] __asm__("_binary_pond_rain_mp3_start");
extern const uint8_t rain_end[] __asm__("_binary_pond_rain_mp3_end");

typedef struct {
    const uint8_t *begin;
    const uint8_t *end;
} pond_audio_clip_t;

static const char *const TAG = "pond_audio";
static const pond_audio_clip_t clips[] = {
    {sunny_begin, sunny_end},
    {rain_begin, rain_end},
};

static atomic_bool worker;
static atomic_bool rain_requested;
static atomic_int requested_volume = DEFAULT_VOLUME;
static atomic_int applied_volume = -1;
static atomic_int volume_error;
static atomic_int active_clip = -1;
static atomic_int audio_result = ESP_ERR_INVALID_STATE;
static atomic_uint blocks_written;
static atomic_uint loops_completed;
static atomic_uint mode_switches;
static atomic_uint max_decode_us;
static atomic_uint stack_low;

static void stereo_frame(const int16_t *mono, int16_t *stereo, int samples, bool fade_in, bool fade_out)
{
    for (int i = 0; i < samples; ++i) {
        int sample = mono[i];
        if (fade_in) { sample = sample * i / samples; }
        if (fade_out) { sample = sample * (samples - 1 - i) / samples; }
        stereo[i * OUTPUT_CHANNELS] = sample;
        stereo[i * OUTPUT_CHANNELS + 1] = sample;
    }
}

static void update_volume(esp_codec_dev_handle_t speaker)
{
    const int target = atomic_load(&requested_volume);
    const int previous = atomic_load(&applied_volume);
    const int last_error = atomic_load(&volume_error);
    if (target == previous && !last_error) { return; }

    int status;
    if (target == 0) {
        status = esp_codec_dev_set_out_mute(speaker, true);
    } else {
        status = esp_codec_dev_set_out_vol(speaker, target);
        if (!status && (previous <= 0 || last_error)) { status = esp_codec_dev_set_out_mute(speaker, false); }
    }

    if (!status) { atomic_store(&applied_volume, target); }
    atomic_store(&volume_error, status);
    if (status && status != last_error) {
        ESP_LOGW(TAG, "Unable to apply volume %d: %d", target, status);
    }
}

static int play(esp_codec_dev_handle_t speaker)
{
    static mp3dec_t decoder;
    static int16_t mono[MINIMP3_MAX_SAMPLES_PER_FRAME];
    static int16_t stereo[MINIMP3_MAX_SAMPLES_PER_FRAME * OUTPUT_CHANNELS];

    bool rain = atomic_load(&rain_requested);
    bool fade_in = true;
    const uint8_t *position = clips[rain].begin;
    mp3dec_init(&decoder);

    for (;;) {
        const size_t bytes_left = (uintptr_t)clips[rain].end - (uintptr_t)position;
        mp3dec_frame_info_t frame = {0};
        const int64_t started = esp_timer_get_time();
        const int samples = mp3dec_decode_frame(&decoder, position, (int)bytes_left, mono, &frame);
        const unsigned decode_us = (unsigned)(esp_timer_get_time() - started);
        if (decode_us > atomic_load(&max_decode_us)) { atomic_store(&max_decode_us, decode_us); }
        if (frame.frame_bytes <= 0 || (size_t)frame.frame_bytes > bytes_left ||
            (samples && (frame.hz != SAMPLE_RATE || frame.channels != 1))) {
            ESP_LOGE(TAG, "Invalid MP3 frame: rain=%d bytes=%u samples=%d rate=%d channels=%d",
                     rain, (unsigned)bytes_left, samples, frame.hz, frame.channels);
            return ESP_ERR_INVALID_RESPONSE;
        }

        position += frame.frame_bytes;
        const bool next_rain = atomic_load(&rain_requested);
        const bool switching = rain != next_rain;
        const bool end = position == clips[rain].end;
        if (samples) {
            update_volume(speaker);
            stereo_frame(mono, stereo, samples, fade_in, switching || end);
            const int status = esp_codec_dev_write(speaker, stereo, samples * OUTPUT_CHANNELS * sizeof(*stereo));
            if (status) { return status; }

            atomic_store(&active_clip, (int)rain);
            atomic_store(&audio_result, ESP_OK);
            const unsigned blocks = atomic_fetch_add(&blocks_written, 1) + 1;
            if (blocks == 1 || (blocks & 127u) == 0) {
                atomic_store(&stack_low, uxTaskGetStackHighWaterMark(NULL));
            }
            fade_in = false;
        }

        if (switching || end) {
            if (switching) { atomic_fetch_add(&mode_switches, 1); }
            else { atomic_fetch_add(&loops_completed, 1); }
            rain = next_rain;
            position = clips[rain].begin;
            fade_in = true;
            mp3dec_init(&decoder);
        }
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    esp_codec_dev_handle_t speaker = bsp_audio_codec_speaker_init();
    esp_codec_dev_sample_info_t format = {
        .sample_rate = SAMPLE_RATE,
        .channel = OUTPUT_CHANNELS,
        .bits_per_sample = 16,
    };

    int status = speaker ? esp_codec_dev_open(speaker, &format) : ESP_FAIL;
    if (!status) {
        status = play(speaker);
        (void)esp_codec_dev_set_out_mute(speaker, true);
        (void)esp_codec_dev_close(speaker);
    }

    atomic_store(&audio_result, status);
    atomic_store(&active_clip, -1);
    atomic_store(&applied_volume, -1);
    atomic_store(&worker, false);
    ESP_LOGE(TAG, "Speaker playback stopped: %d", status);
    vTaskDelete(NULL);
}

void pond_audio_start(void)
{
    if (atomic_exchange(&worker, true)) { return; }
    if (xTaskCreate(audio_task, "pond_audio", AUDIO_STACK_BYTES, NULL, AUDIO_TASK_PRIORITY, NULL) != pdPASS) {
        atomic_store(&audio_result, ESP_ERR_NO_MEM);
        atomic_store(&worker, false);
        ESP_LOGE(TAG, "Unable to allocate the audio task");
    }
}

void pond_audio_set_rain(bool rain)
{
    atomic_store(&rain_requested, rain);
}

void pond_audio_adjust_volume(int delta)
{
    if (delta > MAX_VOLUME) { delta = MAX_VOLUME; }
    if (delta < -MAX_VOLUME) { delta = -MAX_VOLUME; }

    int previous = atomic_load(&requested_volume);
    int target;
    do {
        target = previous + delta;
        if (target < 0) { target = 0; }
        if (target > MAX_VOLUME) { target = MAX_VOLUME; }
    } while (!atomic_compare_exchange_weak(&requested_volume, &previous, target));
}

unsigned pond_audio_volume(void)
{
    return (unsigned)atomic_load(&requested_volume);
}

void pond_audio_status(void)
{
    printf("POND_AUDIO worker=%u codec=%d requested_rain=%u active=%d rate=%u volume=%u blocks=%u loops=%u switches=%u decode_us_peak=%u stack_min=%u volume_applied=%d volume_error=%d\n",
           (unsigned)atomic_load(&worker), atomic_load(&audio_result), (unsigned)atomic_load(&rain_requested),
           atomic_load(&active_clip), SAMPLE_RATE, pond_audio_volume(), atomic_load(&blocks_written),
           atomic_load(&loops_completed), atomic_load(&mode_switches), atomic_load(&max_decode_us),
           atomic_load(&stack_low), atomic_load(&applied_volume), atomic_load(&volume_error));
}
