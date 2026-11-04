// main/portal_proto.c —— 见 portal_proto.h。
#include "portal_proto.h"

#include <string.h>

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool portal_form_get(const char *body, const char *key, char *out, size_t cap) {
    if (!body || !key || !out || cap == 0) return false;
    size_t klen = strlen(key);
    const char *p = body;
    while (*p) {
        const char *end = strchr(p, '&');
        if (!end) end = p + strlen(p);
        if ((size_t)(end - p) > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            size_t o = 0;
            for (const char *v = p + klen + 1; v < end; v++) {
                char c = *v;
                if (c == '+') {
                    c = ' ';
                } else if (c == '%') {
                    if (end - v < 3) return false;
                    int hi = hexval(v[1]), lo = hexval(v[2]);
                    if (hi < 0 || lo < 0) return false;
                    c = (char)(hi * 16 + lo);
                    v += 2;
                }
                if ((unsigned char)c < 0x20 || c == 0x7f) return false;
                if (o + 1 >= cap) return false;
                out[o++] = c;
            }
            out[o] = '\0';
            return true;
        }
        p = *end ? end + 1 : end;
    }
    return false;
}

bool portal_token_valid(const char *token) {
    size_t n = token ? strlen(token) : 0;
    if (n < 20 || n > 120) return false;
    for (size_t i = 0; i < n; i++) {
        char c = token[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

size_t portal_dns_answer(const uint8_t *req, size_t req_len, const uint8_t ip[4],
                         uint8_t *resp, size_t resp_cap) {
    if (req_len < 12 || req_len > 512) return 0;
    if (req[2] & 0x80) return 0;                       // 是应答而不是查询
    if (((req[2] >> 3) & 0x0F) != 0) return 0;         // 只处理标准查询
    if (req[4] != 0 || req[5] != 1) return 0;          // 只处理单个问题
    size_t q = 12;
    while (q < req_len && req[q] != 0) {
        if (req[q] & 0xC0) return 0;                   // 查询里不应出现压缩指针
        q += (size_t)req[q] + 1;
    }
    q += 1 + 4;                                        // 结尾 0 + QTYPE + QCLASS
    if (q > req_len) return 0;
    uint16_t qtype = (uint16_t)(req[q - 4] << 8 | req[q - 3]);
    bool answer = qtype == 1;                          // 只给 A 记录作答,其它类型返回空应答
    size_t total = q + (answer ? 16 : 0);
    if (total > resp_cap) return 0;

    memcpy(resp, req, q);
    resp[2] = 0x81 | (req[2] & 0x01);                  // QR=1, opcode=0, AA=0, RD 照抄
    resp[3] = 0x80;                                    // RA=1, RCODE=0
    resp[6] = 0;
    resp[7] = answer ? 1 : 0;                          // ANCOUNT
    resp[8] = resp[9] = resp[10] = resp[11] = 0;       // NSCOUNT, ARCOUNT
    if (answer) {
        uint8_t *a = resp + q;
        const uint8_t rr[12] = { 0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x04 };
        memcpy(a, rr, sizeof(rr));
        memcpy(a + 12, ip, 4);
    }
    return total;
}

static bool append_escaped(char *out, size_t cap, size_t *o, const char *s) {
    for (; *s; s++) {
        bool esc = strchr("\\;,:\"", *s) != NULL;
        if (*o + (esc ? 2 : 1) >= cap) return false;
        if (esc) out[(*o)++] = '\\';
        out[(*o)++] = *s;
    }
    return true;
}

bool portal_wifi_qr(char *out, size_t cap, const char *ssid, const char *pass) {
    size_t o = 0;
    const char *parts[] = { "WIFI:T:WPA;S:", NULL, ";P:", NULL, ";;" };
    parts[1] = ssid;
    parts[3] = pass;
    for (int i = 0; i < 5; i++) {
        if (i == 1 || i == 3) {
            if (!append_escaped(out, cap, &o, parts[i])) return false;
        } else {
            size_t n = strlen(parts[i]);
            if (o + n >= cap) return false;
            memcpy(out + o, parts[i], n);
            o += n;
        }
    }
    out[o] = '\0';
    return true;
}
