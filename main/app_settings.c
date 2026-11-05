// main/app_settings.c —— 见 app_settings.h。
#include "app_settings.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include <string.h>

static const char *TAG = "settings";
static const char *NS = "astra";

static SemaphoreHandle_t s_lock;
// 提示音默认关闭(办公桌上的监控面板不应主动出声),用户可在设备页打开。
static app_prefs_t s_prefs = { .sound_on = false, .clap_wake = true, .brightness = 100,
                               .screen_off_min = CONFIG_ASTRA_OFF_SECONDS / 60 };
static bool s_nvs_ok;

static uint8_t get_u8(nvs_handle_t h, const char *key, uint8_t def) {
    uint8_t v;
    return nvs_get_u8(h, key, &v) == ESP_OK ? v : def;
}

esp_err_t app_settings_init(void) {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        // 不自动擦除:分区里可能还有用户的 Wi-Fi 凭据或其它数据。
        ESP_LOGE(TAG, "NVS 初始化失败: %s;设置使用默认值", esp_err_to_name(err));
        return err;
    }
    s_nvs_ok = true;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        s_prefs.sound_on = get_u8(h, "sound", 0) != 0;
        s_prefs.clap_wake = get_u8(h, "clap", 1) != 0;
        uint8_t b = get_u8(h, "bright", 100);
        s_prefs.brightness = b < 10 ? 10 : b > 100 ? 100 : b;
        s_prefs.screen_off_min = get_u8(h, "off_min", s_prefs.screen_off_min);
        nvs_close(h);
    }
    return ESP_OK;
}

static esp_err_t write(const char *key, const void *val, size_t len, char type) {
    if (!s_nvs_ok) return ESP_ERR_INVALID_STATE;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    switch (type) {
    case 'b': err = nvs_set_u8(h, key, *(const uint8_t *)val); break;
    case 'u': err = nvs_set_u32(h, key, *(const uint32_t *)val); break;
    case 's': err = len ? nvs_set_str(h, key, val) : nvs_erase_key(h, key); break;
    default: err = ESP_ERR_INVALID_ARG; break;
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;   // 擦除不存在的键
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

app_prefs_t app_settings_prefs(void) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    app_prefs_t p = s_prefs;
    xSemaphoreGive(s_lock);
    return p;
}

esp_err_t app_settings_set_prefs(const app_prefs_t *prefs) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_prefs = *prefs;
    uint8_t sound = prefs->sound_on, clap = prefs->clap_wake, bright = prefs->brightness;
    uint8_t off_min = prefs->screen_off_min;
    esp_err_t err = write("sound", &sound, 1, 'b');
    if (err == ESP_OK) err = write("clap", &clap, 1, 'b');
    if (err == ESP_OK) err = write("bright", &bright, 1, 'b');
    if (err == ESP_OK) err = write("off_min", &off_min, 1, 'b');
    xSemaphoreGive(s_lock);
    return err;
}

bool app_settings_token(char *buf, size_t cap) {
    buf[0] = '\0';
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t h;
    if (s_nvs_ok && nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = cap;
        if (nvs_get_str(h, "gh_token", buf, &len) != ESP_OK) buf[0] = '\0';
        nvs_close(h);
    }
    xSemaphoreGive(s_lock);
    if (!buf[0]) strlcpy(buf, CONFIG_ASTRA_GH_TOKEN, cap);
    return buf[0] != '\0';
}

bool app_settings_has_token(void) {
    if (CONFIG_ASTRA_GH_TOKEN[0]) return true;
    bool found = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t h;
    if (s_nvs_ok && nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = 0;
        found = nvs_get_str(h, "gh_token", NULL, &len) == ESP_OK && len > 1;
        nvs_close(h);
    }
    xSemaphoreGive(s_lock);
    return found;
}

esp_err_t app_settings_set_token(const char *token) {
    if (token && strlen(token) >= APP_TOKEN_MAX) return ESP_ERR_INVALID_SIZE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = write("gh_token", token ? token : "", token ? strlen(token) : 0, 's');
    xSemaphoreGive(s_lock);
    return err;
}

void app_settings_day_baseline(uint32_t *day_key, uint32_t *stars) {
    *day_key = 0;
    *stars = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t h;
    if (s_nvs_ok && nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "day_key", day_key);
        nvs_get_u32(h, "day_stars", stars);
        nvs_close(h);
    }
    xSemaphoreGive(s_lock);
}

esp_err_t app_settings_set_day_baseline(uint32_t day_key, uint32_t stars) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = write("day_key", &day_key, 4, 'u');
    if (err == ESP_OK) err = write("day_stars", &stars, 4, 'u');
    xSemaphoreGive(s_lock);
    return err;
}


bool app_settings_grok(grok_config_t *config) {
    memset(config, 0, sizeof(*config));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t h;
    if (s_nvs_ok && nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(*config);
        if (nvs_get_blob(h, "grok", config, &len) != ESP_OK || len != sizeof(*config)) {
            memset(config, 0, sizeof(*config));
        }
        nvs_close(h);
    }
    xSemaphoreGive(s_lock);
    config->url[GROK_URL_MAX - 1] = 0;
    config->token[GROK_TOKEN_MAX - 1] = 0;
    config->bot[GROK_BOT_MAX - 1] = 0;
    return grok_config_valid(config);
}

esp_err_t app_settings_set_grok(const grok_config_t *config) {
    if (!grok_config_valid(config)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t h;
    esp_err_t err = s_nvs_ok ? nvs_open(NS, NVS_READWRITE, &h) : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        // One blob and one commit avoid mixing old and new endpoint credentials.
        err = nvs_set_blob(h, "grok", config, sizeof(*config));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    xSemaphoreGive(s_lock);
    return err;
}
