#include "pond_audio.h"
#include "pond_display.h"
#include "pond_scene.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/lp_system_reg.h"
#include "soc/soc.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"
#include "tinyusb_default_config.h"

static const char *TAG = "pixel_koi";
static atomic_uint frames, late_frames;
static atomic_int boot_check;

static uint32_t millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void status(void)
{
    const esp_partition_t *part = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (part) { (void)esp_ota_get_state_partition(part, &state); }
    printf("STATUS project=pixel_koi_pond version=%s partition=%s ota_state=%lu "
           "weather=%s uptime_ms=%lu frames=%u late=%u fish_visible=%u startup=%s\n",
           esp_app_get_description()->version, part ? part->label : "unknown", (unsigned long)state,
           pond_scene_is_rain() ? "rain" : "sunny", (unsigned long)millis(),
           atomic_load(&frames), atomic_load(&late_frames), pond_scene_visible_fish(),
           esp_err_to_name(atomic_load(&boot_check)));
}

static void command(const char *line)
{
    esp_err_t result = ESP_OK;
    if (!strcmp(line, "status")) { status(); }
    else if (!strcmp(line, "audio_status")) { pond_audio_status(); }
    else if (!strcmp(line, "pond_fps")) { pond_display_status(); }
    else if (!strcmp(line, "scene rain")) { pond_scene_request_weather(true); }
    else if (!strcmp(line, "scene sunny")) { pond_scene_request_weather(false); }
    else if (!strcmp(line, "download")) {
        puts("Entering ROM download mode");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(200));
        REG_SET_BIT(LP_SYSTEM_REG_SYS_CTRL_REG, LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT);
        esp_restart();
    } else if (!strcmp(line, "restore")) {
        const esp_partition_t *running = esp_ota_get_running_partition();
        const esp_partition_t *platform = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
        result = platform && platform != running
            ? esp_ota_set_boot_partition(platform) : ESP_ERR_INVALID_STATE;
        if (result == ESP_OK) {
            puts("Restoring the application platform");
            fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        }
    } else if (!strcmp(line, "help")) {
        puts("status | audio_status | pond_fps | scene sunny | scene rain | restore | download");
    } else { result = ESP_ERR_NOT_FOUND; }
    printf("result: %s\n", esp_err_to_name(result));
    fflush(stdout);
}

static void console(void *argument)
{
    (void)argument;
    char line[80];
    size_t count = 0;
    for (;;) {
        const int c = getchar();
        if (c == EOF) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
        } else if (c == '\n' || c == '\r') {
            if (count) {
                line[count] = '\0';
                command(line);
                count = 0;
            }
        } else if (count + 1 < sizeof(line)) {
            line[count++] = (char)c;
        }
    }
}

void app_main(void)
{
    atomic_store(&boot_check, ESP_ERR_NOT_FINISHED);
    const tinyusb_config_t usb = TINYUSB_DEFAULT_CONFIG();
    esp_err_t result = tinyusb_driver_install(&usb);
    if (result == ESP_OK) {
        const tinyusb_config_cdcacm_t cdc = {.cdc_port = TINYUSB_CDC_ACM_0};
        result = tinyusb_cdcacm_init(&cdc);
        if (result == ESP_OK) { result = tinyusb_console_init(TINYUSB_CDC_ACM_0); }
    }
    if (result == ESP_OK) { xTaskCreate(console, "pond_console", 4096, NULL, 2, NULL); }
    else { ESP_LOGW(TAG, "USB console: %s", esp_err_to_name(result)); }

    result = pond_display_start();
    if (result == ESP_OK && !pond_scene_frame(millis())) { result = ESP_FAIL; }
    if (result == ESP_OK) {
        const uint32_t wait_started = millis();
        while (!pond_display_frames() && millis() - wait_started < 1600) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!pond_display_frames()) { result = ESP_ERR_TIMEOUT; }
    }
    atomic_store(&boot_check, result);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Pond startup: %s", esp_err_to_name(result));
        (void)esp_ota_mark_app_invalid_rollback_and_reboot();
        return;
    }

    pond_audio_start();
    result = esp_ota_mark_app_valid_cancel_rollback();
    if (result != ESP_OK) { ESP_LOGW(TAG, "OTA confirmation: %s", esp_err_to_name(result)); }

    TickType_t next = xTaskGetTickCount();
    unsigned cadence = 0;
    for (;;) {
        const uint32_t now = millis();
        int x = 0, y = 0;
        const bool down = pond_display_touch(&x, &y);
        pond_scene_touch(down, x, y, now);
        if (pond_scene_frame(now)) { atomic_fetch_add(&frames, 1); }
        if (!xTaskDelayUntil(&next, pdMS_TO_TICKS(cadence == 2 ? 16 : 17))) {
            atomic_fetch_add(&late_frames, 1);
            next = xTaskGetTickCount();
        }
        cadence = (cadence + 1) % 3;
    }
}
