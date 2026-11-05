// main/app_audio.c —— 见 app_audio.h。
#include "app_audio.h"

#include "bsp_audio.h"
#include "voice_codec.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "audio";

#define CHUNK_SAMPLES   320      // 20ms @ 16kHz
#define AMPLITUDE       5200
#define VOLUME_PERCENT  70
#define CLAP_THRESHOLD  14000

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_io_lock;
static volatile bool s_enabled;          // 默认静音,由设置决定
static volatile bool s_listening;
static bool s_audio_ok;
static app_audio_clap_cb_t s_on_clap;
static int16_t s_buf[CHUNK_SAMPLES];
static volatile bool s_rec_on;
static void rec_step(void);

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
        if (s_rec_on) {
            rec_step();
            continue;
        }
        TickType_t wait = s_listening ? 0 : portMAX_DELAY;
        if (xQueueReceive(s_queue, &id, wait) == pdTRUE) {
            if (s_enabled && id != SOUND_NONE && xSemaphoreTake(s_io_lock, 0) == pdTRUE) {
                play(id);
                xSemaphoreGive(s_io_lock);
            }
            // 自己的声音会被麦克风听到:播放后重置检测器。
            clap_detector_init(&clap, CLAP_THRESHOLD);
            continue;
        }
        if (!s_listening) continue;
        if (xSemaphoreTake(s_io_lock, pdMS_TO_TICKS(20)) != pdTRUE) continue;
        esp_err_t read_err = bsp_audio_read(s_buf, sizeof(s_buf));
        if (read_err != ESP_OK) {
            xSemaphoreGive(s_io_lock);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int64_t now_ms = esp_timer_get_time() / 1000;
        bool detected = clap_detector_feed(&clap, s_buf, CHUNK_SAMPLES, now_ms);
        xSemaphoreGive(s_io_lock);
        if (detected && s_on_clap) {
            ESP_LOGI(TAG, "检测到拍手两下");
            s_on_clap();
        }
    }
}

esp_err_t app_audio_start(bool audio_ok, app_audio_clap_cb_t on_clap) {
    s_audio_ok = audio_ok;
    s_on_clap = on_clap;
    s_io_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(4, sizeof(sound_id_t));
    if (!s_queue || !s_io_lock) return ESP_ERR_NO_MEM;
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


// ---------------------------------------------------------------------------
// Push-to-talk recorder. The audio task fills the caller's buffer with 8 kHz
// IMA ADPCM; a worker may upload the bytes that are already published.
// ---------------------------------------------------------------------------

#define REC_DISCARD_CHUNKS 3      // stale DMA samples from before the press

static uint8_t *s_rec_buf;
static size_t s_rec_cap;
static volatile size_t s_rec_len;
static volatile bool s_rec_stop, s_rec_done;
static volatile esp_err_t s_rec_err;
static unsigned s_rec_skip;
static voice_enc_t s_rec_enc;

static void rec_finish(esp_err_t err) {
    s_rec_err = err;
    s_rec_on = false;
    s_rec_done = true;
}

// One 20 ms step; called from the audio task with the I/O lock free.
static void rec_step(void) {
    if (s_rec_stop || s_rec_len + CHUNK_SAMPLES / 4 > s_rec_cap) {
        rec_finish(ESP_OK);
        return;
    }
    if (xSemaphoreTake(s_io_lock, pdMS_TO_TICKS(20)) != pdTRUE) return;
    esp_err_t err = bsp_audio_read(s_buf, sizeof(s_buf));
    xSemaphoreGive(s_io_lock);
    if (err != ESP_OK) {
        rec_finish(err);
        return;
    }
    if (s_rec_skip) {
        s_rec_skip--;
        return;
    }
    size_t n = voice_enc_chunk(&s_rec_enc, s_buf, CHUNK_SAMPLES, s_rec_buf + s_rec_len);
    s_rec_len += n;               // publish only after the bytes are written
}

bool app_audio_rec_start(uint8_t *buf, size_t cap) {
    if (!s_audio_ok || !buf || cap < CHUNK_SAMPLES / 4 || s_rec_on) return false;
    s_rec_buf = buf;
    s_rec_cap = cap;
    s_rec_len = 0;
    s_rec_err = ESP_OK;
    s_rec_skip = REC_DISCARD_CHUNKS;
    s_rec_stop = false;
    s_rec_done = false;
    voice_enc_init(&s_rec_enc);
    s_rec_on = true;
    sound_id_t none = SOUND_NONE;
    (void)xQueueSend(s_queue, &none, 0);    // wake the task
    return true;
}

void app_audio_rec_stop(void) {
    s_rec_stop = true;
}

size_t app_audio_rec_bytes(bool *done, esp_err_t *err) {
    bool finished = s_rec_done;
    if (done) *done = finished;
    if (err) *err = finished ? s_rec_err : ESP_OK;
    return s_rec_len;
}
