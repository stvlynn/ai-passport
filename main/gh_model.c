// main/gh_model.c —— 见 gh_model.h。纯 C,host 测试直接编译本文件。
#include "gh_model.h"

#include <stdio.h>
#include <string.h>

void gh_snapshot_totals(gh_snapshot_t *s) {
    s->total_stars = s->total_issues = s->total_prs = 0;
    for (uint8_t i = 0; i < s->repo_count; i++) {
        s->total_stars += s->repos[i].stars;
        s->total_issues += s->repos[i].issues;
        s->total_prs += s->repos[i].prs;
    }
}

void gh_feed_insert(gh_snapshot_t *s, const gh_feed_item_t *item) {
    uint8_t pos = s->feed_count;
    while (pos > 0 && s->feed[pos - 1].at < item->at) pos--;
    if (pos >= GH_MAX_FEED) return;
    uint8_t last = s->feed_count < GH_MAX_FEED ? s->feed_count : GH_MAX_FEED - 1;
    memmove(&s->feed[pos + 1], &s->feed[pos], (size_t)(last - pos) * sizeof(s->feed[0]));
    s->feed[pos] = *item;
    if (s->feed_count < GH_MAX_FEED) s->feed_count++;
}

int gh_event_priority(gh_ev_kind_t kind) {
    switch (kind) {
    case GH_EV_PR_MERGED:    return 5;
    case GH_EV_PR_OPEN:      return 4;
    case GH_EV_ISSUE_OPEN:   return 3;
    case GH_EV_STAR:         return 2;
    case GH_EV_ISSUE_CLOSED: return 1;
    case GH_EV_PR_CLOSED:    return 1;
    }
    return 0;
}

void gh_carry_star_events(const gh_snapshot_t *prev, gh_snapshot_t *cur, int64_t now) {
    if (!prev || prev->fetched_at == 0) return;
    for (uint8_t i = 0; i < prev->feed_count; i++) {
        if (prev->feed[i].kind == GH_EV_STAR) gh_feed_insert(cur, &prev->feed[i]);
    }
    for (uint8_t i = 0; i < cur->repo_count; i++) {
        for (uint8_t j = 0; j < prev->repo_count; j++) {
            if (strcmp(cur->repos[i].name, prev->repos[j].name) != 0) continue;
            if (cur->repos[i].stars > prev->repos[j].stars) {
                gh_feed_item_t it = { .kind = GH_EV_STAR, .at = now,
                                      .number = cur->repos[i].stars - prev->repos[j].stars };
                memcpy(it.repo, cur->repos[i].name, sizeof(it.repo));
                gh_feed_insert(cur, &it);
            }
            break;
        }
    }
}

int gh_pick_active_repos(const gh_snapshot_t *s, uint8_t *idx, int max) {
    int n = 0;
    for (uint8_t i = 0; i < s->repo_count; i++) {
        // 插入排序:按 pushed_at 降序保留前 max 个。
        int pos = n;
        while (pos > 0 && s->repos[idx[pos - 1]].pushed_at < s->repos[i].pushed_at) pos--;
        if (pos >= max) continue;
        int last = n < max ? n : max - 1;
        for (int k = last; k > pos; k--) idx[k] = idx[k - 1];
        idx[pos] = i;
        if (n < max) n++;
    }
    return n;
}

static bool same_item(const gh_feed_item_t *a, const gh_feed_item_t *b) {
    if (a->kind != b->kind || a->number != b->number) return false;
    if (strcmp(a->repo, b->repo) != 0) return false;
    return a->kind != GH_EV_STAR || strcmp(a->actor, b->actor) == 0;
}

void gh_diff(const gh_snapshot_t *prev, const gh_snapshot_t *cur, gh_changes_t *out) {
    memset(out, 0, sizeof(*out));
    out->headline = -1;
    out->top_repo = -1;
    if (!prev || prev->fetched_at == 0) return;

    out->star_delta = (int32_t)cur->total_stars - (int32_t)prev->total_stars;
    out->issue_delta = (int32_t)cur->total_issues - (int32_t)prev->total_issues;
    out->pr_delta = (int32_t)cur->total_prs - (int32_t)prev->total_prs;

    int32_t best_gain = 0;
    for (uint8_t i = 0; i < cur->repo_count; i++) {
        for (uint8_t j = 0; j < prev->repo_count; j++) {
            if (strcmp(cur->repos[i].name, prev->repos[j].name) != 0) continue;
            int32_t gain = (int32_t)cur->repos[i].stars - (int32_t)prev->repos[j].stars;
            if (gain > best_gain) {
                best_gain = gain;
                out->top_repo = (int8_t)i;
            }
            break;
        }
    }

    // 只把"比上次最新一条更新"的条目算作新动态:避免旧条目因排序变化被重复提示。
    int64_t newest_prev = prev->feed_count ? prev->feed[0].at : prev->fetched_at;
    int best_prio = 0;
    for (uint8_t i = 0; i < cur->feed_count; i++) {
        const gh_feed_item_t *it = &cur->feed[i];
        if (it->at <= newest_prev) continue;
        bool seen = false;
        for (uint8_t j = 0; j < prev->feed_count && !seen; j++) {
            seen = same_item(it, &prev->feed[j]);
        }
        if (seen) continue;
        out->new_items++;
        int prio = gh_event_priority(it->kind);
        if (prio > best_prio) {   // feed 已按时间降序:同优先级保留最新
            best_prio = prio;
            out->headline = (int8_t)i;
        }
    }
}

static int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static bool read_num(const char *s, int len, int *out) {
    int v = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return true;
}

int64_t gh_parse_iso8601(const char *s) {
    if (!s || strlen(s) < 20) return 0;
    int y, mo, d, h, mi, se;
    if (!read_num(s, 4, &y) || s[4] != '-' || !read_num(s + 5, 2, &mo) || s[7] != '-' ||
        !read_num(s + 8, 2, &d) || s[10] != 'T' || !read_num(s + 11, 2, &h) || s[13] != ':' ||
        !read_num(s + 14, 2, &mi) || s[16] != ':' || !read_num(s + 17, 2, &se) || s[19] != 'Z') {
        return 0;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60) return 0;
    return days_from_civil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + se;
}

// 返回该位置合法 UTF-8 字符的字节数;非法返回 0。
static size_t utf8_len(const unsigned char *p) {
    if (p[0] < 0x80) return 1;
    size_t n = (p[0] & 0xE0) == 0xC0 ? 2 : (p[0] & 0xF0) == 0xE0 ? 3 : (p[0] & 0xF8) == 0xF0 ? 4 : 0;
    if (n == 0 || (n == 2 && p[0] < 0xC2)) return 0;
    for (size_t i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) return 0;
    }
    return n;
}

void gh_copy_utf8(char *dst, size_t cap, const char *src) {
    if (cap == 0) return;
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)(src ? src : "");
    while (*p) {
        size_t n = utf8_len(p);
        if (n == 0) {
            if (o + 1 >= cap) break;
            dst[o++] = '?';
            p++;
            continue;
        }
        if (o + n >= cap) break;
        unsigned char c = p[0];
        if (n == 1 && (c == '\n' || c == '\r' || c == '\t')) c = ' ';
        dst[o++] = (char)c;
        for (size_t i = 1; i < n; i++) dst[o++] = (char)p[i];
        p += n;
    }
    dst[o] = '\0';
}

void gh_fmt_ago(char *buf, size_t cap, int64_t now, int64_t t, int32_t tz_offset_s) {
    int64_t dt = now - t;
    if (t <= 0 || now <= 0) {
        snprintf(buf, cap, "--");
    } else if (dt < 60) {
        snprintf(buf, cap, "刚刚");
    } else if (dt < 3600) {
        snprintf(buf, cap, "%d分钟前", (int)(dt / 60));
    } else if (dt < 86400) {
        snprintf(buf, cap, "%d小时前", (int)(dt / 3600));
    } else if (dt < 7 * 86400) {
        snprintf(buf, cap, "%d天前", (int)(dt / 86400));
    } else {
        int64_t days = (t + tz_offset_s) / 86400;
        // civil_from_days
        int64_t z = days + 719468;
        int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        unsigned doe = (unsigned)(z - era * 146097);
        unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        unsigned mp = (5 * doy + 2) / 153;
        unsigned d = doy - (153 * mp + 2) / 5 + 1;
        unsigned m = mp < 10 ? mp + 3 : mp - 9;
        snprintf(buf, cap, "%02u/%02u", m, d);
    }
}

void gh_fmt_count(char *buf, size_t cap, uint32_t n) {
    if (n < 1000) {
        snprintf(buf, cap, "%u", (unsigned)n);
    } else if (n < 10000) {
        snprintf(buf, cap, "%u.%uk", (unsigned)(n / 1000), (unsigned)(n % 1000 / 100));
    } else {
        snprintf(buf, cap, "%uk", (unsigned)(n / 1000));
    }
}

bool gh_day_baseline(uint32_t day_key, uint32_t total, bool partial,
                     uint32_t *base_key, uint32_t *base_stars, int32_t *today) {
    if (partial) {
        *today = -1;
        return false;
    }
    bool changed = false;
    if (*base_key != day_key || (*base_stars == 0 && total > 0)) {
        *base_key = day_key;
        *base_stars = total;
        changed = true;
    }
    *today = (int32_t)total - (int32_t)*base_stars;
    return changed;
}
