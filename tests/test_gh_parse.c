// tests/test_gh_parse.c —— GraphQL 响应解析:正常快照、API 错误与非法输入。
#include "gh_parse.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    assert(buf && fread(buf, 1, (size_t)n, f) == (size_t)n);
    buf[n] = '\0';
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static void test_sample(const char *fixture) {
    size_t len;
    char *json = read_file(fixture, &len);
    gh_snapshot_t s;
    memset(&s, 0xAB, sizeof(s));
    assert(gh_parse_snapshot(json, len, 1234, &s, NULL, 0) == GH_PARSE_OK);
    free(json);

    assert(s.fetched_at == 1234);
    assert(s.repo_total == 3 && s.repo_count == 3);
    assert(strcmp(s.repos[0].name, "Nebula") == 0);
    assert(s.repos[0].stars == 120 && s.repos[0].issues == 4 && s.repos[0].prs == 2);
    assert(s.repos[1].is_private && !s.repos[0].is_private);
    assert(s.repos[2].pushed_at == 0);   // null pushedAt
    assert(s.total_stars == 127 && s.total_issues == 5 && s.total_prs == 3);
    assert(s.rate_remaining == 4987);
    assert(!s.partial);

    // 2 star + 3 issue/PR(Discussion 被忽略),按时间降序。
    assert(s.feed_count == 5);
    assert(s.feed[0].kind == GH_EV_PR_MERGED && s.feed[0].number == 42);
    assert(strcmp(s.feed[0].title, "修复 同步 问题") == 0);
    assert(s.feed[1].kind == GH_EV_STAR && strcmp(s.feed[1].actor, "stargazer-a") == 0);
    assert(s.feed[2].kind == GH_EV_ISSUE_OPEN && strcmp(s.feed[2].actor, "ghost") == 0);
    assert(s.feed[3].kind == GH_EV_PR_CLOSED);
    assert(s.feed[4].kind == GH_EV_STAR && strcmp(s.feed[4].repo, "Orbit") == 0);
}

static void test_partial(void) {
    // fine-grained token 对部分字段无权限:data 里是 null,errors 说明原因。
    const char *json =
        "{\"data\":{\"organization\":{\"repositories\":{\"totalCount\":2,\"nodes\":["
        "{\"name\":\"A\",\"isPrivate\":false,\"stargazerCount\":null,\"pushedAt\":null,"
        "\"issues\":null,\"pullRequests\":null,\"stargazers\":null},null]}},"
        "\"search\":{\"nodes\":[null]},\"rateLimit\":{\"remaining\":10}},"
        "\"errors\":[{\"type\":\"FORBIDDEN\",\"message\":\"Resource not accessible by personal access token\"}]}";
    gh_snapshot_t s;
    char msg[64];
    assert(gh_parse_snapshot(json, strlen(json), 5, &s, msg, sizeof(msg)) == GH_PARSE_OK);
    assert(s.partial && s.repo_count == 1 && s.total_stars == 0 && s.rate_remaining == 10);
    assert(strcmp(msg, "Resource not accessible by personal access token") == 0);
}

static void test_errors(void) {
    gh_snapshot_t s = { .repo_count = 9 };
    char msg[32];
    const char *api = "{\"data\":{\"organization\":null},\"errors\":[{\"message\":\"Could not resolve to an Organization\"}]}";
    assert(gh_parse_snapshot(api, strlen(api), 1, &s, msg, sizeof(msg)) == GH_PARSE_API_ERROR);
    assert(strncmp(msg, "Could not resolve", 17) == 0 && strlen(msg) < sizeof(msg));
    assert(s.repo_count == 9);   // 失败不改动输出

    const char *rest = "{\"message\":\"Bad credentials\",\"documentation_url\":\"x\"}";
    assert(gh_parse_snapshot(rest, strlen(rest), 1, &s, msg, sizeof(msg)) == GH_PARSE_API_ERROR);
    assert(strcmp(msg, "Bad credentials") == 0);

    const char *bad = "{\"data\":";
    assert(gh_parse_snapshot(bad, strlen(bad), 1, &s, msg, sizeof(msg)) == GH_PARSE_BAD_JSON);
    const char *empty = "{}";
    assert(gh_parse_snapshot(empty, strlen(empty), 1, &s, NULL, 0) == GH_PARSE_BAD_JSON);
}

static void test_activity(void) {
    gh_snapshot_t s = { 0 };
    snprintf(s.repos[0].name, GH_NAME_LEN, "Nebula");
    snprintf(s.repos[1].name, GH_NAME_LEN, "bad\"name");   // 非常规仓库名被跳过
    snprintf(s.repos[2].name, GH_NAME_LEN, "astra.si");
    s.repo_count = 3;
    uint8_t idx[3] = { 0, 1, 2 };
    char q[2048];
    size_t n = gh_activity_query(q, sizeof(q), "Org", &s, idx, 3);
    assert(n > 0 && n == strlen(q));
    assert(strstr(q, "r0:repository(owner:\\\"Org\\\",name:\\\"Nebula\\\")"));
    assert(!strstr(q, "r1:") && strstr(q, "r2:repository(owner:\\\"Org\\\",name:\\\"astra.si\\\")"));
    assert(strncmp(q, "{\"query\":\"query{", 16) == 0 && strcmp(q + n - 3, "}\"}") == 0);
    assert(gh_activity_query(q, 64, "Org", &s, idx, 3) == 0);

    const char *json =
        "{\"data\":{\"r0\":{\"issues\":{\"nodes\":[{\"__typename\":\"Issue\",\"number\":5,\"title\":\"Bug\","
        "\"state\":\"OPEN\",\"updatedAt\":\"2026-11-04T01:00:00Z\",\"author\":{\"login\":\"a\"},"
        "\"repository\":{\"name\":\"Nebula\"}}]},\"pullRequests\":{\"nodes\":[{\"__typename\":\"PullRequest\","
        "\"merged\":true,\"number\":6,\"title\":\"Fix\",\"state\":\"MERGED\",\"updatedAt\":\"2026-11-04T02:00:00Z\","
        "\"author\":null,\"repository\":{\"name\":\"Nebula\"}}]}},\"r2\":null}}";
    char msg[32];
    assert(gh_parse_activity(json, strlen(json), &s, msg, sizeof(msg)) == GH_PARSE_OK);
    assert(s.feed_count == 2 && s.feed[0].kind == GH_EV_PR_MERGED && s.feed[1].kind == GH_EV_ISSUE_OPEN);
    assert(strcmp(s.feed[0].actor, "ghost") == 0 && msg[0] == '\0');
    assert(gh_parse_activity("{", 1, &s, msg, sizeof(msg)) == GH_PARSE_BAD_JSON);
}

int main(int argc, char **argv) {
    test_activity();
    test_sample(argc > 1 ? argv[1] : "tests/fixtures/gh_graphql_sample.json");
    test_partial();
    test_errors();
    puts("test_gh_parse: PASS");
    return 0;
}
