// main/app_net.c —— 见 app_net.h。
#include "app_net.h"

#include "app_portal.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/ip4_addr.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "net";

#define BACKOFF_MIN_MS     2000
#define TRIAL_TIMEOUT_MS   20000
#define TRIAL_MAX_DISCONNECTS 3
#define BACKOFF_MAX_MS     60000
#define TIME_VALID_AFTER   1767225600   // 2026-01-01:早于此视为未校时

static SemaphoreHandle_t s_lock;
static net_status_t s_st;
static net_listener_t s_listener;
static esp_netif_t *s_sta;
static esp_netif_t *s_ap;
static esp_timer_handle_t s_retry_timer;
static esp_timer_handle_t s_trial_timer;
static uint32_t s_backoff_ms = BACKOFF_MIN_MS;
static bool s_reconnect_after_disconnect;
static bool s_sntp_started;

// 配网(SoftAP)
static volatile bool s_prov_active;
static volatile bool s_trial;            // 正在试连网页提交的新凭据
static uint8_t s_trial_disconnects;
static wifi_config_t s_prev_cfg;         // 试连前的凭据,失败时恢复
static char s_scan[APP_NET_SCAN_MAX][33];
static int s_scan_count;

static void notify(void) {
    if (s_listener) s_listener();
}

static void set_link(net_link_t link) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_st.link = link;
    xSemaphoreGive(s_lock);
    notify();
}

static void set_prov(net_prov_t prov) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_st.prov = prov;
    xSemaphoreGive(s_lock);
    notify();
}

static bool has_credentials(void) {
    wifi_config_t cfg = { 0 };
    return esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && cfg.sta.ssid[0] != '\0';
}

static void connect_now(void) {
    if (!has_credentials()) {
        set_link(NET_LINK_NO_CREDENTIALS);
        return;
    }
    wifi_config_t cfg = { 0 };
    esp_wifi_get_config(WIFI_IF_STA, &cfg);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_st.ssid, (const char *)cfg.sta.ssid, sizeof(s_st.ssid));
    xSemaphoreGive(s_lock);
    set_link(NET_LINK_CONNECTING);
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
    }
}

static void retry_cb(void *arg) {
    (void)arg;
    connect_now();
}

static void schedule_retry(void) {
    // 配网期间 STA 不做后台重连:频繁扫描/换信道会让手机连不稳热点。
    if (s_prov_active) return;
    if (!has_credentials()) {
        set_link(NET_LINK_NO_CREDENTIALS);
        return;
    }
    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, (uint64_t)s_backoff_ms * 1000);
    s_backoff_ms = s_backoff_ms * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : s_backoff_ms * 2;
}

static void time_sync_cb(struct timeval *tv) {
    (void)tv;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_st.time_synced = true;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "SNTP 校时完成");
    notify();
}

static void trial_fail(const char *why);

static void trial_timeout_cb(void *arg) {
    (void)arg;
    trial_fail("timeout");
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)base;
    switch (id) {
    case WIFI_EVENT_STA_START:
        connect_now();
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *ev = data;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_st.last_reason = (uint8_t)ev->reason;
        s_st.ip[0] = '\0';
        s_st.rssi = 0;
        xSemaphoreGive(s_lock);
        ESP_LOGW(TAG, "Wi-Fi 断开 reason=%u", ev->reason);
        if (s_reconnect_after_disconnect) {
            s_reconnect_after_disconnect = false;
            connect_now();
            return;
        }
        if (s_trial) {
            // 密码错误类原因立即判失败;其它原因(如信号弱)允许重试几次。
            bool auth = ev->reason == WIFI_REASON_AUTH_FAIL || ev->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                        ev->reason == WIFI_REASON_HANDSHAKE_TIMEOUT || ev->reason == WIFI_REASON_NO_AP_FOUND ||
                        ev->reason == WIFI_REASON_CONNECTION_FAIL;
            if (auth || ++s_trial_disconnects >= TRIAL_MAX_DISCONNECTS) {
                trial_fail("disconnect");
            } else {
                esp_wifi_connect();
            }
            return;
        }
        set_link(has_credentials() ? NET_LINK_OFFLINE : NET_LINK_NO_CREDENTIALS);
        schedule_retry();
        break;
    }
    case WIFI_EVENT_AP_STACONNECTED:
        if (s_st.prov == NET_PROV_AP_READY) set_prov(NET_PROV_CLIENT_CONNECTED);
        break;
    case WIFI_EVENT_AP_STADISCONNECTED:
        if (s_st.prov == NET_PROV_CLIENT_CONNECTED) set_prov(NET_PROV_AP_READY);
        break;
    default:
        break;
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)base;
    if (id != IP_EVENT_STA_GOT_IP) return;
    const ip_event_got_ip_t *ev = data;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_st.ip, sizeof(s_st.ip), IPSTR, IP2STR(&ev->ip_info.ip));
    xSemaphoreGive(s_lock);
    s_backoff_ms = BACKOFF_MIN_MS;
    ESP_LOGI(TAG, "已联网 IP=%s", s_st.ip);
    if (!s_sntp_started) {
        esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2,
            ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "pool.ntp.org"));
        cfg.sync_cb = time_sync_cb;
        if (esp_netif_sntp_init(&cfg) == ESP_OK) s_sntp_started = true;
    }
    if (s_trial) {
        s_trial = false;
        esp_timer_stop(s_trial_timer);
        ESP_LOGI(TAG, "配网:新 Wi-Fi 已连上");
        set_prov(NET_PROV_DONE);
    }
    set_link(NET_LINK_ONLINE);
}

esp_err_t app_net_init(net_listener_t listener) {
    s_listener = listener;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    setenv("TZ", CONFIG_ASTRA_TZ, 1);
    tzset();

    esp_err_t err = esp_netif_init();
    if (err == ESP_OK) err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    s_sta = esp_netif_create_default_wifi_sta();
    if (!s_sta) return ESP_ERR_NO_MEM;

    const esp_timer_create_args_t targs = { .callback = retry_cb, .name = "wifi_retry" };
    err = esp_timer_create(&targs, &s_retry_timer);
    if (err != ESP_OK) return err;
    const esp_timer_create_args_t trial = { .callback = trial_timeout_cb, .name = "wifi_trial" };
    err = esp_timer_create(&trial, &s_trial_timer);
    if (err != ESP_OK) return err;

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event, NULL, NULL));
    err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err == ESP_OK) esp_wifi_set_ps(WIFI_PS_MIN_MODEM);   // 与 BLE 共存时也要求开启 modem sleep
    uint8_t mac[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        // 路由器 MAC 白名单/静态 IP 绑定需要它;配网页与串口 status 会显示。
        snprintf(s_st.mac, sizeof(s_st.mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    return err;
}

net_status_t app_net_status(void) {
    if (!s_lock) return (net_status_t){ 0 };
    xSemaphoreTake(s_lock, portMAX_DELAY);
    net_status_t st = s_st;
    xSemaphoreGive(s_lock);
    if (st.link == NET_LINK_ONLINE) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) st.rssi = ap.rssi;
    }
    if (!st.time_synced && time(NULL) > TIME_VALID_AFTER) st.time_synced = true;
    return st;
}

esp_err_t app_net_set_credentials(const char *ssid, const char *password) {
    if (!ssid || !*ssid || strlen(ssid) >= 33) return ESP_ERR_INVALID_ARG;
    if (password && strlen(password) >= 64) return ESP_ERR_INVALID_ARG;
    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    if (password) strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));
    esp_wifi_disconnect();
    s_reconnect_after_disconnect = false;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) return err;
    esp_timer_stop(s_retry_timer);
    s_backoff_ms = BACKOFF_MIN_MS;
    connect_now();
    return ESP_OK;
}

esp_err_t app_net_clear_credentials(void) {
    esp_timer_stop(s_retry_timer);
    wifi_config_t empty = { 0 };
    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &empty);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_st.ssid[0] = '\0';
    s_st.ip[0] = '\0';
    xSemaphoreGive(s_lock);
    set_link(NET_LINK_NO_CREDENTIALS);
    return err;
}

// ---------------------------------------------------------------------------
// SoftAP 网页配网
// ---------------------------------------------------------------------------

// 试连失败:恢复原凭据并回到"等待提交"。在 Wi-Fi 事件任务或 esp_timer 任务里调用。
static void trial_fail(const char *why) {
    if (!s_trial) return;
    s_trial = false;
    esp_timer_stop(s_trial_timer);
    ESP_LOGW(TAG, "配网:新 Wi-Fi 连接失败(%s),恢复原凭据", why);
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &s_prev_cfg);
    set_prov(s_prov_active ? NET_PROV_WIFI_FAILED : NET_PROV_OFF);
    set_link(has_credentials() ? NET_LINK_OFFLINE : NET_LINK_NO_CREDENTIALS);
    if (has_credentials()) connect_now();
}

esp_err_t app_net_try_credentials(const char *ssid, const char *password) {
    if (!ssid || !*ssid || strlen(ssid) >= 33) return ESP_ERR_INVALID_ARG;
    if (password && strlen(password) >= 64) return ESP_ERR_INVALID_ARG;
    if (password && *password && strlen(password) < 8) return ESP_ERR_INVALID_ARG;   // WPA2 下限
    if (s_trial) return ESP_ERR_INVALID_STATE;

    wifi_config_t prev = { 0 };
    esp_wifi_get_config(WIFI_IF_STA, &prev);
    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    if (password) strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));

    esp_timer_stop(s_retry_timer);
    s_prev_cfg = prev;
    s_trial_disconnects = 0;
    s_trial = true;
    bool was_online = s_st.link == NET_LINK_ONLINE;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        s_trial = false;
        return err;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_st.ssid, ssid, sizeof(s_st.ssid));
    xSemaphoreGive(s_lock);
    set_prov(NET_PROV_WIFI_CONNECTING);
    set_link(NET_LINK_CONNECTING);
    esp_timer_start_once(s_trial_timer, (uint64_t)TRIAL_TIMEOUT_MS * 1000);
    if (was_online) {
        s_reconnect_after_disconnect = true;
        if (esp_wifi_disconnect() == ESP_OK) return ESP_OK;
        s_reconnect_after_disconnect = false;
    }
    esp_wifi_connect();
    return ESP_OK;
}

int app_net_scan_results(char ssids[][33], int max) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_scan_count < max ? s_scan_count : max;
    memcpy(ssids, s_scan, (size_t)n * 33);
    xSemaphoreGive(s_lock);
    return n;
}

static void scan_networks(void) {
    wifi_scan_config_t scan = { .show_hidden = false };
    if (esp_wifi_scan_start(&scan, true) != ESP_OK) return;
    uint16_t count = APP_NET_SCAN_MAX * 2;
    wifi_ap_record_t *rec = calloc(count, sizeof(*rec));
    if (!rec) {
        esp_wifi_clear_ap_list();
        return;
    }
    if (esp_wifi_scan_get_ap_records(&count, rec) == ESP_OK) {
        // 结果已按信号降序;去掉空名与重名(同名多个 AP)。
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_scan_count = 0;
        for (uint16_t i = 0; i < count && s_scan_count < APP_NET_SCAN_MAX; i++) {
            const char *name = (const char *)rec[i].ssid;
            if (!*name) continue;
            bool dup = false;
            for (int j = 0; j < s_scan_count && !dup; j++) dup = strcmp(s_scan[j], name) == 0;
            if (!dup) strlcpy(s_scan[s_scan_count++], name, 33);
        }
        xSemaphoreGive(s_lock);
    }
    free(rec);
}

esp_err_t app_net_provision_start(void) {
    if (s_prov_active) return ESP_OK;
    s_prov_active = true;
    set_prov(NET_PROV_STARTING);
    esp_timer_stop(s_retry_timer);

    // 未联网时 STA 可能正处于连接尝试中,扫描会失败:先停下来。
    if (s_st.link != NET_LINK_ONLINE) esp_wifi_disconnect();
    scan_networks();

    // 热点名取 MAC 末两字节,密码为随机 8 位数字(屏幕显示 + 二维码)。
    uint8_t mac[6] = { 0 };
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_st.ap_ssid, sizeof(s_st.ap_ssid), APP_NET_AP_PREFIX "%02X%02X", mac[4], mac[5]);
    snprintf(s_st.ap_pass, sizeof(s_st.ap_pass), "%08lu", (unsigned long)(esp_random() % 100000000UL));
    xSemaphoreGive(s_lock);

    if (!s_ap) s_ap = esp_netif_create_default_wifi_ap();
    wifi_config_t ap = { .ap = { .channel = 6, .max_connection = 1,
                                 .authmode = WIFI_AUTH_WPA2_PSK, .pmf_cfg = { .required = false } } };
    strlcpy((char *)ap.ap.ssid, s_st.ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = (uint8_t)strlen(s_st.ap_ssid);
    strlcpy((char *)ap.ap.password, s_st.ap_pass, sizeof(ap.ap.password));

    // 热点配置只放内存:不写进 NVS,也不覆盖已保存的 STA 凭据。
    esp_err_t err = s_ap ? ESP_OK : ESP_ERR_NO_MEM;
    if (err == ESP_OK) err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err == ESP_OK) err = app_portal_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "配网热点启动失败: %s", esp_err_to_name(err));
        app_net_provision_stop();
        set_prov(NET_PROV_FAILED);
        return err;
    }
    ESP_LOGI(TAG, "配网热点 %s 已开启,扫描到 %d 个网络", s_st.ap_ssid, s_scan_count);
    set_prov(NET_PROV_AP_READY);
    return ESP_OK;
}

void app_net_provision_stop(void) {
    app_portal_stop();
    if (s_trial) trial_fail("cancelled");
    esp_wifi_set_mode(WIFI_MODE_STA);
    bool was_active = s_prov_active;
    s_prov_active = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_st.ap_pass[0] = '\0';
    xSemaphoreGive(s_lock);
    if (was_active) set_prov(NET_PROV_OFF);
    if (s_st.link != NET_LINK_ONLINE && s_st.link != NET_LINK_CONNECTING) connect_now();
}
