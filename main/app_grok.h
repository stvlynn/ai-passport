// Connector worker. No LVGL or microphone work runs in a button callback.
#pragma once
#include "esp_err.h"
#include "grok_proto.h"
#include <stdbool.h>
typedef bool (*app_grok_event_cb_t)(const grok_event_t *event);
typedef void (*app_grok_status_cb_t)(const char *text);
esp_err_t app_grok_start(app_grok_event_cb_t event_cb, app_grok_status_cb_t status_cb);
void app_grok_set_active(bool active);
// Push-to-talk: down starts recording at once (connection is made in parallel),
// up ends the utterance and sends it. Returns false when busy or out of memory.
bool app_grok_ptt_down(void);
void app_grok_ptt_up(void);
// Serialize TLS with GitHub sync on this no-PSRAM board. Worker tasks only.
void app_grok_network_lock(void);
void app_grok_network_unlock(void);
