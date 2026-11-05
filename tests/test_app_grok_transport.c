#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "voice_test_stubs.h"
#include "../main/app_grok.c"

static unsigned notifications, writes, bytes, closed, captured, delivered, statuses;
static bool fail_open, fail_write, cancel_write, reject_event;
static int http_status;
static const char *http_body;
static size_t read_offset;
static int content_length;
static char request_url[600], auth[220], content_type[100];
static grok_config_t test_config = { .url = "https://example.com", .token = "connector-secret", .bot = "Lynn" };
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks) { (void)sem; (void)ticks; return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) { (void)sem; return pdTRUE; }
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *task) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority; *task = (void *)2; return pdPASS;
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks) { (void)clear; (void)ticks; return 0; }
void xTaskNotifyGive(TaskHandle_t task) { (void)task; notifications++; }
net_status_t app_net_status(void) { net_status_t net = { .link = NET_LINK_ONLINE, .time_synced = true }; return net; }
bool app_settings_grok(grok_config_t *config) { *config = test_config; return true; }
void esp_crt_bundle_attach(void) {}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    assert(config->crt_bundle_attach && config->disable_auto_redirect);
    assert(config->timeout_ms == 5000 && config->buffer_size == 1024);
    strcpy(request_url, config->url); read_offset = 0;
    return (void *)3;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value) {
    (void)client;
    if (!strcmp(key, "Authorization")) strcpy(auth, value);
    if (!strcmp(key, "Content-Type")) strcpy(content_type, value);
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int length) { (void)client; content_length = length; return fail_open ? ESP_FAIL : ESP_OK; }
int esp_http_client_fetch_headers(esp_http_client_handle_t client) { (void)client; return strlen(http_body); }
int esp_http_client_get_status_code(esp_http_client_handle_t client) { (void)client; return http_status; }
int esp_http_client_write(esp_http_client_handle_t client, const char *body, int length) {
    (void)client; (void)body; writes++;
    if (fail_write) return -1;
    if (cancel_write) app_grok_set_active(false);
    // Exercise short TCP writes, which must not truncate a microphone chunk.
    int n = length > 160 ? 160 : length; bytes += n; return n;
}
int esp_http_client_read(esp_http_client_handle_t client, char *body, int length) {
    (void)client;
    size_t n = strlen(http_body) - read_offset;
    if (n > (size_t)length) n = length;
    memcpy(body, http_body + read_offset, n); read_offset += n; return n;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client) { (void)client; return read_offset == strlen(http_body); }
void esp_http_client_close(esp_http_client_handle_t client) { (void)client; closed++; }
void esp_http_client_cleanup(esp_http_client_handle_t client) { (void)client; }
esp_err_t app_audio_capture(app_audio_sink_t sink, void *arg, unsigned seconds) {
    assert(seconds == 5); captured++;
    char pcm[640] = {0};
    for (unsigned i = 0; i < 250; i++) { esp_err_t err = sink(pcm, sizeof(pcm), arg); if (err != ESP_OK) return err; }
    return ESP_OK;
}
static bool event_callback(const grok_event_t *event) { assert(event->present); delivered++; return !reject_event; }
static void status_callback(const char *text) { assert(text && text[0]); statuses++; }
static void reset(void) {
    fail_open = fail_write = cancel_write = reject_event = false;
    writes = bytes = closed = captured = delivered = statuses = 0;
    http_status = 202; http_body = "{\"request_id\":\"request-1\"}";
    app_grok_set_active(true);
}
int main(void) {
    assert(app_grok_start(event_callback, status_callback) == ESP_OK);
    assert(!app_grok_voice());
    reset();
    assert(upload(&test_config, s_generation));
    assert(content_length == GROK_VOICE_BYTES && bytes == GROK_VOICE_BYTES && writes == 1000 && closed == 1 && captured == 1);
    assert(!strcmp(request_url, "https://example.com/v1/voice?bot_id=Lynn"));
    assert(!strcmp(auth, "Bearer connector-secret"));
    assert(!strcmp(content_type, "audio/pcm;rate=16000;channels=1;format=s16le"));
    reset(); fail_open = true;
    assert(!upload(&test_config, s_generation) && !captured && closed == 1);
    reset(); fail_write = true;
    assert(!upload(&test_config, s_generation) && writes == 1 && closed == 1);
    reset(); cancel_write = true;
    assert(!upload(&test_config, s_generation) && bytes < GROK_VOICE_BYTES && closed == 1);
    reset(); http_status = 401;
    assert(!upload(&test_config, s_generation) && closed == 1);
    reset(); http_status = 200;
    http_body = "{\"cursor\":42,\"events\":[{\"id\":42,\"type\":\"text\",\"text\":\"Hi\"}]}";
    uint32_t cursor = 41;
    reject_event = true;
    assert(poll_events(&test_config, &cursor, s_generation) && cursor == 41 && delivered == 1);
    reject_event = false;
    assert(poll_events(&test_config, &cursor, s_generation) && cursor == 42 && delivered == 2);
    reset(); http_status = 200; http_body = "{\"cursor\":42,\"events\":[]}";
    cursor = 0;
    assert(poll_events(&test_config, &cursor, s_generation) && cursor == 42 && !delivered);
    reset(); http_status = 200;
    http_body = "{\"cursor\":42,\"events\":[{\"id\":42,\"type\":\"text\",\"text\":\"Old\"}]}";
    uint32_t stale_generation = s_generation;
    app_grok_set_active(false); app_grok_set_active(true);
    cursor = 41;
    assert(poll_events(&test_config, &cursor, stale_generation) && cursor == 41 && !delivered);
    static char large_body[GROK_RESPONSE_MAX + 5];
    memset(large_body, ' ', sizeof(large_body) - 1);
    large_body[sizeof(large_body) - 1] = 0;
    http_body = large_body;
    assert(!poll_events(&test_config, &cursor, s_generation) && cursor == 41);
    unsigned before = notifications;
    app_grok_set_active(true); app_grok_set_active(true);
    assert(notifications == before); // Model refreshes do not create a tight poll loop.
    assert(app_grok_voice() && !app_grok_voice());
    puts("app_grok_transport: PASS");
    return 0;
}
