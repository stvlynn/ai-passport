#include "grok_proto.h"
#include "lynn_stage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    grok_config_t c = { .url = "https://connector.example.com/", .token = "test-connector-token", .bot = "Lynn bot/中文" };
    char url[600];
    assert(grok_config_valid(&c));
    assert(grok_endpoint(url, sizeof(url), &c, true, 0));
    assert(!strcmp(url, "https://connector.example.com/v1/voice?bot_id=Lynn%20bot%2F%E4%B8%AD%E6%96%87"));
    assert(grok_endpoint(url, sizeof(url), &c, false, UINT32_MAX));
    assert(strstr(url, "&after=4294967295"));
    assert(!grok_endpoint(url, 8, &c, true, 0));
    const char *bad_urls[] = {"http://example.com", "https://", "https:///path", "https://a@b", "https://a?x", "https://a#frag", "https://a\\b"};
    for (unsigned i = 0; i < sizeof(bad_urls) / sizeof(bad_urls[0]); i++) {
        strcpy(c.url, bad_urls[i]);
        assert(!grok_config_valid(&c));
    }
    strcpy(c.url, "https://example.com");
    strcpy(c.token, "token\r\nInjected: true");
    assert(!grok_config_valid(&c));

    grok_event_t e;
    assert(grok_event_parse("{\"cursor\":1700000000,\"events\":[]}", 0, &e));
    assert(!e.present && e.cursor == 1700000000);
    assert(grok_event_parse("{\"cursor\":12,\"events\":[{\"id\":12,\"type\":\"text\",\"text\":\"你好\\nHello\"}]}", 11, &e));
    assert(e.present && !strcmp(e.text, "你好\nHello") && !e.action[0]);
    // All 41 existing actions are accessible, including idle activities.
    assert(lynn_action_count == 41);
    for (unsigned i = 0; i < lynn_action_count; i++) {
        char json[256];
        snprintf(json, sizeof(json), "{\"cursor\":42,\"events\":[{\"id\":42,\"type\":\"action\",\"action\":\"%s\",\"text\":\"Hello\"}]}", lynn_actions[i].id);
        assert(grok_event_parse(json, 41, &e));
        assert(e.present && !strcmp(e.action, lynn_actions[i].id));
    }
    const char *bad[] = {
        "{}", "null", "{\"cursor\":1,\"events\":[]}",
        "{\"cursor\":2.5,\"events\":[]}", "{\"cursor\":4294967296,\"events\":[]}",
        "{\"cursor\":3,\"events\":[{},{}]}",
        "{\"cursor\":3,\"events\":[{\"id\":2,\"type\":\"text\",\"text\":\"old\"}]}",
        "{\"cursor\":4,\"events\":[{\"id\":3,\"type\":\"text\",\"text\":\"skip\"}]}",
        "{\"cursor\":3,\"events\":[{\"id\":3,\"type\":\"action\",\"action\":\"unknown\"}]}",
        "{\"cursor\":3,\"events\":[{\"id\":3,\"type\":\"text\",\"text\":\"\xc0\xaf\"}]}",
        "{\"cursor\":3,\"events\":[{\"id\":3,\"type\":\"text\",\"text\":\"\xed\xa0\x80\"}]}",
        "{\"cursor\":3,\"events\":[{\"id\":3,\"type\":\"text\",\"text\":\"\xf4\x90\x80\x80\"}]}",
        "{\"cursor\":3,\"events\":[{\"id\":3,\"type\":\"text\",\"text\":\"\xe4\xb8\"}]}",
        "{\"cursor\":3,\"events\":[{\"id\":3,\"type\":\"text\",\"text\":\"\"}]}",
        "{\"cursor\":3,\"events\":[{\"id\":3,\"type\":\"text\",\"text\":\"\\t\"}]}",
        "{\"cursor\":3,\"events\":[]}garbage", "[[[[[[[[[0]]]]]]]]]"
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) assert(!grok_event_parse(bad[i], 2, &e));
    char huge[GROK_RESPONSE_MAX + 1];
    memset(huge, ' ', sizeof(huge) - 1); huge[sizeof(huge) - 1] = 0;
    assert(!grok_event_parse(huge, 2, &e));
    char json[400], text[GROK_TEXT_MAX + 1];
    memset(text, 'a', GROK_TEXT_MAX); text[GROK_TEXT_MAX] = 0;
    snprintf(json, sizeof(json), "{\"cursor\":3,\"events\":[{\"id\":3,\"type\":\"text\",\"text\":\"%s\"}]}", text);
    assert(!grok_event_parse(json, 2, &e));
    text[GROK_TEXT_MAX - 1] = 0;
    snprintf(json, sizeof(json), "{\"cursor\":3,\"events\":[{\"id\":3,\"type\":\"text\",\"text\":\"%s\"}]}", text);
    assert(grok_event_parse(json, 2, &e));
    puts("grok_proto: PASS");
    return 0;
}
