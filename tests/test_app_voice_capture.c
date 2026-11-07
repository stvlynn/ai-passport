#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "voice_test_stubs.h"
#include "../main/app_audio.c"

static bool locked;
static unsigned reads, read_fail;
static int64_t clock_us;
static bool take_fail;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks) {
    (void)sem; (void)ticks;
    if (locked || take_fail) return 0;
    locked = true; return 1;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) { (void)sem; assert(locked); locked = false; return 1; }
QueueHandle_t xQueueCreate(unsigned count, unsigned size) { (void)count; (void)size; return (void *)2; }
BaseType_t xQueueReceive(QueueHandle_t queue, void *value, TickType_t ticks) { (void)queue; (void)value; (void)ticks; return 0; }
BaseType_t xQueueSend(QueueHandle_t queue, const void *value, TickType_t ticks) { (void)queue; (void)value; (void)ticks; return 1; }
BaseType_t xQueueReset(QueueHandle_t queue) { (void)queue; return 1; }
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *task) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority; (void)task; return pdPASS;
}
void vTaskDelay(TickType_t ticks) { (void)ticks; }
int64_t esp_timer_get_time(void) { return clock_us; }
esp_err_t bsp_audio_set_format(uint32_t rate, uint8_t bits, uint8_t channels) { assert(rate == 16000 && bits == 16 && channels == 1); return ESP_OK; }
void bsp_audio_set_volume(uint8_t volume) { (void)volume; }
esp_err_t bsp_audio_read(void *pcm, size_t bytes) {
    assert(locked && bytes == 640);
    reads++;
    clock_us += 20000;
    memset(pcm, 0, bytes);
    return reads == read_fail ? ESP_FAIL : ESP_OK;
}
esp_err_t bsp_audio_write(const void *pcm, size_t bytes) { (void)pcm; (void)bytes; return ESP_OK; }
static void reset(void) { reads = read_fail = 0; clock_us = 0; assert(!locked); }
static size_t run(unsigned steps) {
    for (unsigned i = 0; i < steps && s_rec_on; i++) rec_step();
    return app_audio_rec_bytes(NULL, NULL);
}
int main(void) {
    static uint8_t buf[VOICE_DATA_BYTES];
    bool done; esp_err_t err;
    assert(!app_audio_rec_start(buf, sizeof(buf)));            // codec not started
    assert(app_audio_start(true, NULL) == ESP_OK);
    assert(!app_audio_rec_start(NULL, sizeof(buf)));
    // Normal turn: the first stale chunks are discarded, each 20 ms becomes 80 bytes.
    assert(app_audio_rec_start(buf, sizeof(buf)));
    assert(!app_audio_rec_start(buf, sizeof(buf)));            // one recording at a time
    assert(run(REC_DISCARD_CHUNKS + 10) == 800);
    app_audio_rec_bytes(&done, NULL);
    assert(!done && !locked);
    app_audio_rec_stop();
    run(1);
    assert(app_audio_rec_bytes(&done, &err) == 800 && done && err == ESP_OK);
    // Auto-stop when the five-second buffer is full.
    reset();
    assert(app_audio_rec_start(buf, sizeof(buf)));
    assert(run(1000) == VOICE_DATA_BYTES);
    app_audio_rec_bytes(&done, &err);
    assert(done && err == ESP_OK && reads == REC_DISCARD_CHUNKS + 250);
    // A microphone error ends the recording with the error.
    reset(); read_fail = 6;
    assert(app_audio_rec_start(buf, sizeof(buf)));
    run(100);
    assert(app_audio_rec_bytes(&done, &err) == 2 * 80 && done && err == ESP_FAIL && !locked);
    // Lock contention only delays a step.
    reset(); take_fail = true;
    assert(app_audio_rec_start(buf, sizeof(buf)));
    run(5);
    assert(!reads && !s_rec_done);
    take_fail = false; app_audio_rec_stop(); run(1);
    assert(s_rec_done);
    puts("app_voice_capture: PASS");
    return 0;
}
