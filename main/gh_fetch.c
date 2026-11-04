// main/gh_fetch.c —— 见 gh_fetch.h。
#include "gh_fetch.h"

#include "gh_parse.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "gh_fetch";

// 第一个请求:星数最多的 24 个仓库的计数与推送时间,外加剩余配额。
// 只请求 fine-grained token(Metadata/Issues/Pull requests 只读)能读到的字段:
// stargazers 连接(谁点了星)会被拒绝,且因字段不可空而把整个仓库置为 null;
// search 对这类 token 返回空结果。所以星标动态改由快照差值生成(gh_carry_star_events),
// issue/PR 动态由第二个请求按仓库拉取(gh_activity_query)。
#define QUERY_FMT \
    "{\"query\":\"query{organization(login:\\\"%s\\\"){repositories(first:24," \
    "orderBy:{field:STARGAZERS,direction:DESC}){totalCount nodes{name isPrivate stargazerCount " \
    "pushedAt issues(states:OPEN){totalCount} pullRequests(states:OPEN){totalCount}}}} " \
    "rateLimit{remaining resetAt}}\"}"

#define RESPONSE_MAX   (20 * 1024)
#define ACTIVITY_REPOS 6
#define ACTIVITY_BODY  (ACTIVITY_REPOS * 360 + 64)

static bool org_is_safe(const char *org) {
    if (!*org || strlen(org) > 39) return false;
    for (const char *p = org; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-')) {
            return false;
        }
    }
    return true;
}

// 发送一次 GraphQL POST,把响应读入 resp(以 NUL 结尾)。返回 HTTP 状态码,网络错误返回 -1。
static int post(const char *auth, const char *body, int body_len, char *resp, int *resp_len,
                char *detail, size_t detail_cap) {
    esp_http_client_config_t cfg = {
        .url = "https://api.github.com/graphql",
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        snprintf(detail, detail_cap, "no client");
        return -1;
    }
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "User-Agent", "AstraMonitor-Passport");
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");

    int status = -1;
    int total = 0;
    esp_err_t err = esp_http_client_open(client, body_len);
    if (err != ESP_OK) {
        snprintf(detail, detail_cap, "%s", esp_err_to_name(err));
    } else if (esp_http_client_write(client, body, body_len) != body_len) {
        snprintf(detail, detail_cap, "write failed");
    } else {
        esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        while (total < RESPONSE_MAX) {
            int n = esp_http_client_read(client, resp + total, RESPONSE_MAX - total);
            if (n < 0) {
                snprintf(detail, detail_cap, "read failed");
                status = -1;
                break;
            }
            if (n == 0) break;
            total += n;
        }
        if (total >= RESPONSE_MAX) {
            snprintf(detail, detail_cap, "response too large");
            status = -2;
        }
    }
    resp[total < RESPONSE_MAX ? total : RESPONSE_MAX] = '\0';
    *resp_len = total;
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return status;
}

// 第二个请求:最近推送的几个仓库的最新 issue/PR。失败只记日志,不影响计数结果。
static void fetch_activity(const char *org, const char *auth, char *resp, gh_snapshot_t *out) {
    uint8_t idx[ACTIVITY_REPOS];
    int n = gh_pick_active_repos(out, idx, ACTIVITY_REPOS);
    if (n == 0) return;
    char *body = malloc(ACTIVITY_BODY);
    if (!body) return;
    size_t len = gh_activity_query(body, ACTIVITY_BODY, org, out, idx, n);
    char detail[48] = "";
    int total = 0;
    int status = len ? post(auth, body, (int)len, resp, &total, detail, sizeof(detail)) : -3;
    free(body);
    char msg[160];
    gh_parse_result_t pr = status == 200
        ? gh_parse_activity(resp, (size_t)total, out, msg, sizeof(msg)) : GH_PARSE_BAD_JSON;
    if (status == 200 && msg[0]) ESP_LOGW(TAG, "动态部分字段无权限: %s", msg);
    ESP_LOGI(TAG, "动态:%d 个仓库,HTTP %d,%d bytes,parse=%d,feed=%u %s", n, status, total, pr,
             out->feed_count, detail);
}

gh_fetch_result_t gh_fetch_snapshot(const char *org, const char *token, int64_t now,
                                    gh_snapshot_t *out, char *detail, size_t detail_cap) {
    detail[0] = '\0';
    if (!token || !*token) return GH_FETCH_NO_TOKEN;
    if (!org_is_safe(org)) {
        snprintf(detail, detail_cap, "bad org name");
        return GH_FETCH_BAD_RESPONSE;
    }

    size_t body_cap = sizeof(QUERY_FMT) + strlen(org);
    size_t auth_cap = strlen(token) + 8;
    char *body = malloc(body_cap);
    char *auth = malloc(auth_cap);
    char *resp = malloc(RESPONSE_MAX + 1);
    gh_fetch_result_t result = GH_FETCH_NO_MEMORY;
    if (!body || !auth || !resp) goto done;
    int body_len = snprintf(body, body_cap, QUERY_FMT, org);
    snprintf(auth, auth_cap, "bearer %s", token);

    int total = 0;
    int status = post(auth, body, body_len, resp, &total, detail, detail_cap);
    if (status < 0) {
        result = status == -2 ? GH_FETCH_BAD_RESPONSE : GH_FETCH_NETWORK;
        goto done;
    }

    char msg[160];
    gh_parse_result_t pr = gh_parse_snapshot(resp, (size_t)total, now, out, msg, sizeof(msg));
    if (status == 401) {
        result = GH_FETCH_AUTH;
        snprintf(detail, detail_cap, "%s", msg[0] ? msg : "401");
    } else if (status == 403 || status == 429) {
        result = strstr(resp, "rate limit") ? GH_FETCH_RATE_LIMITED : GH_FETCH_AUTH;
        snprintf(detail, detail_cap, "%s", msg[0] ? msg : "403");
    } else if (status != 200) {
        result = GH_FETCH_BAD_RESPONSE;
        snprintf(detail, detail_cap, "HTTP %d", status);
    } else if (pr == GH_PARSE_OK) {
        result = GH_FETCH_OK;
        if (out->partial) {
            // 常见原因:token 未被组织批准,或缺少 Metadata/Issues/Pull requests 只读权限。
            ESP_LOGW(TAG, "部分字段无权限: %s", msg);
            snprintf(detail, detail_cap, "%s", msg);
        }
    } else if (pr == GH_PARSE_NO_MEMORY) {
        result = GH_FETCH_NO_MEMORY;
    } else {
        result = GH_FETCH_BAD_RESPONSE;
        snprintf(detail, detail_cap, "%s", msg[0] ? msg : "bad json");
    }
    ESP_LOGI(TAG, "仓库:HTTP %d,%d bytes,%u 个,result=%d", status, total, out->repo_count, result);
    if (result == GH_FETCH_OK) fetch_activity(org, auth, resp, out);

done:
    if (auth) {
        memset(auth, 0, auth_cap);
        free(auth);
    }
    free(body);
    free(resp);
    return result;
}
