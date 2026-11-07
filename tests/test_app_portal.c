#include "provision_test_stubs.h"
#include "../main/app_portal.c"
static char output[20000], status[64];
static const char *request_body;
static size_t offset;
static unsigned wifi_calls, grok_writes, token_writes, forget_calls;
static char selected[33], password[64];
static grok_config_t config = { .url = "https://example.com", .token = "private-connector-token", .bot = "Lynn" };
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *t) { (void)r; (void)t; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *k, const char *v) { (void)r; (void)k; (void)v; return ESP_OK; }
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *s) { (void)r; strcpy(status, s); return ESP_OK; }
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *s, ssize_t n) {
    (void)r; if (s && n > 0) { size_t used = strlen(output); assert(used + (size_t)n < sizeof(output)); memcpy(output + used, s, (size_t)n); output[used + (size_t)n] = 0; } return ESP_OK;
}
esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *r, const char *s) { return httpd_resp_send_chunk(r, s, s ? (ssize_t)strlen(s) : 0); }
esp_err_t httpd_resp_send(httpd_req_t *r, const char *s, ssize_t n) { return httpd_resp_send_chunk(r, s, n); }
int httpd_req_recv(httpd_req_t *r, char *s, size_t n) {
    (void)r; if (n > 17) n = 17; memcpy(s, request_body + offset, n); offset += n; return (int)n;
}
int app_net_scan_results(char ssids[][33], int max) { assert(max > 0); strcpy(ssids[0], "<Nearby>"); return 1; }
int app_net_saved_networks(char ssids[][33], int max) { assert(max >= 2); strcpy(ssids[0], "Home<&\""); strcpy(ssids[1], "Office"); return 2; }
net_status_t app_net_status(void) { return (net_status_t){ .link = NET_LINK_ONLINE, .ssid = "Office" }; }
esp_err_t app_net_try_credentials(const char *ssid, const char *pass) { wifi_calls++; strcpy(selected, ssid); strcpy(password, pass); return ESP_OK; }
esp_err_t app_net_forget_network(const char *ssid) { assert(!strcmp(ssid, "Office")); forget_calls++; return ESP_OK; }
bool app_settings_has_token(void) { return true; }
bool app_settings_grok(grok_config_t *g) { *g = config; return true; }
esp_err_t app_settings_set_grok(const grok_config_t *g) { grok_writes++; config = *g; return ESP_OK; }
esp_err_t app_settings_set_token(const char *t) { assert(t[0]); token_writes++; return ESP_OK; }
bool grok_config_valid(const grok_config_t *g) { return !strncmp(g->url, "https://", 8) && g->token[0] && g->bot[0]; }
static void post(const char *body) {
    request_body = body; offset = 0; output[0] = status[0] = 0;
    httpd_req_t r = { .content_len = strlen(body) };
    assert(save_post(&r) == ESP_OK);
}
int main(int argc, char **argv) {
    httpd_req_t r = {0}; assert(page_get(&r) == ESP_OK);
    assert(strstr(output, "Home&lt;&amp;&quot;") && strstr(output, "Office"));
    assert(strstr(output, "https://example.com") && strstr(output, "value=\"Lynn\""));
    assert(!strstr(output, config.token));
    assert(strstr(output, "name=action value=forget"));
    if (argc == 2) { FILE *f = fopen(argv[1], "w"); assert(f); fputs(output, f); fclose(f); }
    // Wi-Fi can be added without resubmitting any service credentials.
    post("ssid=New+Network&pass=test-password&token=");
    assert(!strcmp(status, "200 OK") && wifi_calls == 1 && !strcmp(selected, "New Network"));
    assert(!grok_writes && !token_writes);
    // Existing Wi-Fi delegates blank-password preservation to the network layer.
    post("ssid=Office&pass=&token=");
    assert(wifi_calls == 2 && !password[0]);
    // A service-only change must not trigger Wi-Fi reconnection or clear tokens.
    post("ssid=&pass=&token=&connector_url=https%3A%2F%2Fnew.example.com&connector_token=&bot_id=Lynn");
    assert(!strcmp(status, "200 OK") && wifi_calls == 2 && grok_writes == 1);
    assert(!strcmp(config.token, "private-connector-token") && !token_writes);
    post("ssid=&pass=&token=&connector_url=&connector_token=&bot_id=");
    assert(!strcmp(status, "200 OK") && wifi_calls == 2 && grok_writes == 1);
    post("ssid=Office&pass=&token=&action=forget");
    assert(!strcmp(status, "200 OK") && forget_calls == 1 && wifi_calls == 2);
    post("ssid=Bad%00Name&pass=&token=");
    assert(!strcmp(status, "400 Bad Request") && wifi_calls == 2);
    post("ssid=New&pass=short&token=");
    assert(!strcmp(status, "400 Bad Request") && wifi_calls == 2);
    puts("test_app_portal: PASS");
}
