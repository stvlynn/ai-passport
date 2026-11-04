// main/gh_parse.c —— 见 gh_parse.h。
#include "gh_parse.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const cJSON *path(const cJSON *node, const char *a, const char *b) {
    node = cJSON_GetObjectItemCaseSensitive(node, a);
    return b ? cJSON_GetObjectItemCaseSensitive(node, b) : node;
}

static const char *str(const cJSON *node, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(node, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static uint32_t count(const cJSON *node, const char *key) {
    const cJSON *v = path(node, key, "totalCount");
    return cJSON_IsNumber(v) && v->valuedouble > 0 ? (uint32_t)v->valuedouble : 0;
}

static void parse_repos(const cJSON *repos, gh_snapshot_t *s) {
    const cJSON *total = cJSON_GetObjectItemCaseSensitive(repos, "totalCount");
    s->repo_total = cJSON_IsNumber(total) ? (uint16_t)total->valuedouble : 0;
    const cJSON *node;
    cJSON_ArrayForEach(node, cJSON_GetObjectItemCaseSensitive(repos, "nodes")) {
        if (s->repo_count >= GH_MAX_REPOS) break;
        // 无权限的仓库在 nodes 里是 null:跳过,不当成一个空名仓库。
        if (!cJSON_IsObject(node)) continue;
        gh_repo_t *r = &s->repos[s->repo_count++];
        gh_copy_utf8(r->name, sizeof(r->name), str(node, "name"));
        const cJSON *stars = cJSON_GetObjectItemCaseSensitive(node, "stargazerCount");
        r->stars = cJSON_IsNumber(stars) && stars->valuedouble > 0 ? (uint32_t)stars->valuedouble : 0;
        r->issues = count(node, "issues");
        r->prs = count(node, "pullRequests");
        r->is_private = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(node, "isPrivate"));
        r->pushed_at = gh_parse_iso8601(str(node, "pushedAt"));

        const cJSON *edge = cJSON_GetArrayItem(path(node, "stargazers", "edges"), 0);
        if (edge) {
            gh_feed_item_t it = { .kind = GH_EV_STAR };
            it.at = gh_parse_iso8601(str(edge, "starredAt"));
            gh_copy_utf8(it.repo, sizeof(it.repo), r->name);
            gh_copy_utf8(it.actor, sizeof(it.actor), str(cJSON_GetObjectItemCaseSensitive(edge, "node"), "login"));
            if (it.at) gh_feed_insert(s, &it);
        }
    }
}

static void parse_search(const cJSON *nodes, gh_snapshot_t *s) {
    const cJSON *node;
    cJSON_ArrayForEach(node, nodes) {
        if (!cJSON_IsObject(node)) continue;
        const char *type = str(node, "__typename");
        const char *state = str(node, "state");
        gh_feed_item_t it = { 0 };
        if (strcmp(type, "PullRequest") == 0) {
            if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(node, "merged"))) {
                it.kind = GH_EV_PR_MERGED;
            } else {
                it.kind = strcmp(state, "OPEN") == 0 ? GH_EV_PR_OPEN : GH_EV_PR_CLOSED;
            }
        } else if (strcmp(type, "Issue") == 0) {
            it.kind = strcmp(state, "OPEN") == 0 ? GH_EV_ISSUE_OPEN : GH_EV_ISSUE_CLOSED;
        } else {
            continue;
        }
        const cJSON *num = cJSON_GetObjectItemCaseSensitive(node, "number");
        it.number = cJSON_IsNumber(num) ? (uint32_t)num->valuedouble : 0;
        it.at = gh_parse_iso8601(str(node, "updatedAt"));
        gh_copy_utf8(it.title, sizeof(it.title), str(node, "title"));
        gh_copy_utf8(it.repo, sizeof(it.repo), str(cJSON_GetObjectItemCaseSensitive(node, "repository"), "name"));
        // author 可能为 null(已删除的账号 "ghost")。
        const char *login = str(cJSON_GetObjectItemCaseSensitive(node, "author"), "login");
        gh_copy_utf8(it.actor, sizeof(it.actor), *login ? login : "ghost");
        if (it.at) gh_feed_insert(s, &it);
    }
}

gh_parse_result_t gh_parse_snapshot(const char *json, size_t len, int64_t now,
                                    gh_snapshot_t *out, char *err_msg, size_t err_cap) {
    if (err_msg && err_cap) err_msg[0] = '\0';
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return GH_PARSE_BAD_JSON;

    gh_parse_result_t result = GH_PARSE_OK;
    const cJSON *errors = cJSON_GetObjectItemCaseSensitive(root, "errors");
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    const cJSON *repos = path(path(data, "organization", NULL), "repositories", NULL);
    if (cJSON_GetArraySize(errors) > 0 && !cJSON_IsObject(repos)) {
        if (err_msg) gh_copy_utf8(err_msg, err_cap, str(cJSON_GetArrayItem(errors, 0), "message"));
        result = GH_PARSE_API_ERROR;
    } else if (!cJSON_IsObject(repos)) {
        // REST 风格错误(如 401 Bad credentials)只有 message 字段。
        if (err_msg) gh_copy_utf8(err_msg, err_cap, str(root, "message"));
        result = err_msg && err_msg[0] ? GH_PARSE_API_ERROR : GH_PARSE_BAD_JSON;
    } else {
        gh_snapshot_t *s = calloc(1, sizeof(*s));
        if (!s) {
            result = GH_PARSE_NO_MEMORY;
        } else {
            s->rate_remaining = -1;
            parse_repos(repos, s);
            parse_search(path(path(data, "search", NULL), "nodes", NULL), s);
            const cJSON *rem = path(data, "rateLimit", "remaining");
            if (cJSON_IsNumber(rem)) s->rate_remaining = (int32_t)rem->valuedouble;
            gh_snapshot_totals(s);
            s->fetched_at = now;
            if (cJSON_GetArraySize(errors) > 0) {
                s->partial = true;
                if (err_msg) gh_copy_utf8(err_msg, err_cap, str(cJSON_GetArrayItem(errors, 0), "message"));
            }
            *out = *s;
            free(s);
        }
    }
    cJSON_Delete(root);
    return result;
}

static bool name_is_safe(const char *name) {
    if (!*name) return false;
    for (const char *p = name; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '-' || *p == '_' || *p == '.')) {
            return false;
        }
    }
    return true;
}

#define ACT_FIELDS "number title state updatedAt author{login} repository{name}"

size_t gh_activity_query(char *buf, size_t cap, const char *org, const gh_snapshot_t *s,
                         const uint8_t *idx, int n) {
    size_t o = 0;
    int w = snprintf(buf, cap, "{\"query\":\"query{");
    if (w < 0 || (size_t)w >= cap) return 0;
    o = (size_t)w;
    for (int i = 0; i < n; i++) {
        const char *name = s->repos[idx[i]].name;
        if (!name_is_safe(name)) continue;
        w = snprintf(buf + o, cap - o,
                     "r%d:repository(owner:\\\"%s\\\",name:\\\"%s\\\"){"
                     "issues(first:3,orderBy:{field:UPDATED_AT,direction:DESC}){nodes{__typename " ACT_FIELDS "}} "
                     "pullRequests(first:3,orderBy:{field:UPDATED_AT,direction:DESC}){nodes{__typename merged "
                     ACT_FIELDS "}}} ",
                     i, org, name);
        if (w < 0 || (size_t)w >= cap - o) return 0;
        o += (size_t)w;
    }
    w = snprintf(buf + o, cap - o, "}\"}");
    if (w < 0 || (size_t)w >= cap - o) return 0;
    return o + (size_t)w;
}

gh_parse_result_t gh_parse_activity(const char *json, size_t len, gh_snapshot_t *s,
                                    char *err_msg, size_t err_cap) {
    if (err_msg && err_cap) err_msg[0] = '\0';
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return GH_PARSE_BAD_JSON;
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    const cJSON *errors = cJSON_GetObjectItemCaseSensitive(root, "errors");
    if (cJSON_GetArraySize(errors) > 0 && err_msg) {
        gh_copy_utf8(err_msg, err_cap, str(cJSON_GetArrayItem(errors, 0), "message"));
    }
    gh_parse_result_t result = GH_PARSE_OK;
    if (!cJSON_IsObject(data)) {
        result = cJSON_GetArraySize(errors) > 0 ? GH_PARSE_API_ERROR : GH_PARSE_BAD_JSON;
    } else {
        const cJSON *repo;
        cJSON_ArrayForEach(repo, data) {
            if (!cJSON_IsObject(repo)) continue;   // 无权限的仓库为 null
            parse_search(path(repo, "issues", "nodes"), s);
            parse_search(path(repo, "pullRequests", "nodes"), s);
        }
    }
    cJSON_Delete(root);
    return result;
}
