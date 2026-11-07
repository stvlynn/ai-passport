#include "grok_proto.h"
#include "lynn_stage.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

static bool printable(const char *s, size_t max) {
    size_t n = s ? strlen(s) : 0;
    if (!n || n >= max) return false;
    for (size_t i = 0; i < n; i++) {
        if ((unsigned char)s[i] < 0x21 || (unsigned char)s[i] > 0x7e) return false;
    }
    return true;
}

bool grok_config_valid(const grok_config_t *c) {
    if (!c || !printable(c->url, GROK_URL_MAX) || strncmp(c->url, "https://", 8) ||
        !c->url[8] || c->url[8] == '/' || c->url[8] == ':' ||
        strpbrk(c->url + 8, "?#@\\") || !printable(c->token, GROK_TOKEN_MAX)) return false;
    // A bot name may contain spaces and UTF-8, but never header controls.
    size_t n = strlen(c->bot);
    if (!n || n >= GROK_BOT_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        if ((unsigned char)c->bot[i] < 0x20 || c->bot[i] == 0x7f) return false;
    }
    return true;
}

bool grok_endpoint(char *out, size_t cap, const grok_config_t *c, bool voice, uint32_t after) {
    if (!grok_config_valid(c)) return false;
    char bot[GROK_BOT_MAX * 3];
    size_t j = 0;
    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)c->bot; *p; p++) {
        unsigned char ch = *p;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || strchr("-._~", ch)) bot[j++] = (char)ch;
        else { bot[j++] = '%'; bot[j++] = hex[ch >> 4]; bot[j++] = hex[ch & 15]; }
    }
    bot[j] = 0;
    size_t n = strlen(c->url);
    while (n && c->url[n - 1] == '/') n--;
    int len = voice ? snprintf(out, cap, "%.*s/v1/voice?bot_id=%s", (int)n, c->url, bot)
                    : snprintf(out, cap, "%.*s/v1/events?bot_id=%s&after=%lu", (int)n, c->url, bot, (unsigned long)after);
    return len >= 0 && (size_t)len < cap;
}

static bool uint32_value(const cJSON *v, uint32_t *out) {
    if (!cJSON_IsNumber(v) || v->valuedouble < 0 || v->valuedouble > UINT32_MAX) return false;
    *out = (uint32_t)v->valuedouble;
    return v->valuedouble == (double)*out;
}

// Validate UTF-8 before accepting text; line breaks are supported by the label.
static bool text_valid(const char *s) {
    if (!s || strlen(s) >= GROK_TEXT_MAX) return false;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t cp;
        unsigned need;
        if (*p < 0x80) { if ((*p < 0x20 && *p != '\n') || *p == 0x7f) return false; p++; continue; }
        if (*p >= 0xc2 && *p <= 0xdf) { cp = *p & 31; need = 1; }
        else if (*p >= 0xe0 && *p <= 0xef) { cp = *p & 15; need = 2; }
        else if (*p >= 0xf0 && *p <= 0xf4) { cp = *p & 7; need = 3; }
        else return false;
        unsigned width = need;
        p++;
        while (need--) { if ((*p & 0xc0) != 0x80) return false; cp = (cp << 6) | (*p++ & 63); }
        if ((width == 2 && cp < 0x800) || (width == 3 && cp < 0x10000) ||
            (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) return false;
    }
    return true;
}

bool grok_event_parse(const char *json, uint32_t after, grok_event_t *event) {
    if (!json || !event || strlen(json) >= GROK_RESPONSE_MAX) return false;
    // Bound parser stack even if a connector returns deeply nested JSON.
    unsigned depth = 0;
    bool string = false, escaped = false;
    for (const char *p = json; *p; p++) {
        if (string) {
            if (escaped) escaped = false;
            else if (*p == '\\') escaped = true;
            else if (*p == '"') string = false;
        } else if (*p == '"') string = true;
        else if (*p == '{' || *p == '[') { if (++depth > 8) return false; }
        else if (*p == '}' || *p == ']') { if (!depth) return false; depth--; }
    }
    if (depth || string) return false;
    memset(event, 0, sizeof(*event));
    const char *end;
    cJSON *root = cJSON_ParseWithOpts(json, &end, true);
    if (!root) return false;
    bool ok = false;
    const cJSON *events = cJSON_GetObjectItemCaseSensitive(root, "events");
    uint32_t cursor;
    if (!cJSON_IsObject(root) || !uint32_value(cJSON_GetObjectItemCaseSensitive(root, "cursor"), &cursor) ||
        cursor < after || !cJSON_IsArray(events) || cJSON_GetArraySize(events) > 1) goto done;
    if (cJSON_GetArraySize(events) == 0) { event->cursor = cursor; ok = true; goto done; }
    const cJSON *e = cJSON_GetArrayItem(events, 0);
    uint32_t id;
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(e, "type");
    const cJSON *text = cJSON_GetObjectItemCaseSensitive(e, "text");
    const cJSON *action = cJSON_GetObjectItemCaseSensitive(e, "action");
    if (!uint32_value(cJSON_GetObjectItemCaseSensitive(e, "id"), &id) || id <= after || id != cursor ||
        !cJSON_IsString(type)) goto done;
    if (strcmp(type->valuestring, "action") == 0) {
        if (!cJSON_IsString(action) || strlen(action->valuestring) >= sizeof(event->action) ||
            lynn_action_find(action->valuestring) < 0) goto done;
        strcpy(event->action, action->valuestring);
        if (text && (!cJSON_IsString(text) || !text_valid(text->valuestring))) goto done;
    } else if (strcmp(type->valuestring, "text") == 0) {
        if (!cJSON_IsString(text) || !text_valid(text->valuestring) || !text->valuestring[0]) goto done;
    } else goto done;
    if (text) strcpy(event->text, text->valuestring);
    event->cursor = cursor;
    event->present = true;
    ok = true;
done:
    cJSON_Delete(root);
    return ok;
}
