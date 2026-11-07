#include "wifi_profiles.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    wifi_profiles_t p = {0};
    wifi_profile_t next;
    assert(wifi_profiles_valid(&p));
    assert(wifi_profiles_resolve(&p, "Home", "test-password", &next));
    assert(wifi_profiles_put(&p, &next));
    assert(wifi_profiles_resolve(&p, "Office", "", &next));
    assert(wifi_profiles_put(&p, &next));
    assert(p.count == 2 && !strcmp(p.entries[1].ssid, "Home"));
    assert(wifi_profiles_resolve(&p, "Home", "", &next));
    assert(!strcmp(next.password, "test-password"));
    assert(wifi_profiles_put(&p, &next) && p.count == 2);
    assert(!strcmp(p.entries[0].ssid, "Home"));
    assert(wifi_profiles_resolve(&p, "Home", "replacement", &next));
    assert(wifi_profiles_put(&p, &next));
    assert(!strcmp(p.entries[0].password, "replacement"));
    assert(!wifi_profiles_resolve(&p, "X", "short", &next));
    char full[34]; memset(full, 'x', 33); full[33] = 0;
    assert(!wifi_profiles_resolve(&p, full, "", &next));
    full[32] = 0;
    assert(wifi_profiles_resolve(&p, full, "", &next));
    assert(wifi_profiles_put(&p, &next));
    assert(wifi_profiles_resolve(&p, "Cafe", "", &next) && wifi_profiles_put(&p, &next));
    assert(wifi_profiles_resolve(&p, "Hotel", "", &next) && wifi_profiles_put(&p, &next));
    wifi_profiles_t before = p;
    assert(!wifi_profiles_resolve(&p, "Sixth", "", &next));
    assert(memcmp(&p, &before, sizeof(p)) == 0);
    assert(wifi_profiles_resolve(&p, "Home", "", &next) && wifi_profiles_put(&p, &next));
    assert(p.count == WIFI_PROFILES_MAX);
    assert(wifi_profiles_remove(&p, "Office") && p.count == 4);
    assert(!wifi_profiles_remove(&p, "Office"));
    assert(wifi_profiles_valid(&p));
    p.entries[1] = p.entries[0]; assert(!wifi_profiles_valid(&p));
    p = before; memset(p.entries[0].ssid, 'a', 33); assert(!wifi_profiles_valid(&p));
    p = before; p.count = 6; assert(!wifi_profiles_valid(&p));
    puts("test_wifi_profiles: PASS");
}
