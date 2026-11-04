// main/gh_fetch.h —— 通过 HTTPS 调用 GitHub GraphQL,得到组织快照。
// 阻塞(数秒),只在同步任务里调用;需要已联网。
#pragma once

#include "esp_err.h"
#include "gh_model.h"

#include <stddef.h>

typedef enum {
    GH_FETCH_OK = 0,
    GH_FETCH_NO_TOKEN,
    GH_FETCH_NETWORK,       // 连接/TLS/超时
    GH_FETCH_AUTH,          // 401/403:token 无效或权限不足
    GH_FETCH_RATE_LIMITED,
    GH_FETCH_BAD_RESPONSE,  // 非 200 或解析失败
    GH_FETCH_NO_MEMORY,
} gh_fetch_result_t;

// 成功时覆盖 *out。detail 写入简短的英文/服务端错误描述,便于在设备页展示。
gh_fetch_result_t gh_fetch_snapshot(const char *org, const char *token, int64_t now,
                                    gh_snapshot_t *out, char *detail, size_t detail_cap);
