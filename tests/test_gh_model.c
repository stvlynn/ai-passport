// tests/test_gh_model.c —— gh_model 的快照汇总、动态排序、变化检测与格式化。
#include "gh_model.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static gh_feed_item_t item(gh_ev_kind_t kind, const char *repo, uint32_t number, int64_t at) {
    gh_feed_item_t it = { .kind = kind, .number = number, .at = at };
    snprintf(it.repo, sizeof(it.repo), "%s", repo);
    snprintf(it.actor, sizeof(it.actor), "u%u", (unsigned)number);
    return it;
}

static void test_iso8601(void) {
    assert(gh_parse_iso8601("1970-01-01T00:00:00Z") == 0);
    assert(gh_parse_iso8601("2026-11-04T05:48:45Z") == 1793771325);
    assert(gh_parse_iso8601("2024-02-29T23:59:59Z") == 1709251199);
    assert(gh_parse_iso8601("2026-13-04T05:48:45Z") == 0);
    assert(gh_parse_iso8601("2026-11-04 05:48:45Z") == 0);
    assert(gh_parse_iso8601("") == 0);
    assert(gh_parse_iso8601(NULL) == 0);
}

static void test_feed_order_and_cap(void) {
    gh_snapshot_t s = { 0 };
    int64_t newest = 0;
    for (int i = 0; i < GH_MAX_FEED + 5; i++) {
        int64_t at = (i * 37) % 23 + 1;
        if (at > newest) newest = at;
        gh_feed_item_t it = item(GH_EV_ISSUE_OPEN, "r", (uint32_t)i, at);
        gh_feed_insert(&s, &it);
    }
    assert(s.feed_count == GH_MAX_FEED);
    for (int i = 1; i < s.feed_count; i++) assert(s.feed[i - 1].at >= s.feed[i].at);
    assert(s.feed[0].at == newest);
}

static void test_diff(void) {
    gh_snapshot_t prev = { 0 }, cur = { 0 };
    gh_changes_t ch;

    snprintf(prev.repos[0].name, GH_NAME_LEN, "alpha");
    prev.repos[0].stars = 10;
    snprintf(prev.repos[1].name, GH_NAME_LEN, "beta");
    prev.repos[1].stars = 5;
    prev.repo_count = 2;
    gh_feed_item_t old = item(GH_EV_ISSUE_OPEN, "alpha", 1, 100);
    gh_feed_insert(&prev, &old);
    gh_snapshot_totals(&prev);

    cur = prev;
    // 首次刷新不报告变化。
    gh_diff(NULL, &cur, &ch);
    assert(ch.headline == -1 && ch.new_items == 0 && ch.star_delta == 0);
    prev.fetched_at = 0;
    gh_diff(&prev, &cur, &ch);
    assert(ch.headline == -1);

    prev.fetched_at = 150;
    cur.repos[1].stars = 8;
    cur.repos[1].prs = 1;
    gh_feed_item_t star = item(GH_EV_STAR, "beta", 0, 200);
    gh_feed_item_t pr = item(GH_EV_PR_OPEN, "beta", 7, 190);
    gh_feed_item_t stale = item(GH_EV_ISSUE_CLOSED, "alpha", 2, 90); // 比上次最新还旧
    gh_feed_insert(&cur, &star);
    gh_feed_insert(&cur, &pr);
    gh_feed_insert(&cur, &stale);
    gh_snapshot_totals(&cur);
    gh_diff(&prev, &cur, &ch);
    assert(ch.star_delta == 3);
    assert(ch.pr_delta == 1);
    assert(ch.issue_delta == 0);
    assert(ch.top_repo == 1);
    assert(ch.new_items == 2);
    assert(cur.feed[ch.headline].kind == GH_EV_PR_OPEN); // PR 优先于 star

    // 同一条目再次出现不算新动态。
    gh_snapshot_t again = cur;
    again.fetched_at = 300;
    gh_diff(&again, &cur, &ch);
    assert(ch.new_items == 0 && ch.headline == -1 && ch.star_delta == 0);

    // 取消 star 得到负增量,不选 top_repo。
    gh_snapshot_t fewer = cur;
    fewer.repos[0].stars = 9;
    gh_snapshot_totals(&fewer);
    gh_diff(&again, &fewer, &ch);
    assert(ch.star_delta == -1 && ch.top_repo == -1);
}

static void test_utf8_copy(void) {
    char buf[8];
    gh_copy_utf8(buf, sizeof(buf), "abc中文d");     // "abc"+3+3 > 7:在字符边界截断
    assert(strcmp(buf, "abc中") == 0);
    gh_copy_utf8(buf, sizeof(buf), "a\xff" "b\nc");  // 非法字节→'?',换行→空格
    assert(strcmp(buf, "a?b c") == 0);
    gh_copy_utf8(buf, sizeof(buf), NULL);
    assert(buf[0] == '\0');
    char one[1];
    gh_copy_utf8(one, sizeof(one), "x");
    assert(one[0] == '\0');
}

static void test_format(void) {
    char b[24];
    gh_fmt_ago(b, sizeof(b), 1000, 990, 0);
    assert(strcmp(b, "刚刚") == 0);
    gh_fmt_ago(b, sizeof(b), 10000, 10000 - 5 * 60, 0);
    assert(strcmp(b, "5分钟前") == 0);
    gh_fmt_ago(b, sizeof(b), 100000, 100000 - 3 * 3600, 0);
    assert(strcmp(b, "3小时前") == 0);
    gh_fmt_ago(b, sizeof(b), 1000000, 1000000 - 2 * 86400, 0);
    assert(strcmp(b, "2天前") == 0);
    // 2026-11-02 20:00Z 在 UTC+8 是 11/03。
    gh_fmt_ago(b, sizeof(b), gh_parse_iso8601("2026-12-01T00:00:00Z"),
               gh_parse_iso8601("2026-11-02T20:00:00Z"), 8 * 3600);
    assert(strcmp(b, "11/03") == 0);
    gh_fmt_ago(b, sizeof(b), 0, 5, 0);
    assert(strcmp(b, "--") == 0);

    gh_fmt_count(b, sizeof(b), 999);
    assert(strcmp(b, "999") == 0);
    gh_fmt_count(b, sizeof(b), 1234);
    assert(strcmp(b, "1.2k") == 0);
    gh_fmt_count(b, sizeof(b), 12345);
    assert(strcmp(b, "12k") == 0);
}

static void test_star_events(void) {
    gh_snapshot_t prev = { 0 }, cur = { 0 };
    snprintf(prev.repos[0].name, GH_NAME_LEN, "alpha");
    prev.repos[0].stars = 10;
    snprintf(prev.repos[1].name, GH_NAME_LEN, "beta");
    prev.repos[1].stars = 5;
    prev.repo_count = 2;
    gh_feed_item_t old = item(GH_EV_STAR, "beta", 2, 50);
    old.actor[0] = '\0';
    gh_feed_insert(&prev, &old);
    gh_snapshot_totals(&prev);

    // 首次刷新:不生成也不带入任何星标条目。
    cur = prev;
    cur.feed_count = 0;
    gh_carry_star_events(NULL, &cur, 100);
    assert(cur.feed_count == 0);

    prev.fetched_at = 90;
    cur.repos[0].stars = 13;      // +3
    cur.repos[1].stars = 4;       // -1:不生成条目
    gh_snapshot_totals(&cur);
    gh_carry_star_events(&prev, &cur, 100);
    assert(cur.feed_count == 2);
    assert(cur.feed[0].kind == GH_EV_STAR && strcmp(cur.feed[0].repo, "alpha") == 0);
    assert(cur.feed[0].number == 3 && cur.feed[0].at == 100 && cur.feed[0].actor[0] == '\0');
    assert(strcmp(cur.feed[1].repo, "beta") == 0 && cur.feed[1].at == 50);   // 旧条目被带入

    gh_changes_t ch;
    gh_diff(&prev, &cur, &ch);
    assert(ch.new_items == 1 && cur.feed[ch.headline].number == 3);
}

static void test_pick_active(void) {
    gh_snapshot_t s = { 0 };
    const int64_t pushed[] = { 30, 0, 90, 10, 60, 90 };
    for (int i = 0; i < 6; i++) {
        snprintf(s.repos[i].name, GH_NAME_LEN, "r%d", i);
        s.repos[i].pushed_at = pushed[i];
    }
    s.repo_count = 6;
    uint8_t idx[4];
    assert(gh_pick_active_repos(&s, idx, 4) == 4);
    assert(s.repos[idx[0]].pushed_at == 90 && s.repos[idx[1]].pushed_at == 90);
    assert(idx[2] == 4 && idx[3] == 0);
    s.repo_count = 2;
    assert(gh_pick_active_repos(&s, idx, 4) == 2 && idx[0] == 0 && idx[1] == 1);
    s.repo_count = 0;
    assert(gh_pick_active_repos(&s, idx, 4) == 0);
}

static void test_day_baseline(void) {
    uint32_t key = 0, base = 0;
    int32_t today;
    // 首次:建立基线,今日 0。
    assert(gh_day_baseline(20261104, 395, false, &key, &base, &today) && key == 20261104 && base == 395);
    assert(today == 0);
    assert(!gh_day_baseline(20261104, 398, false, &key, &base, &today) && today == 3);
    assert(!gh_day_baseline(20261104, 394, false, &key, &base, &today) && today == -1);   // 取消 star
    // 第二天重置。
    assert(gh_day_baseline(20261105, 400, false, &key, &base, &today) && base == 400 && today == 0);
    // partial 快照不建立基线、今日未知。
    key = 0; base = 0;
    assert(!gh_day_baseline(20261104, 0, true, &key, &base, &today) && today == -1 && key == 0);
    // 数据缺失时误记的 0 基线:拿到真实数据后重置,而不是把全部星标算成"今日"。
    key = 20261104; base = 0;
    assert(gh_day_baseline(20261104, 395, false, &key, &base, &today) && base == 395 && today == 0);
    // 真的没有星标的组织:0 基线保持,今日 0。
    key = 20261104; base = 0;
    assert(!gh_day_baseline(20261104, 0, false, &key, &base, &today) && today == 0);
}

int main(void) {
    test_day_baseline();
    test_star_events();
    test_pick_active();
    test_iso8601();
    test_feed_order_and_cap();
    test_diff();
    test_utf8_copy();
    test_format();
    puts("test_gh_model: PASS");
    return 0;
}
