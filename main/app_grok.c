#include "app_grok.h"
#include "app_audio.h"
#include "app_net.h"
#include "app_settings.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static SemaphoreHandle_t s_network, s_state;
static TaskHandle_t s_task;
static bool s_active, s_pending, s_busy;
static uint32_t s_generation;
static app_grok_event_cb_t s_event;
static app_grok_status_cb_t s_status;

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
        .timeout_ms = 5000, .buffer_size = 1024, .buffer_size_tx = 1024,
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
    if (voice) esp_http_client_set_header(client, "Content-Type", "audio/pcm;rate=16000;channels=1;format=s16le");
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

typedef struct { esp_http_client_handle_t client; uint32_t generation; } sink_t;
static esp_err_t audio_sink(const void *pcm, size_t bytes, void *arg) {
    sink_t *sink = arg;
    if (!current(sink->generation)) return ESP_ERR_INVALID_STATE;
    const char *p = pcm;
    while (bytes) {
        int n = esp_http_client_write(sink->client, p, bytes);
        if (n <= 0) return ESP_FAIL;
        p += n;
        bytes -= n;
    }
    return ESP_OK;
}

static bool upload(const grok_config_t *config, uint32_t generation) {
    esp_http_client_handle_t client = client_new(config, true, 0);
    if (!client) return false;
    bool ok = false;
    if (esp_http_client_open(client, GROK_VOICE_BYTES) == ESP_OK && current(generation)) {
        status(generation, "Listening: speak for 5 seconds");
        sink_t sink = { client, generation };
        if (app_audio_capture(audio_sink, &sink, GROK_VOICE_SECONDS) == ESP_OK) {
            char body[256];
            ok = response(client, 202, body, sizeof(body));
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
    bool ok = esp_http_client_open(client, 0) == ESP_OK &&
              response(client, 200, body, sizeof(body)) && grok_event_parse(body, *cursor, &event);
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
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(failed ? 5000 : 1000));
        xSemaphoreTake(s_state, portMAX_DELAY);
        bool active = s_active, voice = s_pending;
        s_pending = false;
        uint32_t next_generation = s_generation;
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
        if (current(generation)) {
            // Establish a fresh cursor before accepting audio. This discards
            // history at boot/re-entry and never replays an old tool call.
            if (!cursor && !poll_events(&config, &cursor, generation)) failed = true;
            if (voice && !failed && current(generation)) {
                status(generation, "Connecting microphone...");
                bool ok = upload(&config, generation);
                status(generation, ok ? "Sent. Waiting for Grok..." : "Voice failed. Press OK to retry");
                failed = !ok;
            }
            if (!failed && current(generation)) failed = !poll_events(&config, &cursor, generation);
            if (failed && !voice) status(generation, "Connector offline. Retrying...");
        }
        app_grok_network_unlock();
idle:
        memset(&config, 0, sizeof(config));
        xSemaphoreTake(s_state, portMAX_DELAY);
        if (voice) s_busy = false;
        xSemaphoreGive(s_state);
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

bool app_grok_voice(void) {
    if (!s_task) return false;
    xSemaphoreTake(s_state, portMAX_DELAY);
    bool ok = s_active && !s_busy;
    if (ok) { s_pending = true; s_busy = true; }
    xSemaphoreGive(s_state);
    if (ok) xTaskNotifyGive(s_task);
    return ok;
}
