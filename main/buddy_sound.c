#include "buddy_sound.h"
#include "buddy_types.h"
#include "esp_timer.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum { SOUND_CONNECTED = 1U, SOUND_COMPLETED = 2U, SOUND_TEST = 4U };
static TaskHandle_t s_worker;
static atomic_bool s_allowed;
static atomic_bool s_test_allowed;
static atomic_uint s_stage;
static atomic_uint s_play_count;
uint32_t buddy_sound_stage(void) { return atomic_load(&s_stage); }
uint32_t buddy_sound_play_count(void) { return atomic_load(&s_play_count); }
static const char *TAG = "buddy_sound";

enum { CUE_VOLUME = 92, AUDIO_WORKER_PRIORITY = 6 };
static atomic_uint s_volume = CUE_VOLUME;
static atomic_uint s_tone;

void buddy_sound_set_tone(uint8_t tone)
{
    atomic_store(&s_tone, tone < BUDDY_TONE_COUNT ? tone : BUDDY_TONE_NOTIFICATION_09);
}

void buddy_sound_set_volume_level(uint8_t level)
{
    static const uint8_t volumes[] = {78, 84, CUE_VOLUME, 96, 100};
    if (level < 1U || level > BUDDY_VOLUME_LEVEL_COUNT) level = BUDDY_DEFAULT_VOLUME_LEVEL;
    atomic_store(&s_volume, volumes[level - 1U]);
}

/* Embedded notification clips, 16 kHz signed PCM. */
extern const uint8_t notification_09_start[] asm("_binary_notification_09_pcm_start");
extern const uint8_t notification_09_end[] asm("_binary_notification_09_pcm_end");
extern const uint8_t notification_010_start[] asm("_binary_notification_010_pcm_start");
extern const uint8_t notification_010_end[] asm("_binary_notification_010_pcm_end");
extern const uint8_t charge_start[] asm("_binary_charge_pcm_start");
extern const uint8_t charge_end[] asm("_binary_charge_pcm_end");

static esp_err_t write_tone(void)
{
    int16_t pcm[160];
    static const struct { const uint8_t *start, *end; } samples[BUDDY_TONE_COUNT] = {
        [BUDDY_TONE_NOTIFICATION_09] = {notification_09_start, notification_09_end},
        [BUDDY_TONE_NOTIFICATION_010] = {notification_010_start, notification_010_end},
        [BUDDY_TONE_CHARGE] = {charge_start, charge_end},
    };
    unsigned selected = atomic_load(&s_tone);
    const uint8_t *audio = samples[selected].start;
    size_t frames = ((uintptr_t)samples[selected].end - (uintptr_t)audio) / sizeof(pcm[0]);
    for (size_t start = 0; start < frames; start += 160U) {
        size_t count = frames - start < 160U ? frames - start : 160U;
        memcpy(pcm, audio + start * sizeof(pcm[0]), count * sizeof(pcm[0]));
        esp_err_t err = bsp_audio_write(pcm, count * sizeof(pcm[0]));
        if (err != ESP_OK) return err;
    }
    memset(pcm, 0, sizeof(pcm));
    for (unsigned i = 0; i < 10U; ++i) {
        esp_err_t err = bsp_audio_write(pcm, sizeof(pcm));
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

static esp_err_t play_cue(const char *label, bool test)
{
    (void)label;
    if (!(test ? atomic_load(&s_test_allowed) : atomic_load(&s_allowed))) return ESP_OK;
    atomic_store(&s_stage, 3);
    bsp_audio_set_volume((uint8_t)atomic_load(&s_volume));
    atomic_store(&s_stage, 4);
    esp_err_t err = write_tone();
    vTaskDelay(pdMS_TO_TICKS(100));
    bsp_audio_set_volume(0);
    if (err == ESP_OK) {
        atomic_fetch_add(&s_play_count, 1);
        atomic_store(&s_stage, 5);
        ESP_LOGI(TAG, "%s chime played", label);
    }
    return err;
}

static void sound_worker(void *context)
{
    (void)context;
    bool initialized = false;
    bool failed = false;
    int64_t last_completion = -2000000;
    uint32_t alerts = 0;
    for (;;) {
        uint32_t pending = 0;
        (void)xTaskNotifyWait(0, UINT32_MAX, &pending, portMAX_DELAY);
        if (failed) continue;
        alerts |= pending & (SOUND_CONNECTED | SOUND_COMPLETED | SOUND_TEST);
        esp_err_t err = ESP_OK;
        if (!initialized) {
            atomic_store(&s_stage, 1);
            err = bsp_audio_init();
            atomic_store(&s_stage, 2);
            if (err == ESP_OK) err = bsp_audio_set_format(16000, 16, 1);
            initialized = err == ESP_OK;
        }
        uint32_t ready = alerts;
        alerts = 0;
        if (err == ESP_OK && (ready & SOUND_TEST)) {
            err = play_cue("Sound test", true);
        }
        if (err == ESP_OK && (ready & SOUND_COMPLETED) &&
            esp_timer_get_time() - last_completion >= 2000000) {
            err = play_cue("Completion", false);
            last_completion = esp_timer_get_time();
        } else if (err == ESP_OK && (ready & SOUND_CONNECTED)) {
            err = play_cue("Connection", false);
        }
        if (err != ESP_OK) {
            atomic_store(&s_stage, 100);
            failed = true;
            ESP_LOGW(TAG, "Audio unavailable: %s", esp_err_to_name(err));
        }
    }
}

static void notify_sound(uint32_t kind)
{
    if (s_worker == NULL && xTaskCreate(sound_worker, "buddy_sound", 6144, NULL,
                                       AUDIO_WORKER_PRIORITY, &s_worker) != pdPASS) {
        ESP_LOGW(TAG, "Unable to allocate audio worker");
        return;
    }
    xTaskNotify(s_worker, kind, eSetBits);
}

void buddy_sound_set_allowed(bool allowed) { atomic_store(&s_allowed, allowed); }
void buddy_sound_set_test_allowed(bool allowed) { atomic_store(&s_test_allowed, allowed); }
void buddy_sound_notify_test(void) { notify_sound(SOUND_TEST); }
void buddy_sound_notify(void) { notify_sound(SOUND_COMPLETED); }
void buddy_sound_notify_connected(void) { notify_sound(SOUND_CONNECTED); }
