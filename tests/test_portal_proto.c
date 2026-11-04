// tests/test_portal_proto.c —— 配网网页的表单解析、token 校验、DNS 应答与二维码字符串。
#include "portal_proto.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_form(void) {
    char v[16];
    const char *body = "ssid=My+Home%20Wi-Fi&pass=p%40ss%26w%3Drd&token=";
    char big[32];
    assert(portal_form_get(body, "ssid", big, sizeof(big)) && strcmp(big, "My Home Wi-Fi") == 0);
    assert(portal_form_get(body, "pass", big, sizeof(big)) && strcmp(big, "p@ss&w=rd") == 0);
    assert(portal_form_get(body, "token", big, sizeof(big)) && big[0] == '\0');
    assert(!portal_form_get(body, "missing", big, sizeof(big)));
    assert(!portal_form_get(body, "ss", big, sizeof(big)));          // 前缀不算匹配
    // UTF-8 SSID 原样解码
    assert(portal_form_get("ssid=%E5%8A%9E%E5%85%AC%E5%AE%A4", "ssid", big, sizeof(big)));
    assert(strcmp(big, "办公室") == 0);
    // 非法转义、控制字符、截断的转义
    assert(!portal_form_get("ssid=a%zz", "ssid", big, sizeof(big)));
    assert(!portal_form_get("ssid=a%0Ab", "ssid", big, sizeof(big)));
    assert(!portal_form_get("ssid=a%4", "ssid", big, sizeof(big)));
    // 恰好放满
    assert(portal_form_get("k=123456789012345", "k", v, sizeof(v)) && strlen(v) == 15);
    assert(!portal_form_get("k=1234567890123456", "k", v, sizeof(v)));
}

// 测试用 token 在运行时拼出,避免仓库里出现形似真实 token 的字面量。
static const char *fake_token(char *buf, const char *prefix, size_t body_len) {
    size_t n = strlen(prefix);
    memcpy(buf, prefix, n);
    for (size_t i = 0; i < body_len; i++) buf[n + i] = "aZ0_"[i % 4];
    buf[n + body_len] = '\0';
    return buf;
}

static void test_token(void) {
    char buf[160];
    assert(portal_token_valid(fake_token(buf, "ghp_", 36)));
    assert(portal_token_valid(fake_token(buf, "github_pat_", 82)));
    assert(!portal_token_valid(fake_token(buf, "github_pat_", 120)));   // 超过 120
    assert(!portal_token_valid("short_token"));
    assert(!portal_token_valid("ghp_0123456789abcdef 0123456789"));
    assert(!portal_token_valid("ghp_0123456789abcdef-0123456789"));
    assert(!portal_token_valid(""));
    assert(!portal_token_valid(NULL));
}

static size_t make_query(uint8_t *q, const char *name, uint16_t type) {
    uint8_t hdr[12] = { 0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0 };
    memcpy(q, hdr, 12);
    size_t o = 12;
    const char *p = name;
    while (*p) {
        const char *dot = strchr(p, '.');
        size_t n = dot ? (size_t)(dot - p) : strlen(p);
        q[o++] = (uint8_t)n;
        memcpy(q + o, p, n);
        o += n;
        p += n + (dot ? 1 : 0);
    }
    q[o++] = 0;
    q[o++] = (uint8_t)(type >> 8);
    q[o++] = (uint8_t)type;
    q[o++] = 0;
    q[o++] = 1;
    return o;
}

static void test_dns(void) {
    const uint8_t ip[4] = { 192, 168, 4, 1 };
    uint8_t q[128], r[160];
    size_t qn = make_query(q, "captive.apple.com", 1);
    size_t rn = portal_dns_answer(q, qn, ip, r, sizeof(r));
    assert(rn == qn + 16);
    assert(r[0] == 0x12 && r[1] == 0x34);           // 同一事务 ID
    assert((r[2] & 0x80) && r[3] == 0x80);          // 是应答,无错误
    assert(r[7] == 1);                              // 一条回答
    assert(memcmp(r + qn + 12, ip, 4) == 0);
    assert(r[qn] == 0xC0 && r[qn + 1] == 0x0C);     // 指回问题里的域名

    // AAAA:空应答(让客户端退回 IPv4)
    qn = make_query(q, "example.com", 28);
    rn = portal_dns_answer(q, qn, ip, r, sizeof(r));
    assert(rn == qn && r[7] == 0);

    // 应答包、截断包、缓冲区不足
    q[2] |= 0x80;
    assert(portal_dns_answer(q, qn, ip, r, sizeof(r)) == 0);
    q[2] &= 0x7F;
    assert(portal_dns_answer(q, 10, ip, r, sizeof(r)) == 0);
    assert(portal_dns_answer(q, qn - 3, ip, r, sizeof(r)) == 0);
    qn = make_query(q, "a.b", 1);
    assert(portal_dns_answer(q, qn, ip, r, qn + 15) == 0);
}

static void test_qr(void) {
    char out[64];
    assert(portal_wifi_qr(out, sizeof(out), "AstraMonitor-0554", "12345678"));
    assert(strcmp(out, "WIFI:T:WPA;S:AstraMonitor-0554;P:12345678;;") == 0);
    assert(portal_wifi_qr(out, sizeof(out), "a;b", "c:d\\"));
    assert(strcmp(out, "WIFI:T:WPA;S:a\\;b;P:c\\:d\\\\;;") == 0);
    char tiny[16];
    assert(!portal_wifi_qr(tiny, sizeof(tiny), "AstraMonitor-0554", "12345678"));
}

int main(void) {
    test_form();
    test_token();
    test_dns();
    test_qr();
    puts("test_portal_proto: PASS");
    return 0;
}
