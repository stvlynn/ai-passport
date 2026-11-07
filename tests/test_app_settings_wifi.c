#include "provision_test_stubs.h"
#include "../main/app_settings.c"
static wifi_profiles_t stored, pending;
static size_t blob_size;
static esp_err_t read_error = ESP_ERR_NVS_NOT_FOUND, write_error, commit_error;
static unsigned writes, commits, closes;
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h) { assert(!strcmp(ns, "astra")); (void)mode; *h = 1; return ESP_OK; }
void nvs_close(nvs_handle_t h) { assert(h == 1); closes++; }
esp_err_t nvs_commit(nvs_handle_t h) {
    assert(h == 1); commits++;
    if (commit_error) return commit_error;
    stored = pending; blob_size = sizeof(stored); read_error = ESP_OK; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *p, size_t n) {
    assert(h == 1 && !strcmp(key, "wifi_profiles") && n == sizeof(pending));
    writes++; if (write_error) return write_error;
    memcpy(&pending, p, n); return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *p, size_t *n) {
    assert(h == 1 && !strcmp(key, "wifi_profiles") && *n == sizeof(stored));
    if (read_error) return read_error;
    memcpy(p, &stored, blob_size); *n = blob_size; return ESP_OK;
}
int main(void) {
    s_lock = xSemaphoreCreateMutex();
    wifi_profiles_t p;
    assert(app_settings_wifi(&p) == ESP_ERR_INVALID_STATE);
    s_nvs_ok = true;
    assert(app_settings_wifi(&p) == ESP_ERR_NVS_NOT_FOUND && p.count == 0);
    wifi_profile_t profile = { .ssid = "Home", .password = "test-password" };
    assert(wifi_profiles_put(&p, &profile));
    assert(app_settings_set_wifi(&p) == ESP_OK && writes == 1 && commits == 1);
    memset(&p, 0, sizeof(p));
    assert(app_settings_wifi(&p) == ESP_OK && p.count == 1);
    assert(!strcmp(p.entries[0].password, "test-password"));
    profile = (wifi_profile_t){ .ssid = "Office" };
    assert(wifi_profiles_put(&p, &profile));
    write_error = ESP_FAIL;
    assert(app_settings_set_wifi(&p) == ESP_FAIL && commits == 1);
    write_error = ESP_OK; commit_error = ESP_FAIL;
    assert(app_settings_set_wifi(&p) == ESP_FAIL);
    commit_error = ESP_OK;
    assert(app_settings_wifi(&p) == ESP_OK && p.count == 1);
    // An empty committed list is different from missing legacy storage.
    memset(&p, 0, sizeof(p));
    assert(app_settings_set_wifi(&p) == ESP_OK);
    assert(app_settings_wifi(&p) == ESP_OK && p.count == 0);
    stored.count = WIFI_PROFILES_MAX + 1;
    assert(app_settings_wifi(&p) == ESP_ERR_INVALID_STATE && p.count == 0);
    stored.count = 0; blob_size--;
    assert(app_settings_wifi(&p) == ESP_ERR_INVALID_STATE && p.count == 0);
    unsigned before = writes; p.count = WIFI_PROFILES_MAX + 1;
    assert(app_settings_set_wifi(&p) == ESP_ERR_INVALID_ARG && writes == before);
    assert(closes > 0);
    puts("test_app_settings_wifi: PASS");
}
