// Bounded connector wire protocol, independent from ESP-IDF/LVGL.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GROK_URL_MAX 256
#define GROK_TOKEN_MAX 192
#define GROK_BOT_MAX 65
#define GROK_TEXT_MAX 192
#define GROK_RESPONSE_MAX 2048
#define GROK_VOICE_SECONDS 5
#define GROK_VOICE_BYTES (16000 * 2 * GROK_VOICE_SECONDS)

typedef struct {
    char url[GROK_URL_MAX];
    char token[GROK_TOKEN_MAX];
    char bot[GROK_BOT_MAX];
} grok_config_t;

typedef struct {
    uint32_t cursor;
    bool present;
    char action[32]; // Empty for text events.
    char text[GROK_TEXT_MAX];
} grok_event_t;

bool grok_config_valid(const grok_config_t *config);
bool grok_endpoint(char *out, size_t cap, const grok_config_t *config,
                   bool voice, uint32_t after);
// Reject malformed/oversized batches. A successful parse never skips events.
bool grok_event_parse(const char *json, uint32_t after, grok_event_t *event);
