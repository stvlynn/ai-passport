// main/portal_proto.h —— 配网网页用到的纯协议逻辑(host 可测):
// 表单字段解析、token 校验、DNS 劫持应答构造、Wi-Fi 二维码字符串。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 从 application/x-www-form-urlencoded 的 body 中取出 key 对应的值并做 URL 解码。
// 找不到返回 false。值超过 cap-1 字节、或含 NUL/控制字符时也返回 false。
bool portal_form_get(const char *body, const char *key, char *out, size_t cap);

// GitHub token 只允许 [A-Za-z0-9_],长度 20..120(classic ghp_/fine-grained github_pat_)。
bool portal_token_valid(const char *token);

// 对任意 A 记录查询回答 ip(网络字节序的 4 字节)。req 为收到的 DNS 报文。
// 返回写入 resp 的字节数;不是可回答的标准查询时返回 0。
size_t portal_dns_answer(const uint8_t *req, size_t req_len, const uint8_t ip[4],
                         uint8_t *resp, size_t resp_cap);

// 手机相机可识别的 Wi-Fi 二维码内容:WIFI:T:WPA;S:<ssid>;P:<pass>;;
// 按规范对 \ ; , : " 转义。返回 false 表示缓冲区不足。
bool portal_wifi_qr(char *out, size_t cap, const char *ssid, const char *pass);
