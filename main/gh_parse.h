// main/gh_parse.h —— 把 GitHub GraphQL 响应(见 gh_fetch.c 的查询)解析成 gh_snapshot_t。
// 只依赖 cJSON,host 测试用 ESP-IDF 自带的 cJSON 源码编译。
#pragma once

#include "gh_model.h"

typedef enum {
    GH_PARSE_OK = 0,
    GH_PARSE_BAD_JSON,      // 不是合法 JSON 或缺少 data
    GH_PARSE_API_ERROR,     // 响应带 errors 字段(token 权限、组织名错误等)
    GH_PARSE_NO_MEMORY,
} gh_parse_result_t;

// 成功时覆盖 *out(fetched_at 置为 now)。失败时 *out 不变;
// err_msg 非 NULL 时写入 API 返回的首条错误信息(UTF-8 安全截断)。
// 有 data 又有 errors(fine-grained token 对部分字段无权限,字段为 null)时仍返回
// GH_PARSE_OK,但置 out->partial 并写入首条错误,调用方应提示用户检查 token 权限。
gh_parse_result_t gh_parse_snapshot(const char *json, size_t len, int64_t now,
                                    gh_snapshot_t *out, char *err_msg, size_t err_cap);

// 第二个请求:为 s->repos 中 idx[] 指定的仓库各取最近更新的 3 个 issue 和 3 个 PR。
// 生成完整的 JSON 请求体;仓库名含非常规字符时跳过该仓库。返回写入长度,0 = 缓冲区不足。
size_t gh_activity_query(char *buf, size_t cap, const char *org, const gh_snapshot_t *s,
                         const uint8_t *idx, int n);

// 解析第二个请求的响应,把 issue/PR 插入 s->feed(按时间排序,最多 GH_MAX_FEED)。
gh_parse_result_t gh_parse_activity(const char *json, size_t len, gh_snapshot_t *s,
                                    char *err_msg, size_t err_cap);
