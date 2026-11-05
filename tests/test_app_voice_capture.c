#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "voice_test_stubs.h"
#include "../main/app_audio.c"

static bool locked;
static unsigned reads, read_fail, chunks, sink_fail;
static int64_t clock_us;
static unsigned sink_delay;
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
static esp_err_t sink(const void *pcm, size_t bytes, void *arg) {
    (void)pcm; (void)arg;
    assert(locked && bytes == 640);
    chunks++;
    clock_us += sink_delay;
    return chunks == sink_fail ? ESP_FAIL : ESP_OK;
}
static void reset(void) { reads = chunks = read_fail = sink_fail = sink_delay = 0; clock_us = 0; assert(!locked); }
int main(void) {
    assert(app_audio_capture(sink, NULL, 5) == ESP_ERR_INVALID_STATE);
    assert(app_audio_start(true, NULL) == ESP_OK);
    assert(app_audio_capture(NULL, NULL, 5) == ESP_ERR_INVALID_ARG);
    assert(app_audio_capture(sink, NULL, 6) == ESP_ERR_INVALID_ARG);
    assert(app_audio_capture(sink, NULL, 5) == ESP_OK);
    assert(chunks == 250 && chunks * 640 == 160000 && reads == chunks + 8 && !locked);
    reset(); read_fail = 12;
    assert(app_audio_capture(sink, NULL, 5) == ESP_FAIL);
    assert(chunks == 3 && !locked);
    reset(); read_fail = 1;
    assert(app_audio_capture(sink, NULL, 5) == ESP_FAIL && !chunks && !locked);
    reset(); sink_fail = 9;
    assert(app_audio_capture(sink, NULL, 5) == ESP_FAIL && chunks == 9 && !locked);
    reset(); sink_delay = 1000000;
    assert(app_audio_capture(sink, NULL, 5) == ESP_ERR_TIMEOUT && chunks < 250 && !locked);
    reset(); take_fail = true;
    assert(app_audio_capture(sink, NULL, 5) == ESP_ERR_TIMEOUT && !reads);
    puts("app_voice_capture: PASS");
    return 0;
}
