#define _POSIX_C_SOURCE 200809L
#include "provision_test_stubs.h"
#include "../main/app_net.c"

static wifi_config_t driver;
static wifi_profiles_t persisted;
static bool have_profiles, fail_save, fail_config;
static unsigned connects;
static bool stale_ip;
esp_err_t esp_wifi_get_config(int i, wifi_config_t *c) { assert(i == WIFI_IF_STA); *c = driver; return ESP_OK; }
esp_err_t esp_wifi_set_config(int i, const wifi_config_t *c) {
    if (i != WIFI_IF_STA) return ESP_OK;
    if (fail_config) { fail_config = false; return ESP_FAIL; }
    driver = *c; return ESP_OK;
}
esp_err_t esp_wifi_connect(void) { connects++; return ESP_OK; }
esp_err_t esp_wifi_disconnect(void) { return ESP_OK; }
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *a) {
    memset(a, 0, sizeof(*a)); memcpy(a->ssid, driver.sta.ssid, 32);
    if (stale_ip) strcpy((char *)a->ssid, "PreviousAssociation");
    return ESP_OK;
}
esp_err_t app_settings_wifi(wifi_profiles_t *p) { *p = persisted; return have_profiles ? ESP_OK : ESP_ERR_NVS_NOT_FOUND; }
esp_err_t app_settings_set_wifi(const wifi_profiles_t *p) {
    if (fail_save) return ESP_FAIL;
    assert(wifi_profiles_valid(p)); persisted = *p; have_profiles = true; return ESP_OK;
}
esp_err_t app_portal_start(void) { return ESP_OK; }
void app_portal_stop(void) {}
static void disconnected(unsigned reason) {
    wifi_event_sta_disconnected_t e = { .reason = reason };
    wifi_event(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &e);
}
static void got_ip(void) {
    ip_event_got_ip_t e = { .ip_info.ip = 42 };
    ip_event(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &e);
}
static void trial(const char *ssid, const char *pass) {
    bool switching = s_st.link == NET_LINK_ONLINE || s_st.link == NET_LINK_CONNECTING;
    assert(app_net_try_credentials(ssid, pass) == ESP_OK);
    if (switching) disconnected(0);
    assert(s_trial);
}
int main(void) {
    strcpy((char *)driver.sta.ssid, "Home");
    strcpy((char *)driver.sta.password, "home-password");
    assert(app_net_init(NULL) == ESP_OK);
    assert(have_profiles && persisted.count == 1); // upgrade imports old credentials
    wifi_event(NULL, WIFI_EVENT, WIFI_EVENT_STA_START, NULL); got_ip();
    trial("Office", "office-password");
    assert(persisted.count == 1); // unverified credentials stay in RAM
    stale_ip = true; got_ip();
    assert(s_trial && persisted.count == 1); // stale association cannot pass a trial
    stale_ip = false;
    got_ip(); assert(!s_trial && persisted.count == 2 && s_st.prov == NET_PROV_DONE);
    trial("Home", "");
    assert(!strcmp((char *)driver.sta.password, "home-password"));
    got_ip(); assert(persisted.count == 2);
    wifi_profiles_t before = persisted;
    trial("Broken", "wrong-password"); disconnected(WIFI_REASON_AUTH_FAIL);
    assert(!s_trial && !memcmp(&persisted, &before, sizeof(before)));
    assert(!strcmp((char *)driver.sta.ssid, "Home"));
    // The rollback disconnect can arrive later without corrupting the list.
    disconnected(0); retry_cb(NULL);
    assert(!strcmp((char *)driver.sta.ssid, "Office")); // automatic failover
    got_ip();
    trial("Timeout", "test-password"); trial_timeout_cb(NULL);
    assert(!s_trial && !memcmp(&persisted, &before, sizeof(before)));
    got_ip(); fail_save = true;
    trial("StorageFailure", "test-password"); got_ip();
    assert(!s_trial && !memcmp(&persisted, &before, sizeof(before)));
    fail_save = false; got_ip();
    fail_config = true;
    assert(app_net_try_credentials("ConfigFailure", "test-password") == ESP_FAIL);
    assert(!s_trial && !memcmp(&persisted, &before, sizeof(before)));
    got_ip();
    trial("Cafe", ""); got_ip();
    trial("Hotel", "test-password"); got_ip();
    trial("Guest", "test-password"); got_ip();
    assert(persisted.count == 5);
    unsigned before_retry = connects; retry_cb(NULL);
    assert(connects == before_retry); // late retry cannot disturb an online link
    assert(app_net_try_credentials("Sixth", "") == ESP_ERR_INVALID_ARG);
    assert(app_net_forget_network("Cafe") == ESP_OK && persisted.count == 4);
    fail_save = true;
    assert(app_net_forget_network("Home") == ESP_FAIL && persisted.count == 4);
    fail_save = false;
    assert(app_net_forget_network("Guest") == ESP_OK && persisted.count == 3);
    assert(strcmp((char *)driver.sta.ssid, "Guest") != 0);
    assert(app_net_clear_credentials() == ESP_OK && persisted.count == 0);
    strcpy((char *)driver.sta.ssid, "Legacy"); // old driver NVS still exists on reboot
    memset(&s_st, 0, sizeof(s_st));
    assert(app_net_init(NULL) == ESP_OK);
    assert(!driver.sta.ssid[0] && s_profiles.count == 0); // never resurrect old data
    trial("RebootTrial", "test-password");
    strcpy((char *)driver.sta.ssid, "Legacy");
    s_trial = false; memset(&s_st, 0, sizeof(s_st));
    assert(app_net_init(NULL) == ESP_OK && s_profiles.count == 0 && !driver.sta.ssid[0]);
    assert(connects > 0);
    puts("test_app_net_profiles: PASS");
}
