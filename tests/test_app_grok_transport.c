#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "voice_test_stubs.h"
#include "../main/app_grok.c"

static unsigned notifications, writes, bytes, closed, captured, delivered, statuses, stops;
static uint32_t trailer;
static size_t rec_total; static int rec_err_code;
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
int64_t esp_timer_get_time(void) { return 0; }
void vTaskDelay(TickType_t ticks) { (void)ticks; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    assert(config->crt_bundle_attach && config->disable_auto_redirect);
    assert(config->timeout_ms == GROK_TIMEOUT_MS && config->buffer_size == 1024);
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
    (void)client; writes++;
    if (fail_write) return -1;
    if (cancel_write) app_grok_set_active(false);
    // Exercise short TCP writes, which must not truncate the stream.
    int n = length > 160 ? 160 : length;
    if (bytes >= VOICE_DATA_BYTES) for (int i = 0; i < n; i++) trailer |= (uint32_t)(uint8_t)body[i] << (8 * (bytes - VOICE_DATA_BYTES + i));
    bytes += n; return n;
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
// Fake recorder: produces 800 bytes per poll until rec_total, then reports done.
static size_t rec_ready;
bool app_audio_rec_start(uint8_t *buf, size_t cap) { (void)buf; assert(cap == VOICE_DATA_BYTES); captured++; rec_ready = 0; return true; }
void app_audio_rec_stop(void) { stops++; rec_total = rec_ready; }
size_t app_audio_rec_bytes(bool *done, esp_err_t *err) {
    if (rec_ready < rec_total) rec_ready += rec_ready + 800 > rec_total ? rec_total - rec_ready : 800;
    bool d = rec_ready >= rec_total;
    if (done) *done = d;
    if (err) *err = d ? rec_err_code : ESP_OK;
    return rec_ready;
}
static bool event_callback(const grok_event_t *event) { assert(event->present); delivered++; return !reject_event; }
static void status_callback(const char *text) { assert(text && text[0]); statuses++; }
static void reset(void) {
    fail_open = fail_write = cancel_write = reject_event = false;
    writes = bytes = closed = captured = delivered = statuses = stops = 0;
    trailer = 0; rec_ready = 0; rec_total = 8000; rec_err_code = ESP_OK;
    http_status = 202; http_body = "{\"request_id\":\"request-1\"}";
    app_grok_set_active(true);
}
int main(void) {
    static uint8_t buf[VOICE_DATA_BYTES];
    assert(app_grok_start(event_callback, status_callback) == ESP_OK);
    assert(!app_grok_ptt_down());          // inactive page
    reset();
    // 1 s of speech: streamed, zero-padded to a fixed body, sample count trailer.
    assert(upload(&test_config, s_generation, buf));
    assert(content_length == VOICE_BODY_BYTES && bytes == VOICE_BODY_BYTES && closed == 1);
    assert(trailer == 16000);
    assert(!strcmp(request_url, "https://example.com/v1/voice?bot_id=Lynn"));
    assert(!strcmp(auth, "Bearer connector-secret"));
    assert(!strcmp(content_type, VOICE_CONTENT_TYPE));
    reset(); rec_total = 1000;             // 0.25 s: too short, nothing completed
    assert(!upload(&test_config, s_generation, buf) && bytes == 1000 && closed == 1);
    reset(); rec_err_code = ESP_FAIL;
    assert(!upload(&test_config, s_generation, buf) && closed == 1);
    reset(); fail_open = true;             // connect failure stops the recording
    assert(!upload(&test_config, s_generation, buf) && stops == 1 && !writes && closed == 1);
    reset(); fail_write = true;
    assert(!upload(&test_config, s_generation, buf) && writes == 1 && stops == 1 && closed == 1);
    reset(); cancel_write = true;
    assert(!upload(&test_config, s_generation, buf) && bytes < VOICE_BODY_BYTES && closed == 1);
    reset(); http_status = 401;
    assert(!upload(&test_config, s_generation, buf) && closed == 1);
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
    // A tap (OK page flip) is dropped before any connection, without a status.
    reset();
    rec_total = 400;
    s_ptt_cancel = true;
    assert(upload(&test_config, s_generation, buf) && closed == 0 && writes == 0 && statuses == 0);
    s_ptt_cancel = false;
    assert(app_grok_ptt_down() && !app_grok_ptt_down());   // one turn at a time
    assert(!s_ptt_cancel);
    app_grok_ptt_up(true);
    assert(s_ptt_cancel);
    puts("app_grok_transport: PASS");
    return 0;
}
