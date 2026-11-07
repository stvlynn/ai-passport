// Bounded saved-network model; independent of ESP-IDF and LVGL.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define WIFI_PROFILES_MAX 5

typedef struct {
    char ssid[33];
    char password[64];
} wifi_profile_t;

typedef struct {
    uint8_t count;
    wifi_profile_t entries[WIFI_PROFILES_MAX];
} wifi_profiles_t;

bool wifi_profiles_valid(const wifi_profiles_t *profiles);
int wifi_profiles_find(const wifi_profiles_t *profiles, const char *ssid);
// Empty password preserves a saved password; a new empty password means open Wi-Fi.
bool wifi_profiles_resolve(const wifi_profiles_t *profiles, const char *ssid,
                           const char *password, wifi_profile_t *out);
// Upsert and prefer a successfully connected network. Never evict another network.
bool wifi_profiles_put(wifi_profiles_t *profiles, const wifi_profile_t *profile);
bool wifi_profiles_remove(wifi_profiles_t *profiles, const char *ssid);
