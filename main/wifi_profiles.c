#include "wifi_profiles.h"
#include <string.h>

static bool profile_valid(const wifi_profile_t *p) {
    if (!memchr(p->ssid, 0, sizeof(p->ssid)) || !p->ssid[0] ||
        !memchr(p->password, 0, sizeof(p->password))) return false;
    size_t n = strlen(p->password);
    return n == 0 || n >= 8;
}

int wifi_profiles_find(const wifi_profiles_t *p, const char *ssid) {
    for (unsigned i = 0; i < p->count; i++) {
        if (strcmp(p->entries[i].ssid, ssid) == 0) return (int)i;
    }
    return -1;
}

bool wifi_profiles_valid(const wifi_profiles_t *p) {
    if (!p || p->count > WIFI_PROFILES_MAX) return false;
    for (unsigned i = 0; i < p->count; i++) {
        if (!profile_valid(&p->entries[i]) ||
            wifi_profiles_find(p, p->entries[i].ssid) != (int)i) return false;
    }
    return true;
}

bool wifi_profiles_resolve(const wifi_profiles_t *p, const char *ssid,
                           const char *password, wifi_profile_t *out) {
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || (password && strlen(password) > 63)) return false;
    wifi_profile_t next = {0};
    memcpy(next.ssid, ssid, strlen(ssid));
    int i = wifi_profiles_find(p, ssid);
    if ((!password || !password[0]) && i >= 0) next = p->entries[i];
    else if (password) memcpy(next.password, password, strlen(password));
    if (!profile_valid(&next) || (i < 0 && p->count == WIFI_PROFILES_MAX)) return false;
    *out = next;
    return true;
}

bool wifi_profiles_put(wifi_profiles_t *p, const wifi_profile_t *profile) {
    if (!profile_valid(profile)) return false;
    int i = wifi_profiles_find(p, profile->ssid);
    if (i < 0 && p->count == WIFI_PROFILES_MAX) return false;
    wifi_profile_t copy = *profile;
    if (i < 0) i = p->count++;
    memmove(p->entries + 1, p->entries, (size_t)i * sizeof(copy));
    p->entries[0] = copy;
    return true;
}

bool wifi_profiles_remove(wifi_profiles_t *p, const char *ssid) {
    int i = wifi_profiles_find(p, ssid);
    if (i < 0) return false;
    memmove(p->entries + i, p->entries + i + 1, (p->count - (unsigned)i - 1) * sizeof(p->entries[0]));
    memset(&p->entries[--p->count], 0, sizeof(p->entries[0]));
    return true;
}
