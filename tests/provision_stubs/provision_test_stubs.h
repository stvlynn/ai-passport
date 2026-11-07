#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_WIFI_CONN 0x3007
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERROR_CHECK(x) assert((x) == ESP_OK)
static inline void test_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define CONFIG_ASTRA_TZ "UTC0"
#define CONFIG_ASTRA_OFF_SECONDS 60
#define CONFIG_ASTRA_GH_TOKEN ""
static inline const char *esp_err_to_name(esp_err_t e) { (void)e; return "stub"; }
#undef strlcpy
#define strlcpy test_strlcpy
static inline size_t test_strlcpy(char *d, const char *s, size_t cap) {
    size_t n = strlen(s); if (cap) { size_t k = n < cap - 1 ? n : cap - 1; memcpy(d, s, k); d[k] = 0; } return n;
}
typedef struct { bool held; } *SemaphoreHandle_t;
typedef int BaseType_t;
typedef unsigned TickType_t;
typedef void *TaskHandle_t;
#define portMAX_DELAY 0xffffffffU
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return calloc(1, sizeof(struct { bool held; })); }
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) { return xSemaphoreCreateMutex(); }
static inline int xSemaphoreTake(SemaphoreHandle_t h, unsigned ticks) { (void)ticks; assert(h && !h->held); h->held = true; return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t h) { assert(h); h->held = false; return 1; }
static inline void vSemaphoreDelete(SemaphoreHandle_t h) { free(h); }
static inline void vTaskDelete(void *p) { (void)p; }
static inline int xTaskCreate(void (*fn)(void *), const char *n, unsigned stack, void *arg, unsigned pri, TaskHandle_t *t) {
    (void)fn; (void)n; (void)stack; (void)arg; (void)pri; *t = (void *)1; return pdPASS;
}
typedef const char *esp_event_base_t;
#define WIFI_EVENT "wifi"
#define IP_EVENT "ip"
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define WIFI_EVENT_AP_STACONNECTED 3
#define WIFI_EVENT_AP_STADISCONNECTED 4
#define IP_EVENT_STA_GOT_IP 5
#define WIFI_REASON_AUTH_FAIL 6
#define WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT 7
#define WIFI_REASON_HANDSHAKE_TIMEOUT 8
#define WIFI_REASON_NO_AP_FOUND 9
#define WIFI_REASON_CONNECTION_FAIL 10
typedef struct { unsigned reason; } wifi_event_sta_disconnected_t;
typedef struct { int ip; } esp_netif_t;
typedef struct { struct { unsigned ip; } ip_info; } ip_event_got_ip_t;
#define IPSTR "%u"
#define IP2STR(p) (*(p))
static inline esp_err_t esp_netif_init(void) { return ESP_OK; }
static inline esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
static inline esp_err_t esp_event_handler_instance_register(esp_event_base_t b, int32_t id, void (*fn)(void *, esp_event_base_t, int32_t, void *), void *arg, void *inst) {
    (void)b; (void)id; (void)fn; (void)arg; (void)inst; return ESP_OK;
}
static inline esp_netif_t *esp_netif_create_default_wifi_sta(void) { static esp_netif_t n; return &n; }
static inline esp_netif_t *esp_netif_create_default_wifi_ap(void) { return esp_netif_create_default_wifi_sta(); }
typedef struct { void (*sync_cb)(struct timeval *); } esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(...) ((esp_sntp_config_t){0})
static inline esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *c) { (void)c; return ESP_OK; }
static inline uint32_t esp_random(void) { return 12345678; }
typedef void *esp_timer_handle_t;
typedef struct { void (*callback)(void *); const char *name; } esp_timer_create_args_t;
static inline esp_err_t esp_timer_create(const esp_timer_create_args_t *a, esp_timer_handle_t *t) { (void)a; *t = (void *)1; return ESP_OK; }
static inline esp_err_t esp_timer_stop(esp_timer_handle_t t) { (void)t; return ESP_OK; }
static inline esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t us) { (void)t; (void)us; return ESP_OK; }
typedef struct { uint8_t ssid[32], password[64]; } wifi_sta_config_t;
typedef union {
    wifi_sta_config_t sta;
    struct { uint8_t ssid[32], password[64], ssid_len; int channel, max_connection, authmode; struct { bool required; } pmf_cfg; } ap;
} wifi_config_t;
typedef struct { int unused; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define WIFI_IF_STA 0
#define WIFI_IF_AP 1
#define WIFI_STORAGE_RAM 0
#define WIFI_MODE_STA 0
#define WIFI_MODE_APSTA 1
#define WIFI_PS_MIN_MODEM 0
#define WIFI_AUTH_WPA2_PSK 3
typedef struct { bool show_hidden; } wifi_scan_config_t;
typedef struct { uint8_t ssid[33]; int8_t rssi; } wifi_ap_record_t;
esp_err_t esp_wifi_get_config(int iface, wifi_config_t *cfg);
esp_err_t esp_wifi_set_config(int iface, const wifi_config_t *cfg);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
static inline esp_err_t esp_wifi_init(const wifi_init_config_t *c) { (void)c; return ESP_OK; }
static inline esp_err_t esp_wifi_set_storage(int s) { assert(s == WIFI_STORAGE_RAM); return ESP_OK; }
static inline esp_err_t esp_wifi_set_mode(int m) { (void)m; return ESP_OK; }
static inline esp_err_t esp_wifi_start(void) { return ESP_OK; }
static inline esp_err_t esp_wifi_set_ps(int m) { (void)m; return ESP_OK; }
static inline esp_err_t esp_wifi_get_mac(int i, uint8_t *m) { (void)i; memset(m, 1, 6); return ESP_OK; }
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *a);
static inline esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *s, bool b) { (void)s; (void)b; return ESP_OK; }
static inline esp_err_t esp_wifi_clear_ap_list(void) { return ESP_OK; }
static inline esp_err_t esp_wifi_scan_get_ap_records(uint16_t *n, wifi_ap_record_t *a) { (void)a; *n = 0; return ESP_OK; }
typedef int nvs_handle_t;
