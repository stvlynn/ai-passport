// main/gh_model.h —— GitHub 组织快照、动态流与变化检测(纯逻辑,不依赖 ESP-IDF/LVGL)。
//
// 一次刷新得到一个 gh_snapshot_t;与上一次快照比较得到 gh_changes_t,
// 由应用决定是否弹提示、播放音效、点亮屏幕。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GH_MAX_REPOS   24
#define GH_MAX_FEED    12
#define GH_NAME_LEN    32
#define GH_ACTOR_LEN   24
#define GH_TITLE_LEN   96   // UTF-8 字节,含结尾 NUL;只在字符边界截断

typedef struct {
    char name[GH_NAME_LEN];
    uint32_t stars;
    uint32_t issues;      // 打开中的 issue(不含 PR)
    uint32_t prs;         // 打开中的 PR
    bool is_private;
    int64_t pushed_at;    // UTC 秒;0 = 未知
} gh_repo_t;

typedef enum {
    GH_EV_STAR = 0,
    GH_EV_ISSUE_OPEN,
    GH_EV_ISSUE_CLOSED,
    GH_EV_PR_OPEN,
    GH_EV_PR_MERGED,
    GH_EV_PR_CLOSED,
} gh_ev_kind_t;

typedef struct {
    gh_ev_kind_t kind;
    char repo[GH_NAME_LEN];
    char actor[GH_ACTOR_LEN];
    char title[GH_TITLE_LEN];   // STAR 事件为空
    uint32_t number;            // STAR 事件为 0
    int64_t at;                 // UTC 秒
} gh_feed_item_t;

typedef struct {
    gh_repo_t repos[GH_MAX_REPOS];
    uint8_t repo_count;         // 实际存入的仓库数(按星数降序)
    uint16_t repo_total;        // 组织仓库总数(可能多于 repo_count)
    gh_feed_item_t feed[GH_MAX_FEED];
    uint8_t feed_count;         // 按时间降序
    uint32_t total_stars;
    uint32_t total_issues;
    uint32_t total_prs;
    int32_t rate_remaining;     // -1 = 未知
    bool partial;               // 响应带 errors:部分字段因 token 权限不足而为 null
    int64_t fetched_at;         // UTC 秒;0 = 从未成功
} gh_snapshot_t;

typedef struct {
    int32_t star_delta;         // 总星数变化,可为负(取消 star)
    int32_t issue_delta;        // 打开中的 issue 数变化
    int32_t pr_delta;           // 打开中的 PR 数变化
    uint8_t new_items;          // 上次快照里没有、且比上次最新条目更新的动态数
    int8_t headline;            // 最值得提示的新动态在 cur->feed 中的下标;-1 = 无
    int8_t top_repo;            // 星数涨得最多的仓库在 cur->repos 中的下标;-1 = 无
} gh_changes_t;

// 汇总 repos[] 得到 total_*。
void gh_snapshot_totals(gh_snapshot_t *s);

// 把 STAR / issue / PR 候选条目合并进 s->feed:按时间降序,最多 GH_MAX_FEED 条。
// 调用前 s->feed_count 可以为 0;本函数会就地插入。
void gh_feed_insert(gh_snapshot_t *s, const gh_feed_item_t *item);

// fine-grained token 读不到"谁点了星标",星标动态改由快照差值生成:
// 把 prev 中已有的 STAR 条目带到 cur,并为每个星数上涨的仓库插入一条
// { kind=STAR, repo, actor="", number=增量, at=now }。须在 gh_diff 之前调用。
void gh_carry_star_events(const gh_snapshot_t *prev, gh_snapshot_t *cur, int64_t now);

// 选出最近推送的最多 max 个仓库(写入下标,按 pushed_at 降序),用于拉取 issue/PR 动态。
int gh_pick_active_repos(const gh_snapshot_t *s, uint8_t *idx, int max);

// "今日新增星标"的基线。day_key 形如 20261104;*base_key/*base_stars 为已保存的基线。
// 返回 true 表示基线需要更新(调用方随后持久化)。规则:
//   - partial 快照(token 权限不足、星数为 null)不建立也不更新基线;
//   - 换了一天时以当前总星数为新基线;
//   - 已存基线为 0 而当前总数 > 0:视为在数据缺失时误记的基线,重置。
// *today 写入今日增量;无法确定时为 -1。
bool gh_day_baseline(uint32_t day_key, uint32_t total, bool partial,
                     uint32_t *base_key, uint32_t *base_stars, int32_t *today);

// prev 可为 NULL 或 fetched_at==0(首次刷新):此时不报告任何变化。
void gh_diff(const gh_snapshot_t *prev, const gh_snapshot_t *cur, gh_changes_t *out);

// 动态的提示优先级:合并 PR > 新 PR > 新 issue > star > 关闭类。数字越大越重要。
int gh_event_priority(gh_ev_kind_t kind);

// "2026-11-04T05:48:45Z" → UTC 秒。格式不符返回 0。
int64_t gh_parse_iso8601(const char *s);

// UTF-8 安全复制:最多写 cap-1 字节,不切断多字节字符,非法序列替换为 '?'。
void gh_copy_utf8(char *dst, size_t cap, const char *src);

// 相对时间(中文),如 "刚刚" "5分钟前" "3小时前" "2天前" "11/02"。
// now/t 为 UTC 秒;tz_offset_s 用于超过 7 天时显示本地日期。
void gh_fmt_ago(char *buf, size_t cap, int64_t now, int64_t t, int32_t tz_offset_s);

// 千位紧凑格式:999 → "999";1234 → "1.2k";12345 → "12k"。
void gh_fmt_count(char *buf, size_t cap, uint32_t n);
