// main/app_audio.c —— 见 app_audio.h。
#include "app_audio.h"

#include "bsp_audio.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "audio";

#define CHUNK_SAMPLES   320      // 20ms @ 16kHz
#define AMPLITUDE       5200
#define VOLUME_PERCENT  70
#define CLAP_THRESHOLD  14000

static QueueHandle_t s_queue;
static volatile bool s_enabled;          // 默认静音,由设置决定
static volatile bool s_listening;
static bool s_audio_ok;
static app_audio_clap_cb_t s_on_clap;
static int16_t s_buf[CHUNK_SAMPLES];

static void play(sound_id_t id) {
    sound_player_t p;
    sound_player_start(&p, id, AMPLITUDE);
    size_t n;
    while ((n = sound_player_render(&p, s_buf, CHUNK_SAMPLES)) > 0) {
        if (bsp_audio_write(s_buf, n * sizeof(int16_t)) != ESP_OK) break;
    }
    // 补一段静音把 DMA 中残留的最后一块推出去,避免尾音被截断或重复。
    for (size_t i = 0; i < CHUNK_SAMPLES; i++) s_buf[i] = 0;
    bsp_audio_write(s_buf, sizeof(s_buf));
}

static void audio_task(void *arg) {
    (void)arg;
    clap_detector_t clap;
    clap_detector_init(&clap, CLAP_THRESHOLD);
    for (;;) {
        sound_id_t id;
        TickType_t wait = s_listening ? 0 : portMAX_DELAY;
        if (xQueueReceive(s_queue, &id, wait) == pdTRUE) {
            if (s_enabled && id != SOUND_NONE) play(id);
            // 自己的声音会被麦克风听到:播放后重置检测器。
            clap_detector_init(&clap, CLAP_THRESHOLD);
            continue;
        }
        if (!s_listening) continue;
        if (bsp_audio_read(s_buf, sizeof(s_buf)) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (clap_detector_feed(&clap, s_buf, CHUNK_SAMPLES, now_ms) && s_on_clap) {
            ESP_LOGI(TAG, "检测到拍手两下");
            s_on_clap();
        }
    }
}

esp_err_t app_audio_start(bool audio_ok, app_audio_clap_cb_t on_clap) {
    s_audio_ok = audio_ok;
    s_on_clap = on_clap;
    s_queue = xQueueCreate(4, sizeof(sound_id_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (!audio_ok) return ESP_OK;
    esp_err_t err = bsp_audio_set_format(SOUND_RATE_HZ, 16, 1);
    if (err != ESP_OK) {
        s_audio_ok = false;
        return err;
    }
    bsp_audio_set_volume(VOLUME_PERCENT);
    if (xTaskCreate(audio_task, "app_audio", 3072, NULL, 4, NULL) != pdPASS) {
        s_audio_ok = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void app_audio_play(sound_id_t id) {
    if (!s_audio_ok || !s_enabled || id == SOUND_NONE) return;
    (void)xQueueSend(s_queue, &id, 0);
}

void app_audio_set_enabled(bool sound_on) {
    s_enabled = sound_on;
}

void app_audio_set_listening(bool listen) {
    s_listening = listen && s_audio_ok;
    if (s_listening && s_queue) {
        // 唤醒阻塞在 portMAX_DELAY 上的任务,让它进入监听循环。
        sound_id_t none = SOUND_NONE;
        (void)xQueueSend(s_queue, &none, 0);
    }
}
