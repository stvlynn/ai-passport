// main/app_portal.c —— 见 app_portal.h。
#include "app_portal.h"

#include "app_net.h"
#include "app_settings.h"
#include "portal_proto.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"

#include <string.h>

static const char *TAG = "portal";

#define BODY_MAX 2048

static httpd_handle_t s_http;
static TaskHandle_t s_dns_task;
static SemaphoreHandle_t s_dns_done;
static volatile bool s_dns_run;

// ---------------------------------------------------------------------------
// 网页
// ---------------------------------------------------------------------------

// 页面配色取自 AstraFoundry Sunbeam:暖纸底、墨色字、唯一的黄色按钮。
static const char PAGE_HEAD[] =
    "<!doctype html><html lang=zh><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Astra Monitor 配网</title><style>"
    "body{margin:0;background:#faf8f5;color:#14110f;font:16px/1.6 system-ui,sans-serif}"
    "main{max-width:420px;margin:0 auto;padding:24px}"
    "h1{font-size:24px;font-weight:500;margin:0 0 4px}"
    "p{color:#6c6660;margin:0 0 20px;font-size:14px}"
    "label{display:block;font-size:14px;font-weight:500;margin:16px 0 6px}"
    "input,select{box-sizing:border-box;width:100%;height:44px;padding:0 12px;font:inherit;"
    "border:1px solid #dfdad3;border-radius:10px;background:#fff;color:inherit}"
    "small{display:block;color:#6c6660;font-size:13px;margin-top:4px}"
    "button{margin-top:24px;width:100%;height:44px;border:0;border-radius:999px;"
    "background:#ffcc00;color:#14110f;font:inherit;font-weight:500}"
    "h2{font-size:18px;margin:0 0 12px}.card{margin-bottom:16px;background:#fff;border:1px solid #dfdad3;border-radius:18px;padding:20px}"
    "</style></head><body><main>";
static const char PAGE_TAIL[] = "</main></body></html>";

static void send_escaped(httpd_req_t *req, const char *s) {
    char buf[64];
    size_t o = 0;
    for (; *s; s++) {
        const char *rep = NULL;
        switch (*s) {
        case '<': rep = "&lt;"; break;
        case '>': rep = "&gt;"; break;
        case '&': rep = "&amp;"; break;
        case '"': rep = "&quot;"; break;
        case '\'': rep = "&#39;"; break;
        default: break;
        }
        size_t n = rep ? strlen(rep) : 1;
        if (o + n >= sizeof(buf)) {
            httpd_resp_send_chunk(req, buf, (ssize_t)o);
            o = 0;
        }
        if (rep) {
            memcpy(buf + o, rep, n);
        } else {
            buf[o] = *s;
        }
        o += n;
    }
    if (o) httpd_resp_send_chunk(req, buf, (ssize_t)o);
}

static esp_err_t page_get(httpd_req_t *req) {
    static char ssids[APP_NET_SCAN_MAX][33];
    int n = app_net_scan_results(ssids, APP_NET_SCAN_MAX);
    net_status_t st = app_net_status();

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    httpd_resp_sendstr_chunk(req, "<h1>Astra Monitor</h1><p>为设备连接 Wi-Fi,并设置 Lynn 的 Grok Bot connector。设备 MAC ");
    send_escaped(req, st.mac);
    httpd_resp_sendstr_chunk(req, "</p><section class=card><h2>已保存的 Wi-Fi</h2>"
        "<small>最多保存 5 个网络,断线后自动轮换尝试。密码不会显示在网页上。</small>");
    char saved[WIFI_PROFILES_MAX][33];
    int saved_count = app_net_saved_networks(saved, WIFI_PROFILES_MAX);
    for (int i = 0; i < saved_count; i++) {
        httpd_resp_sendstr_chunk(req, "<form method=post action=/save><input type=hidden name=ssid value=\"");
        send_escaped(req, saved[i]);
        httpd_resp_sendstr_chunk(req, "\"><input type=hidden name=pass value=''><input type=hidden name=token value=''><p>");
        send_escaped(req, saved[i]);
        if (st.link == NET_LINK_ONLINE && strcmp(st.ssid, saved[i]) == 0) {
            httpd_resp_sendstr_chunk(req, " · 当前连接");
        }
        httpd_resp_sendstr_chunk(req, "</p><button type=submit>连接此网络</button>"
            "<button type=submit name=action value=forget>删除此配置</button></form>");
    }
    if (!saved_count) httpd_resp_sendstr_chunk(req, "<p>尚未保存网络</p>");
    httpd_resp_sendstr_chunk(req, "</section><form class=card method=post action=/save>"
        "<h2>添加或更新 Wi-Fi</h2><label for=ssid>Wi-Fi 名称</label>"
        "<input id=ssid name=ssid list=nets maxlength=32 autocomplete=off required");
    httpd_resp_sendstr_chunk(req, "><datalist id=nets>");
    for (int i = 0; i < n; i++) {
        httpd_resp_sendstr_chunk(req, "<option value=\"");
        send_escaped(req, ssids[i]);
        httpd_resp_sendstr_chunk(req, "\">");
    }
    httpd_resp_sendstr_chunk(req,
        "</datalist><small>仅支持 2.4GHz;可从附近网络中选择</small>"
        "<label for=pass>Wi-Fi 密码</label>"
        "<input id=pass name=pass type=password maxlength=63 autocomplete=off>"
        "<small>已保存的同名网络留空保留密码;新网络留空表示开放网络。</small>"
        "<input type=hidden name=token value=''>"
        "<button type=submit>保存并连接 Wi-Fi</button></form>"
        "<form class=card method=post action=/save><h2>服务设置</h2>"
        "<input type=hidden name=ssid value=''><input type=hidden name=pass value=''>"
        "<label for=token>GitHub token</label>"
        "<input id=token name=token type=password maxlength=120 autocomplete=off "
        "placeholder=\"github_pat_…\">");
    httpd_resp_sendstr_chunk(req, app_settings_has_token()
        ? "<small>设备上已有 token,留空则保持不变</small>"
        : "<small>只读权限即可:Metadata、Issues、Pull requests</small>");
    grok_config_t grok;
    bool configured = app_settings_grok(&grok);
    httpd_resp_sendstr_chunk(req, "<label for=connector_url>Connector 公网 HTTPS URL</label>"
        "<input id=connector_url name=connector_url type=url maxlength=255 placeholder='https://connector.example.com' value=\"");
    send_escaped(req, grok.url);
    httpd_resp_sendstr_chunk(req, "\"><label for=connector_token>Connector Bearer token</label>"
        "<input id=connector_token name=connector_token type=password maxlength=191 autocomplete=off>"
        "<small>留空保留已有 token;此处填写 connector token,不是 Grok API key</small>"
        "<label for=bot_id>Grok Bot ID</label>"
        "<input id=bot_id name=bot_id maxlength=64 value=\"");
    send_escaped(req, grok.bot);
    httpd_resp_sendstr_chunk(req, configured ? "\"><small>Connector 已配置</small>" : "\"><small>三个字段首次需全部填写</small>");
    memset(&grok, 0, sizeof(grok));
    httpd_resp_sendstr_chunk(req, "<button type=submit>保存服务设置</button></form>");
    httpd_resp_sendstr_chunk(req, PAGE_TAIL);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t reply(httpd_req_t *req, const char *status, const char *title, const char *text) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    httpd_resp_sendstr_chunk(req, "<div class=card><h1>");
    send_escaped(req, title);
    httpd_resp_sendstr_chunk(req, "</h1><p>");
    send_escaped(req, text);
    httpd_resp_sendstr_chunk(req, "</p><a href=/>返回</a></div>");
    httpd_resp_sendstr_chunk(req, PAGE_TAIL);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t save_post(httpd_req_t *req) {
    if (req->content_len == 0 || req->content_len > BODY_MAX) {
        return reply(req, "413 Payload Too Large", "提交内容过长", "请检查输入后重试。");
    }
    static char body[BODY_MAX + 1];
    size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) {
            memset(body, 0, sizeof(body));
            return ESP_FAIL;
        }
        got += (size_t)r;
    }
    body[got] = '\0';

    char ssid[33] = "", pass[64] = "", token[APP_TOKEN_MAX] = "";
    bool ok = portal_form_get(body, "ssid", ssid, sizeof(ssid)) &&
              portal_form_get(body, "pass", pass, sizeof(pass)) &&
              portal_form_get(body, "token", token, sizeof(token));
    char action[16] = "";
    bool action_field = strstr(body, "action=") != NULL;
    if (action_field && !portal_form_get(body, "action", action, sizeof(action))) ok = false;
    if (ok && strcmp(action, "forget") == 0) {
        memset(body, 0, sizeof(body));
        memset(pass, 0, sizeof(pass));
        memset(token, 0, sizeof(token));
        esp_err_t err = app_net_forget_network(ssid);
        return err == ESP_OK
            ? reply(req, "200 OK", "已删除 Wi-Fi", "其它网络和服务设置保持不变。")
            : reply(req, "409 Conflict", "暂时无法删除", "请等待试连结束后重试,并确认设备存储可用。");
    }
    if (action[0]) ok = false;
    grok_config_t grok;
    app_settings_grok(&grok);
    char connector_url[GROK_URL_MAX] = "", connector_token[GROK_TOKEN_MAX] = "", bot[GROK_BOT_MAX] = "";
    // Missing optional connector fields keep compatibility with old saved forms.
    bool connector_fields = strstr(body, "connector_url=") || strstr(body, "connector_token=") || strstr(body, "bot_id=");
    if (connector_fields) {
        ok = ok && portal_form_get(body, "connector_url", connector_url, sizeof(connector_url)) &&
             portal_form_get(body, "connector_token", connector_token, sizeof(connector_token)) &&
             portal_form_get(body, "bot_id", bot, sizeof(bot));
    }
    bool grok_update = connector_url[0] || connector_token[0] || bot[0];
    if (connector_url[0]) strlcpy(grok.url, connector_url, sizeof(grok.url));
    if (connector_token[0]) strlcpy(grok.token, connector_token, sizeof(grok.token));
    if (bot[0]) strlcpy(grok.bot, bot, sizeof(grok.bot));
    memset(body, 0, sizeof(body));

    esp_err_t res;
    if (!ok) {
        res = reply(req, "400 Bad Request", "无法读取表单", "Wi-Fi 名称最多 32 字节,密码最多 63 字节。");
    } else if (token[0] && !portal_token_valid(token)) {
        res = reply(req, "400 Bad Request", "token 格式不对",
                    "应为 github_pat_ 或 ghp_ 开头,只含字母、数字和下划线。");
    } else if (grok_update && !grok_config_valid(&grok)) {
        res = reply(req, "400 Bad Request", "Connector 设置不完整", "请填写公网 HTTPS URL、Bearer token 和 Bot ID。URL 不含查询参数、片段或用户名。");
    } else if (!ssid[0] && !token[0] && !grok_update) {
        res = reply(req, "200 OK", "配置保持不变", "已保存的 Wi-Fi 和服务设置无需重新填写。");
    } else if (pass[0] && strlen(pass) < 8) {
        res = reply(req, "400 Bad Request", "Wi-Fi 密码太短", "WPA2 密码至少 8 位;开放网络请留空。");
    } else {
        esp_err_t saved = grok_update ? app_settings_set_grok(&grok) : ESP_OK;
        if (saved == ESP_OK && token[0]) saved = app_settings_set_token(token);
        if (saved != ESP_OK) {
            res = reply(req, "500 Internal Server Error", "保存失败", "设备存储暂时不可用,请重试。");
        } else if (ssid[0]) {
            esp_err_t e = app_net_try_credentials(ssid, pass);
            ESP_LOGI(TAG, "试连 Wi-Fi: %s", esp_err_to_name(e));
            res = e == ESP_OK
                ? reply(req, "200 OK", "正在连接", "请看设备屏幕上的结果。连接成功后保存此网络并保留其它配置,热点会自动关闭;失败不覆盖原配置,可回到本页重试。")
                : e == ESP_ERR_INVALID_ARG
                    ? reply(req, "400 Bad Request", "无法添加 Wi-Fi", "请检查名称和密码;最多保存 5 个网络,已满时请先删除一个配置。")
                    : reply(req, "409 Conflict", "暂时无法连接", "设备正在试连上一个网络,请稍候再提交。");
        } else {
            res = reply(req, "200 OK", "设置已保存", "返回 Lynn 页按 OK 说话;设备会在联网后连接 connector。");
        }
    }
    memset(pass, 0, sizeof(pass));
    memset(token, 0, sizeof(token));
    memset(connector_token, 0, sizeof(connector_token));
    memset(&grok, 0, sizeof(grok));
    return res;
}

// 手机系统的联网探测(generate_204、hotspot-detect.html 等)一律重定向到首页,
// 让系统弹出"登录此网络"页面。
static esp_err_t redirect_get(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" APP_PORTAL_IP "/");
    return httpd_resp_send(req, NULL, 0);
}

// ---------------------------------------------------------------------------
// DNS:所有 A 记录查询都回答本机地址
// ---------------------------------------------------------------------------

static void dns_task(void *arg) {
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(53),
                                .sin_addr.s_addr = htonl(INADDR_ANY) };
    struct timeval tv = { .tv_sec = 1 };
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        ESP_LOGE(TAG, "DNS socket 失败");
        s_dns_run = false;
    }
    static uint8_t req[512], resp[528];
    const uint8_t ip[4] = { 192, 168, 4, 1 };
    while (s_dns_run) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, req, sizeof(req), 0, (struct sockaddr *)&from, &flen);
        if (n <= 0) continue;
        size_t r = portal_dns_answer(req, (size_t)n, ip, resp, sizeof(resp));
        if (r) sendto(sock, resp, r, 0, (struct sockaddr *)&from, flen);
    }
    if (sock >= 0) close(sock);
    xSemaphoreGive(s_dns_done);
    vTaskDelete(NULL);
}

esp_err_t app_portal_start(void) {
    if (s_http) return ESP_OK;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_open_sockets = 3;
    cfg.backlog_conn = 2;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 4;
    cfg.stack_size = 6144;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    esp_err_t err = httpd_start(&s_http, &cfg);
    if (err != ESP_OK) {
        s_http = NULL;
        return err;
    }
    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = page_get },
        { .uri = "/save", .method = HTTP_POST, .handler = save_post },
        { .uri = "/*", .method = HTTP_GET, .handler = redirect_get },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) httpd_register_uri_handler(s_http, &uris[i]);

    s_dns_done = xSemaphoreCreateBinary();
    s_dns_run = true;
    if (!s_dns_done || xTaskCreate(dns_task, "portal_dns", 3072, NULL, 3, &s_dns_task) != pdPASS) {
        s_dns_run = false;
        app_portal_stop();
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "配网网页已启动 http://" APP_PORTAL_IP "/");
    return ESP_OK;
}

void app_portal_stop(void) {
    if (s_dns_task) {
        s_dns_run = false;
        xSemaphoreTake(s_dns_done, pdMS_TO_TICKS(3000));   // recvfrom 超时 1 秒后退出
        s_dns_task = NULL;
    }
    if (s_dns_done) {
        vSemaphoreDelete(s_dns_done);
        s_dns_done = NULL;
    }
    if (s_http) {
        httpd_stop(s_http);
        s_http = NULL;
    }
}
