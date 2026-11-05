#include "app_grok.h"
#include "app_audio.h"
#include "app_net.h"
#include "app_settings.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "voice_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SemaphoreHandle_t s_network, s_state;
static TaskHandle_t s_task;
static bool s_active, s_pending, s_busy;
static uint32_t s_generation;
static app_grok_event_cb_t s_event;
static app_grok_status_cb_t s_status;
static uint8_t *s_voice_buf;     // owned by the worker once a voice turn is pending

static const char *TAG = "grok";
#define GROK_TIMEOUT_MS 10000
#define GROK_POLL_MS 2000

void app_grok_network_lock(void) { xSemaphoreTake(s_network, portMAX_DELAY); }
void app_grok_network_unlock(void) { xSemaphoreGive(s_network); }

static bool current(uint32_t generation) {
    xSemaphoreTake(s_state, portMAX_DELAY);
    bool yes = s_active && generation == s_generation;
    xSemaphoreGive(s_state);
    return yes;
}

static void status(uint32_t generation, const char *text) {
    if (current(generation) && s_status) s_status(text);
}

static esp_http_client_handle_t client_new(const grok_config_t *config, bool voice, uint32_t after) {
    char url[GROK_URL_MAX + GROK_BOT_MAX * 3 + 80];
    if (!grok_endpoint(url, sizeof(url), config, voice, after)) return NULL;
    esp_http_client_config_t cfg = {
        .url = url, .method = voice ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = GROK_TIMEOUT_MS, .buffer_size = 1024, .buffer_size_tx = 1024,
        // Never forward the Bearer credential to a redirect destination.
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return NULL;
    char auth[GROK_TOKEN_MAX + 8];
    snprintf(auth, sizeof(auth), "Bearer %s", config->token);
    esp_http_client_set_header(client, "Authorization", auth);
    memset(auth, 0, sizeof(auth));
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    if (voice) esp_http_client_set_header(client, "Content-Type", VOICE_CONTENT_TYPE);
    return client;
}

static bool response(esp_http_client_handle_t client, int expected, char *body, size_t cap) {
    if (esp_http_client_fetch_headers(client) < 0 || esp_http_client_get_status_code(client) != expected) return false;
    size_t n = 0;
    while (n < cap - 1) {
        int got = esp_http_client_read(client, body + n, cap - 1 - n);
        if (got < 0) return false;
        if (!got) {
            body[n] = 0;
            return esp_http_client_is_complete_data_received(client);
        }
        n += got;
    }
    return false;
}

static bool write_all(esp_http_client_handle_t client, const uint8_t *p, size_t bytes) {
    while (bytes) {
        int n = esp_http_client_write(client, (const char *)p, (int)bytes);
        if (n <= 0) return false;
        p += n;
        bytes -= (size_t)n;
    }
    return true;
}

// Push-to-talk upload. Recording already runs into buf; the connection is made
// in parallel and buffered audio is streamed as soon as it is open. The body
// has a fixed length: VOICE_DATA_BYTES of ADPCM (zero-padded) + sample count.
static bool upload(const grok_config_t *config, uint32_t generation, uint8_t *buf) {
    esp_http_client_handle_t client = client_new(config, true, 0);
    if (!client) {
        app_audio_rec_stop();
        return false;
    }
    bool ok = false;
    int64_t t0 = esp_timer_get_time();
    (void)t0;
    esp_err_t err = esp_http_client_open(client, VOICE_BODY_BYTES);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "voice connect failed: %s", esp_err_to_name(err));
        app_audio_rec_stop();
        status(generation, "Connector unreachable. Hold OK to retry");
    } else {
        ESP_LOGI(TAG, "voice connected in %d ms", (int)((esp_timer_get_time() - t0) / 1000));
        size_t sent = 0;
        bool done = false, wrote = true;
        esp_err_t rec_err = ESP_OK;
        bool announced = false;
        for (;;) {
            size_t ready = app_audio_rec_bytes(&done, &rec_err);
            if (!current(generation)) { app_audio_rec_stop(); wrote = false; break; }
            if (!done && !announced) { status(generation, "Listening... release OK to send"); announced = true; }
            if (ready > sent) {
                if (!write_all(client, buf + sent, ready - sent)) { app_audio_rec_stop(); wrote = false; break; }
                sent = ready;
            } else if (done) {
                break;
            } else {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
        if (!done) {    // write failure/cancel: wait for the recorder to release buf
            while (!done) { vTaskDelay(pdMS_TO_TICKS(20)); app_audio_rec_bytes(&done, &rec_err); }
        }
        uint32_t samples = (uint32_t)sent * 2;
        if (wrote && rec_err != ESP_OK) {
            ESP_LOGW(TAG, "microphone read failed: %s", esp_err_to_name(rec_err));
            status(generation, "Microphone error. Hold OK to retry");
        } else if (wrote && samples < VOICE_MIN_SAMPLES) {
            status(generation, "Too short. Hold OK while you talk");
        } else if (wrote) {
            status(generation, "Sending...");
            memset(buf + sent, 0, VOICE_DATA_BYTES - sent);
            uint8_t trailer[4] = { (uint8_t)samples, (uint8_t)(samples >> 8), (uint8_t)(samples >> 16), (uint8_t)(samples >> 24) };
            char body[256];
            ok = write_all(client, buf + sent, VOICE_DATA_BYTES - sent) && write_all(client, trailer, 4) &&
                 response(client, 202, body, sizeof(body));
            if (!ok) ESP_LOGW(TAG, "voice upload rejected (HTTP %d)", esp_http_client_get_status_code(client));
            status(generation, ok ? "Sent. Waiting for Grok..." : "Voice failed. Hold OK to retry");
        } else {
            ESP_LOGW(TAG, "voice stream interrupted");
            status(generation, "Voice failed. Hold OK to retry");
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

static bool poll_events(const grok_config_t *config, uint32_t *cursor, uint32_t generation) {
    esp_http_client_handle_t client = client_new(config, false, *cursor);
    if (!client) return false;
    static char body[GROK_RESPONSE_MAX];
    grok_event_t event;
    esp_err_t err = esp_http_client_open(client, 0);
    bool ok = err == ESP_OK && response(client, 200, body, sizeof(body)) && grok_event_parse(body, *cursor, &event);
    if (!ok) ESP_LOGW(TAG, "event poll failed: %s status=%d", esp_err_to_name(err), esp_http_client_get_status_code(client));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (ok && current(generation)) {
        // Keep the cursor when the app queue is full; the same tool event is
        // offered again on the next poll instead of silently being dropped.
        if (!event.present || !s_event || s_event(&event)) *cursor = event.cursor;
    }
    return ok;
}

static void worker(void *arg) {
    (void)arg;
    grok_config_t config, previous = {0};
    uint32_t cursor = 0, generation = 0;
    bool failed = false;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(failed ? 5000 : GROK_POLL_MS));
        xSemaphoreTake(s_state, portMAX_DELAY);
        bool active = s_active, voice = s_pending;
        s_pending = false;
        uint32_t next_generation = s_generation;
        uint8_t *voice_buf = voice ? s_voice_buf : NULL;
        bool had_voice = voice;
        xSemaphoreGive(s_state);
        if (next_generation != generation) { cursor = 0; generation = next_generation; }
        if (!active) goto idle;
        if (!app_settings_grok(&config)) {
            if (voice) status(generation, "Set connector in Wi-Fi portal");
            goto idle;
        }
        if (memcmp(&config, &previous, sizeof(config))) { previous = config; cursor = 0; }
        net_status_t net = app_net_status();
        if (net.link != NET_LINK_ONLINE || !net.time_synced) {
            if (voice) status(generation, "Waiting for Wi-Fi and clock");
            goto idle;
        }
        app_grok_network_lock();
        failed = false;
        if (voice && current(generation)) {
            // Upload first: the recording is already running and the user is
            // waiting. The event cursor is established afterwards.
            failed = !upload(&config, generation, voice_buf);
            voice = false;
            voice_buf = NULL;
        }
        if (current(generation)) {
            // Establish a fresh cursor at boot/re-entry; never replay an old tool call.
            if (!cursor && !poll_events(&config, &cursor, generation)) failed = true;
            else if (cursor && !poll_events(&config, &cursor, generation)) failed = true;
            if (failed && !had_voice) status(generation, "Connector offline. Retrying...");
        }
        app_grok_network_unlock();
idle:
        memset(&config, 0, sizeof(config));
        if (voice_buf) {
            // Not uploaded (offline, unconfigured, page left): end the recording.
            app_audio_rec_stop();
            bool done = false;
            while (!done) { app_audio_rec_bytes(&done, NULL); if (!done) vTaskDelay(pdMS_TO_TICKS(20)); }
        }
        if (had_voice) {
            xSemaphoreTake(s_state, portMAX_DELAY);
            free(s_voice_buf);
            s_voice_buf = NULL;
            s_busy = false;
            xSemaphoreGive(s_state);
        }
    }
}

esp_err_t app_grok_start(app_grok_event_cb_t event_cb, app_grok_status_cb_t status_cb) {
    s_network = xSemaphoreCreateMutex();
    s_state = xSemaphoreCreateMutex();
    if (!s_network || !s_state) return ESP_ERR_NO_MEM;
    s_event = event_cb;
    s_status = status_cb;
    if (xTaskCreate(worker, "grok", 6144, NULL, 4, &s_task) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void app_grok_set_active(bool active) {
    if (!s_task) return;
    xSemaphoreTake(s_state, portMAX_DELAY);
    bool changed = active != s_active;
    if (changed) { s_active = active; s_generation++; }
    xSemaphoreGive(s_state);
    if (changed) xTaskNotifyGive(s_task);
}

bool app_grok_ptt_down(void) {
    if (!s_task) return false;
    xSemaphoreTake(s_state, portMAX_DELAY);
    bool ok = s_active && !s_busy;
    if (ok) {
        // Allocate only for the duration of one turn: 20 KB is too much to keep
        // reserved next to TLS on this no-PSRAM board.
        s_voice_buf = malloc(VOICE_DATA_BYTES);
        ok = s_voice_buf && app_audio_rec_start(s_voice_buf, VOICE_DATA_BYTES);
        if (ok) {
            s_pending = true;
            s_busy = true;
        } else {
            free(s_voice_buf);
            s_voice_buf = NULL;
        }
    }
    xSemaphoreGive(s_state);
    if (ok) xTaskNotifyGive(s_task);
    return ok;
}

void app_grok_ptt_up(void) {
    app_audio_rec_stop();
}
