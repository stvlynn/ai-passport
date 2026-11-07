// main/app_settings.h —— 应用设置与小量持久状态(NVS 命名空间 "astra")。
// 所有函数可在任意任务调用;内部串行化。NVS 失败时退回默认值,不擦除分区。
#pragma once

#include "esp_err.h"
#include "grok_proto.h"
#include "wifi_profiles.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_TOKEN_MAX 128

typedef struct {
    bool sound_on;
    bool clap_wake;
    uint8_t brightness;      // 10..100
    uint8_t screen_off_min;  // 息屏时间(分钟),0 = 从不;取值见 APP_SCREEN_OFF_OPTIONS
} app_prefs_t;

// 初始化 NVS(若尚未初始化)并加载设置。NVS 损坏时返回错误但设置仍为默认值。
esp_err_t app_settings_init(void);

app_prefs_t app_settings_prefs(void);
esp_err_t app_settings_set_prefs(const app_prefs_t *prefs);

// GitHub token:优先 NVS,其次 Kconfig 的 CONFIG_ASTRA_GH_TOKEN。返回 false 表示都没有。
bool app_settings_token(char *buf, size_t cap);
bool app_settings_has_token(void);
esp_err_t app_settings_set_token(const char *token);   // NULL 或 "" = 清除 NVS 中的 token

// "今日新增星标"的基线:day_key 形如 20261104(本地日期)。
void app_settings_day_baseline(uint32_t *day_key, uint32_t *stars);
esp_err_t app_settings_set_day_baseline(uint32_t day_key, uint32_t stars);

// Connector settings, stored only in NVS. Updates must contain all fields.
bool app_settings_grok(grok_config_t *config);
esp_err_t app_settings_set_grok(const grok_config_t *config);

// NOT_FOUND distinguishes legacy installations from an intentionally empty list.
esp_err_t app_settings_wifi(wifi_profiles_t *profiles);
esp_err_t app_settings_set_wifi(const wifi_profiles_t *profiles);
