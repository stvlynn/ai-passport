#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
static inline const char *esp_err_to_name(esp_err_t err) { (void)err; return "err"; }
typedef int BaseType_t;
typedef unsigned TickType_t;
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
QueueHandle_t xQueueCreate(unsigned count, unsigned size);
BaseType_t xQueueReceive(QueueHandle_t queue, void *value, TickType_t ticks);
BaseType_t xQueueSend(QueueHandle_t queue, const void *value, TickType_t ticks);
BaseType_t xQueueReset(QueueHandle_t queue);
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, TaskHandle_t *task);
void vTaskDelay(TickType_t ticks);
int64_t esp_timer_get_time(void);
esp_err_t bsp_audio_set_format(uint32_t rate, uint8_t bits, uint8_t channels);
void bsp_audio_set_volume(uint8_t volume);
esp_err_t bsp_audio_read(void *pcm, size_t bytes);
esp_err_t bsp_audio_write(const void *pcm, size_t bytes);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks);
void xTaskNotifyGive(TaskHandle_t task);
#define HTTP_METHOD_GET 0
#define HTTP_METHOD_POST 1
typedef void *esp_http_client_handle_t;
typedef struct {
    const char *url;
    int method;
    void (*crt_bundle_attach)(void);
    int timeout_ms, buffer_size, buffer_size_tx;
    bool disable_auto_redirect;
} esp_http_client_config_t;
void esp_crt_bundle_attach(void);
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value);
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int length);
int esp_http_client_fetch_headers(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
int esp_http_client_write(esp_http_client_handle_t client, const char *body, int length);
int esp_http_client_read(esp_http_client_handle_t client, char *body, int length);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client);
void esp_http_client_close(esp_http_client_handle_t client);
void esp_http_client_cleanup(esp_http_client_handle_t client);
