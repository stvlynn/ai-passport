// main/ui.c —— 见 ui.h。
//
// 视觉规则(AstraFoundry Sunbeam 的像素化落地):
//   - 暖纸底 ink-50,暖墨字 ink-900;卡片白底 + 1px 墨线 + 2px 硬投影(像素风,不用模糊阴影)。
//   - 黄色 yellow-500 只用于"结果":今日新增星标。状态色只用于图标/圆点,不用于正文。
//   - 字体:Fusion Pixel 12px(1bpp),24/36px 为其 2x/3x 整数倍栅格,保持像素锐利。
//   - 动效只解释变化:提示卡滑入一次后停住;看板娘反应几秒后回到静止。
#include "ui.h"

#include "ui_assets.h"

#include "lynn_anim.h"

#include "app_portal.h"
#include "portal_proto.h"

#include "lvgl.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

LV_FONT_DECLARE(ui_font_12);
LV_FONT_DECLARE(ui_font_24);
LV_FONT_DECLARE(ui_font_36);

// Sunbeam tokens
#define C_PAPER    0xFAF8F5   // background (ink-50)
#define C_CARD     0xFFFFFF
#define C_WELL     0xEFECE6   // muted / secondary (ink-100)
#define C_HAIR     0xDFDAD3   // border (ink-200)
#define C_FAINT    0xC0B9AF   // ink-300
#define C_MUTED    0x6C6660   // muted-foreground (ink-500)
#define C_INK7     0x38322E   // ink-700
#define C_INK      0x14110F   // foreground (ink-900)
#define C_YELLOW   0xFFCC00   // primary
#define C_DANGER   0xE03F1F
#define C_SUCCESS  0x1F9E5E

#define W 240
#define H 320
#define X0 16                 // 内容左边距(避开 30px 圆角)
#define XW (W - 2 * X0)

#define FEED_ROWS   6
#define FEED_ROW_H  38
#define REPO_ROWS   7
#define REPO_ROW_H  33

typedef struct {
    lv_obj_t *box, *icon, *head, *right, *sub, *bar;
} row_t;

static struct {
    lv_obj_t *scr;
    // 状态栏
    lv_obj_t *clock, *wifi[3], *wifi_dot, *batt_fill, *batt_label, *batt_frame;
    lv_obj_t *tabs[APP_PAGE_COUNT];
    // 总览
    lv_obj_t *stars, *chip, *chip_label, *stat[3];
    lv_obj_t *focus_lock, *focus_name, *focus_stars, *focus_meta, *focus_idx;
    lv_obj_t *lynn, *bubble, *sync_label;
    // 列表页
    row_t rows[FEED_ROWS > REPO_ROWS ? FEED_ROWS : REPO_ROWS];
    lv_obj_t *list_count, *empty;
    // 设备页
    lv_obj_t *info[4];
    row_t menu[UI_MENU_COUNT];
    // 配网页
    lv_obj_t *prov_status, *prov_icon, *prov_qr, *prov_info[3];
    char prov_qr_data[96];
    // 提示卡
    lv_obj_t *toast, *toast_icon, *toast_head, *toast_sub;
    lv_timer_t *toast_timer;
    lv_timer_t *anim_timer;
    // 看板娘
    lynn_act_t mood;            // 由界面状态决定的心情(喂给 lynn_anim)
    bool syncing;
    bool built_provisioning;
    app_page_t built_page;
    uint8_t idle;
} s;

// ---------------------------------------------------------------------------
// 基础构件
// ---------------------------------------------------------------------------

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, int32_t bg) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    if (bg >= 0) {
        lv_obj_set_style_bg_color(o, lv_color_hex((uint32_t)bg), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    }
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void outline(lv_obj_t *o, uint32_t color) {
    lv_obj_set_style_border_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
}

// 像素卡片:2px 硬投影 + 白底 + 1px 墨线。返回卡片本体。
static lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h) {
    box(parent, x + 2, y + 2, w, h, C_HAIR);
    lv_obj_t *c = box(parent, x, y, w, h, C_CARD);
    outline(c, C_INK7);
    return c;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                       int x, int y, int w, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    if (w > 0) {
        // 单行省略:DOTS 需要固定高度,否则标签会自动长高换行。
        lv_obj_set_size(l, w, lv_font_get_line_height(font));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    }
    lv_label_set_text(l, text ? text : "");
    return l;
}

// 多行文本:自动换行,行距 3px(像素字体 12px 行高本身没有留白)。
static void wrap(lv_obj_t *l) {
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(l, LV_SIZE_CONTENT);
    lv_obj_set_style_text_line_space(l, 3, 0);
}

static lv_obj_t *label_right(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                             int x, int y, int w, const char *text) {
    lv_obj_t *l = label(parent, font, color, x, y, w, text);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    return l;
}

static lv_obj_t *icon(lv_obj_t *parent, const lv_image_dsc_t *src, int x, int y) {
    lv_obj_t *i = lv_image_create(parent);
    lv_image_set_src(i, src);
    lv_obj_set_pos(i, x, y);
    return i;
}

static const lv_image_dsc_t *event_icon(gh_ev_kind_t kind) {
    switch (kind) {
    case GH_EV_STAR:         return &ui_icon_star;
    case GH_EV_ISSUE_OPEN:   return &ui_icon_issue;
    case GH_EV_ISSUE_CLOSED: return &ui_icon_issue_done;
    case GH_EV_PR_OPEN:      return &ui_icon_pr;
    case GH_EV_PR_MERGED:    return &ui_icon_merged;
    case GH_EV_PR_CLOSED:    return &ui_icon_pr_closed;
    }
    return &ui_icon_star_ink;
}

static const char *event_verb(gh_ev_kind_t kind) {
    switch (kind) {
    case GH_EV_STAR:         return "新星标";
    case GH_EV_ISSUE_OPEN:   return "Issue";
    case GH_EV_ISSUE_CLOSED: return "Issue 已关闭";
    case GH_EV_PR_OPEN:      return "PR";
    case GH_EV_PR_MERGED:    return "PR 已合并";
    case GH_EV_PR_CLOSED:    return "PR 已关闭";
    }
    return "";
}

// 逐帧动画的帧 → 图片。动画状态放在 s 之外:换页重建控件时日程与反应不中断。
static const lv_image_dsc_t *const FRAMES[LF_COUNT] = {
    &ui_img_lynn_idle, &ui_img_lynn_idle_blink, &ui_img_lynn_idle_2,
    &ui_img_lynn_idle_3, &ui_img_lynn_happy, &ui_img_lynn_happy_2,
    &ui_img_lynn_happy_hop, &ui_img_lynn_surprised, &ui_img_lynn_surprised_2,
    &ui_img_lynn_review, &ui_img_lynn_review_2, &ui_img_lynn_review_3,
    &ui_img_lynn_sleep, &ui_img_lynn_sleep_2, &ui_img_lynn_sleep_3,
    &ui_img_lynn_confused, &ui_img_lynn_confused_2, &ui_img_lynn_confused_3,
    &ui_img_lynn_read_1, &ui_img_lynn_read_2, &ui_img_lynn_read_3,
    &ui_img_lynn_read_4, &ui_img_lynn_type_1, &ui_img_lynn_type_2,
    &ui_img_lynn_type_3, &ui_img_lynn_type_4, &ui_img_lynn_doze_1,
    &ui_img_lynn_doze_2, &ui_img_lynn_doze_3, &ui_img_lynn_doze_4,
    &ui_img_lynn_wave_1, &ui_img_lynn_wave_2, &ui_img_lynn_wave_3,
    &ui_img_lynn_tea_1, &ui_img_lynn_tea_2, &ui_img_lynn_tea_3,
    &ui_img_lynn_stretch_1, &ui_img_lynn_stretch_2, &ui_img_lynn_stretch_3,
    &ui_img_lynn_music_1, &ui_img_lynn_music_2, &ui_img_lynn_music_3,
    &ui_img_lynn_party_1, &ui_img_lynn_party_2, &ui_img_lynn_party_3,
    &ui_img_lynn_inspect_1, &ui_img_lynn_inspect_2,
};
_Static_assert(sizeof(FRAMES) / sizeof(FRAMES[0]) == LF_COUNT, "每个动画帧都要有图片");
static lynn_anim_t s_anim;

static lynn_act_t pose_mood(ui_pose_t pose) {
    switch (pose) {
    case UI_POSE_HAPPY:     return LA_HAPPY;
    case UI_POSE_SURPRISED: return LA_SURPRISED;
    case UI_POSE_REVIEW:    return LA_REVIEW;
    case UI_POSE_SLEEP:     return LA_SLEEP;
    case UI_POSE_CONFUSED:  return LA_CONFUSED;
    case UI_POSE_IDLE:      break;
    }
    return LA_IDLE;
}

static int32_t tz_offset(int64_t now) {
    time_t t = (time_t)now;
    struct tm lt, gt;
    localtime_r(&t, &lt);
    gmtime_r(&t, &gt);
    int32_t d = (lt.tm_hour - gt.tm_hour) * 3600 + (lt.tm_min - gt.tm_min) * 60;
    if (lt.tm_yday != gt.tm_yday) d += (lt.tm_yday > gt.tm_yday || lt.tm_year > gt.tm_year) ? 86400 : -86400;
    return d;
}

// ---------------------------------------------------------------------------
// 公共框架:状态栏与页签
// ---------------------------------------------------------------------------

static void build_chrome(lv_obj_t *scr, const char *title, bool tabs) {
    // 左:组织标记(实心小方块 = 品牌"mark",在左上角内边距处)+ 页名
    box(scr, X0 + 4, 13, 7, 7, C_INK);
    label(scr, &ui_font_12, C_INK, X0 + 15, 10, 92, title);
    s.clock = label(scr, &ui_font_12, C_MUTED, 104, 10, 36, "--:--");
    lv_obj_set_style_text_align(s.clock, LV_TEXT_ALIGN_CENTER, 0);

    // 右:Wi-Fi 三格 + 电量
    for (int i = 0; i < 3; i++) {
        int h = 3 + i * 2;
        s.wifi[i] = box(scr, 150 + i * 3, 20 - h, 2, h, C_FAINT);
    }
    s.wifi_dot = box(scr, 159, 13, 2, 2, C_DANGER);
    s.batt_label = label_right(scr, &ui_font_12, C_INK, 162, 10, 32, "");
    s.batt_frame = box(scr, 198, 13, 15, 8, -1);
    outline(s.batt_frame, C_INK);
    box(scr, 213, 15, 2, 4, C_INK);
    s.batt_fill = box(scr, 200, 15, 0, 4, C_INK);

    if (!tabs) return;
    int x = W / 2 - (APP_PAGE_COUNT * 10 - 4) / 2;
    for (int i = 0; i < APP_PAGE_COUNT; i++) {
        s.tabs[i] = box(scr, x + i * 10, 304, 6, 6, -1);
        outline(s.tabs[i], C_INK);
    }
}

static void refresh_chrome(const ui_model_t *m) {
    if (m->now > 0) {
        time_t t = (time_t)m->now;
        struct tm lt;
        localtime_r(&t, &lt);
        lv_label_set_text_fmt(s.clock, "%02d:%02d", lt.tm_hour, lt.tm_min);
    } else {
        lv_label_set_text(s.clock, "--:--");
    }

    int level = 0;
    if (m->net.link == NET_LINK_ONLINE) {
        level = m->net.rssi == 0 ? 3 : m->net.rssi > -60 ? 3 : m->net.rssi > -72 ? 2 : 1;
    }
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_bg_color(s.wifi[i], lv_color_hex(i < level ? C_INK : C_FAINT), 0);
    }
    if (m->net.link == NET_LINK_ONLINE || m->net.link == NET_LINK_CONNECTING) {
        lv_obj_add_flag(s.wifi_dot, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s.wifi_dot, LV_OBJ_FLAG_HIDDEN);
    }

    if (m->battery_soc < 0) {
        // 电量计不可用:只画空电池框,不显示数字。
        lv_label_set_text(s.batt_label, "");
        lv_obj_set_width(s.batt_fill, 0);
    } else {
        lv_label_set_text_fmt(s.batt_label, "%d%%", m->battery_soc);
        lv_obj_set_width(s.batt_fill, (m->battery_soc * 11 + 50) / 100);
        lv_obj_set_style_bg_color(s.batt_fill, lv_color_hex(m->battery_soc <= 15 ? C_DANGER : C_INK), 0);
    }

    if (s.tabs[0] && !m->provisioning) {
        for (int i = 0; i < APP_PAGE_COUNT; i++) {
            bool on = i == (int)m->nav->page;
            lv_obj_set_style_bg_color(s.tabs[i], lv_color_hex(C_INK), 0);
            lv_obj_set_style_bg_opa(s.tabs[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// 看板娘
// ---------------------------------------------------------------------------

static ui_pose_t default_pose(const ui_model_t *m, char *msg, size_t cap) {
    const gh_snapshot_t *sn = m->snap;
    if (m->idle == APP_IDLE_OFF) {
        snprintf(msg, cap, "Zzz…\n拍两下手或按任意键叫醒我");
        return UI_POSE_SLEEP;
    }
    if (m->net.link == NET_LINK_NO_CREDENTIALS) {
        snprintf(msg, cap, "还没有 Wi-Fi\n去「设备」页网页配网吧");
        return UI_POSE_CONFUSED;
    }
    if (!m->token_set) {
        snprintf(msg, cap, "缺少 GitHub token\n用 USB 串口输入\ntoken <值>");
        return UI_POSE_CONFUSED;
    }
    if (m->net.link != NET_LINK_ONLINE) {
        snprintf(msg, cap, "网络断开了\n正在重连 %s", m->net.ssid);
        return UI_POSE_CONFUSED;
    }
    if (sn->fetched_at == 0) {
        if (m->syncing || m->last_result == GH_FETCH_OK) {
            snprintf(msg, cap, "正在翻看\n%s 的仓库…", m->org);
            return UI_POSE_REVIEW;
        }
        snprintf(msg, cap, "同步失败\n%s", m->last_detail[0] ? m->last_detail : "稍后重试");
        return UI_POSE_CONFUSED;
    }
    if (sn->partial) {
        snprintf(msg, cap, "token 权限不足\n星标等数据读不到\n请检查 token 授权");
        return UI_POSE_CONFUSED;
    }
    if (m->last_result == GH_FETCH_AUTH) {
        snprintf(msg, cap, "token 被拒绝了\n请检查权限");
        return UI_POSE_CONFUSED;
    }
    if (m->today_stars > 0) {
        snprintf(msg, cap, "今天收获了\n%d 颗新星标", (int)m->today_stars);
        return UI_POSE_HAPPY;
    }
    if (sn->total_prs > 0) {
        snprintf(msg, cap, "有 %u 个 PR\n等待评审", (unsigned)sn->total_prs);
        return UI_POSE_REVIEW;
    }
    if (sn->total_issues > 0) {
        snprintf(msg, cap, "还有 %u 个 issue\n开着", (unsigned)sn->total_issues);
        return UI_POSE_IDLE;
    }
    snprintf(msg, cap, "一切安好\n长按上下键\n可以戳戳我");
    return UI_POSE_IDLE;
}

static void apply_pose(void) {
    if (!s.lynn) return;
    lynn_input_t in = { .mood = s.mood, .syncing = s.syncing, .dim = s.idle == APP_IDLE_DIM };
    if (s.idle == APP_IDLE_OFF) in.mood = LA_SLEEP;
    const lv_image_dsc_t *img = FRAMES[lynn_anim_frame(&s_anim, &in, lv_tick_get())];
    if (lv_image_get_src(s.lynn) != img) lv_image_set_src(s.lynn, img);
}

static void anim_tick(lv_timer_t *t) {
    (void)t;
    apply_pose();
}

bool ui_poke(const ui_model_t *m) {
    (void)m;
    if (!s.lynn) return false;
    lynn_input_t in = { .mood = s.mood, .syncing = s.syncing, .dim = s.idle == APP_IDLE_DIM };
    lynn_anim_poke(&s_anim, &in, lv_tick_get());
    apply_pose();
    return true;
}

// ---------------------------------------------------------------------------
// 总览页
// ---------------------------------------------------------------------------

static void build_home(lv_obj_t *scr) {
    label(scr, &ui_font_12, C_MUTED, X0, 32, 100, "总星标 STARS");
    s.stars = label(scr, &ui_font_36, C_INK, X0, 46, 130, "--");
    s.chip = box(scr, 150, 56, 74, 22, C_WELL);
    s.chip_label = label(s.chip, &ui_font_12, C_INK, 0, 5, 74, "");
    lv_obj_set_style_text_align(s.chip_label, LV_TEXT_ALIGN_CENTER, 0);

    static const char *names[3] = { "Issues", "PRs", "仓库" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = card(scr, X0 + i * 70, 92, 64, 46);
        s.stat[i] = label(c, &ui_font_24, C_INK, 6, 3, 54, "-");
        label(c, &ui_font_12, C_MUTED, 6, 28, 56, names[i]);
    }

    lv_obj_t *f = card(scr, X0, 148, XW - 2, 40);
    s.focus_lock = icon(f, &ui_icon_lock, 6, 6);
    s.focus_name = label(f, &ui_font_12, C_INK, 16, 4, 112, "");
    icon(f, &ui_icon_star, 150, 5);
    s.focus_stars = label(f, &ui_font_12, C_INK, 162, 4, 40, "");
    s.focus_meta = label(f, &ui_font_12, C_MUTED, 6, 21, 150, "");
    s.focus_idx = label_right(f, &ui_font_12, C_FAINT, 150, 21, 52, "");

    s.lynn = lv_image_create(scr);
    lv_obj_set_pos(s.lynn, -22, 186);
    lv_image_set_src(s.lynn, &ui_img_lynn_idle);

    // 对话气泡:白卡 + 指向看板娘的像素小尾巴
    lv_obj_t *b = card(scr, 100, 206, 124, 62);
    for (int i = 0; i < 4; i++) box(scr, 96 + i, 226 + i, 4 - i, 1, C_INK7);
    s.bubble = label(b, &ui_font_12, C_INK, 6, 4, 112, "");
    wrap(s.bubble);
    s.sync_label = label(scr, &ui_font_12, C_MUTED, 100, 276, 124, "");
}

static void refresh_home(const ui_model_t *m) {
    const gh_snapshot_t *sn = m->snap;
    char buf[48];
    bool have = sn->fetched_at != 0;

    if (have) {
        lv_label_set_text_fmt(s.stars, "%u", (unsigned)sn->total_stars);
        lv_label_set_text_fmt(s.stat[0], "%u", (unsigned)sn->total_issues);
        lv_label_set_text_fmt(s.stat[1], "%u", (unsigned)sn->total_prs);
        lv_label_set_text_fmt(s.stat[2], "%u", (unsigned)sn->repo_total);
    }
    if (m->today_stars > 0) {
        lv_obj_set_style_bg_color(s.chip, lv_color_hex(C_YELLOW), 0);   // 唯一的黄色:今日成果
        lv_label_set_text_fmt(s.chip_label, "今日 +%d", (int)m->today_stars);
    } else {
        lv_obj_set_style_bg_color(s.chip, lv_color_hex(C_WELL), 0);
        lv_label_set_text(s.chip_label, m->today_stars == 0 ? "今日 +0" : "今日 --");
    }

    uint8_t n = sn->repo_count;
    if (n) {
        const gh_repo_t *r = &sn->repos[m->nav->cursor[APP_PAGE_HOME] % n];
        if (r->is_private) {
            lv_obj_remove_flag(s.focus_lock, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_x(s.focus_name, 16);
        } else {
            lv_obj_add_flag(s.focus_lock, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_x(s.focus_name, 6);
        }
        lv_label_set_text(s.focus_name, r->name);
        gh_fmt_count(buf, sizeof(buf), r->stars);
        lv_label_set_text(s.focus_stars, buf);
        char ago[16];
        gh_fmt_ago(ago, sizeof(ago), m->now, r->pushed_at, tz_offset(m->now));
        lv_label_set_text_fmt(s.focus_meta, "I %u · PR %u · 推送 %s",
                              (unsigned)r->issues, (unsigned)r->prs, ago);
        lv_label_set_text_fmt(s.focus_idx, "%u/%u", (unsigned)(m->nav->cursor[APP_PAGE_HOME] % n + 1),
                              (unsigned)n);
    } else {
        lv_obj_add_flag(s.focus_lock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(s.focus_name, 6);
        lv_label_set_text(s.focus_name, "等待第一次同步");
        lv_label_set_text(s.focus_stars, "-");
        lv_label_set_text(s.focus_meta, "按 OK 切换焦点仓库");
        lv_label_set_text(s.focus_idx, "");
    }

    char msg[96];
    s.mood = pose_mood(default_pose(m, msg, sizeof(msg)));
    s.syncing = m->syncing;
    lv_label_set_text(s.bubble, msg);
    apply_pose();

    if (m->syncing) {
        lv_label_set_text(s.sync_label, "同步中…");
    } else if (have) {
        char ago[16];
        gh_fmt_ago(ago, sizeof(ago), m->now, sn->fetched_at, tz_offset(m->now));
        lv_label_set_text_fmt(s.sync_label, "%s同步", ago);
    } else {
        lv_label_set_text(s.sync_label, "长按 OK 同步");
    }
}

// ---------------------------------------------------------------------------
// 动态页 / 仓库页
// ---------------------------------------------------------------------------

static void build_list_header(lv_obj_t *scr, const char *title) {
    label(scr, &ui_font_24, C_INK, X0, 30, 120, title);
    s.list_count = label_right(scr, &ui_font_12, C_MUTED, 120, 40, 104, "");
    box(scr, X0, 60, XW, 1, C_INK7);
    s.empty = label(scr, &ui_font_12, C_MUTED, X0, 80, XW, "");
}

static void build_feed(lv_obj_t *scr) {
    build_list_header(scr, "动态");
    for (int i = 0; i < FEED_ROWS; i++) {
        row_t *r = &s.rows[i];
        int y = 66 + i * FEED_ROW_H;
        r->box = box(scr, X0 - 2, y, XW + 2, FEED_ROW_H - 3, C_CARD);
        r->icon = icon(r->box, &ui_icon_star, 5, 6);
        r->head = label(r->box, &ui_font_12, C_INK, 18, 3, 140, "");
        r->right = label_right(r->box, &ui_font_12, C_MUTED, 150, 3, 56, "");
        r->sub = label(r->box, &ui_font_12, C_MUTED, 18, 19, 188, "");
        r->bar = box(scr, X0, y + FEED_ROW_H - 2, XW, 1, C_HAIR);
    }
}

static void row_select(row_t *r, bool selected) {
    lv_obj_set_style_bg_opa(r->box, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r->box, selected ? 1 : 0, 0);
    lv_obj_set_style_border_color(r->box, lv_color_hex(C_INK7), 0);
    lv_obj_set_style_shadow_width(r->box, 0, 0);
}

static int first_visible(int cursor, int count, int rows) {
    int first = cursor - rows / 2;
    if (first > count - rows) first = count - rows;
    return first < 0 ? 0 : first;
}

static void refresh_feed(const ui_model_t *m) {
    const gh_snapshot_t *sn = m->snap;
    int count = sn->feed_count;
    lv_label_set_text_fmt(s.list_count, "最近 %d 条", count);
    lv_label_set_text(s.empty, count ? "" : sn->fetched_at ? "暂时没有动态" : "等待第一次同步…");
    int cursor = m->nav->cursor[APP_PAGE_FEED];
    int first = first_visible(cursor, count, FEED_ROWS);
    for (int i = 0; i < FEED_ROWS; i++) {
        row_t *r = &s.rows[i];
        int idx = first + i;
        if (idx >= count) {
            lv_obj_add_flag(r->box, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(r->bar, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(r->box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(r->bar, LV_OBJ_FLAG_HIDDEN);
        const gh_feed_item_t *it = &sn->feed[idx];
        lv_image_set_src(r->icon, event_icon(it->kind));
        if (it->kind == GH_EV_STAR) {
            lv_label_set_text(r->head, it->repo);
            if (it->actor[0]) {
                lv_label_set_text_fmt(r->sub, "%s 点了星标", it->actor);
            } else {
                lv_label_set_text_fmt(r->sub, "新增 %u 颗星标", (unsigned)it->number);
            }
        } else {
            lv_label_set_text_fmt(r->head, "%s #%u", it->repo, (unsigned)it->number);
            lv_label_set_text_fmt(r->sub, "%s · %s", event_verb(it->kind), it->title);
        }
        char ago[16];
        gh_fmt_ago(ago, sizeof(ago), m->now, it->at, tz_offset(m->now));
        lv_label_set_text(r->right, ago);
        row_select(r, idx == cursor);
    }
}

static void build_repos(lv_obj_t *scr) {
    build_list_header(scr, "仓库");
    for (int i = 0; i < REPO_ROWS; i++) {
        row_t *r = &s.rows[i];
        int y = 66 + i * REPO_ROW_H;
        r->box = box(scr, X0 - 2, y, XW + 2, REPO_ROW_H - 3, C_CARD);
        r->icon = icon(r->box, &ui_icon_lock, 4, 4);
        r->head = label(r->box, &ui_font_12, C_INK, 14, 3, 110, "");
        r->right = label_right(r->box, &ui_font_12, C_INK, 130, 3, 64, "");
        icon(r->box, &ui_icon_star_ink, 197, 4);
        r->sub = label_right(r->box, &ui_font_12, C_MUTED, 96, 17, 110, "");
        r->bar = box(r->box, 14, 21, 0, 3, C_FAINT);
    }
}

static uint32_t isqrt(uint32_t v) {
    uint32_t r = 0;
    while ((r + 1) * (r + 1) <= v) r++;
    return r;
}

static void refresh_repos(const ui_model_t *m) {
    const gh_snapshot_t *sn = m->snap;
    int count = sn->repo_count;
    if (sn->repo_total > count) {
        lv_label_set_text_fmt(s.list_count, "前 %d / 共 %u 个", count, (unsigned)sn->repo_total);
    } else {
        lv_label_set_text_fmt(s.list_count, "共 %d 个", count);
    }
    lv_label_set_text(s.empty, count ? "" : "等待第一次同步…");
    uint32_t max_root = count ? isqrt(sn->repos[0].stars) : 0;
    int cursor = m->nav->cursor[APP_PAGE_REPOS];
    int first = first_visible(cursor, count, REPO_ROWS);
    for (int i = 0; i < REPO_ROWS; i++) {
        row_t *r = &s.rows[i];
        int idx = first + i;
        if (idx >= count) {
            lv_obj_add_flag(r->box, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(r->box, LV_OBJ_FLAG_HIDDEN);
        const gh_repo_t *repo = &sn->repos[idx];
        bool sel = idx == cursor;
        if (repo->is_private) {
            lv_obj_remove_flag(r->icon, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(r->icon, LV_OBJ_FLAG_HIDDEN);
        }
        lv_label_set_text(r->head, repo->name);
        char cnt[12];
        gh_fmt_count(cnt, sizeof(cnt), repo->stars);
        lv_label_set_text(r->right, cnt);
        lv_label_set_text_fmt(r->sub, "Issue %u · PR %u", (unsigned)repo->issues, (unsigned)repo->prs);
        // 星数条:平方根比例,让小仓库也看得见;选中行用墨色,其余用淡墨。
        uint32_t w = max_root ? isqrt(repo->stars) * 100 / max_root : 0;
        lv_obj_set_width(r->bar, (int32_t)(repo->stars ? (w < 2 ? 2 : w) : 0));
        lv_obj_set_style_bg_color(r->bar, lv_color_hex(sel ? C_INK : C_FAINT), 0);
        row_select(r, sel);
    }
}

// ---------------------------------------------------------------------------
// 设备页
// ---------------------------------------------------------------------------

static const char *MENU_NAMES[UI_MENU_COUNT] = {
    "网页配网", "立即同步", "提示音", "拍手唤醒", "屏幕亮度", "息屏时间", "清除 Wi-Fi",
};

static void build_system(lv_obj_t *scr) {
    label(scr, &ui_font_24, C_INK, X0, 30, 120, "设备");
    box(scr, X0, 60, XW, 1, C_INK7);
    static const char *keys[4] = { "Wi-Fi", "GitHub", "同步", "电池" };
    for (int i = 0; i < 4; i++) {
        label(scr, &ui_font_12, C_MUTED, X0, 65 + i * 16, 48, keys[i]);
        s.info[i] = label(scr, &ui_font_12, C_INK, X0 + 50, 65 + i * 16, XW - 50, "");
    }
    for (int i = 0; i < UI_MENU_COUNT; i++) {
        row_t *r = &s.menu[i];
        r->box = box(scr, X0 - 2, 132 + i * 22, XW + 2, 20, C_INK);
        r->head = label(r->box, &ui_font_12, C_INK, 8, 4, 120, MENU_NAMES[i]);
        r->right = label_right(r->box, &ui_font_12, C_MUTED, 120, 4, 76, "");
    }
    label(scr, &ui_font_12, C_FAINT, X0, 289, XW, "OK 选择 · 长按 OK 确认");
}

static const char *fetch_text(const ui_model_t *m) {
    if (!m->token_set) return "未设置 token";
    switch (m->last_result) {
    case GH_FETCH_OK:           return "已授权";
    case GH_FETCH_NO_TOKEN:     return "未设置 token";
    case GH_FETCH_NETWORK:      return "网络错误";
    case GH_FETCH_AUTH:         return "token 无效";
    case GH_FETCH_RATE_LIMITED: return "触发限流";
    case GH_FETCH_BAD_RESPONSE: return "响应异常";
    case GH_FETCH_NO_MEMORY:    return "内存不足";
    }
    return "";
}

static void refresh_system(const ui_model_t *m) {
    const net_status_t *n = &m->net;
    switch (n->link) {
    case NET_LINK_ONLINE:
        lv_label_set_text_fmt(s.info[0], "%s %ddBm", n->ssid, n->rssi);
        break;
    case NET_LINK_CONNECTING:
        lv_label_set_text_fmt(s.info[0], "连接中 %s", n->ssid);
        break;
    case NET_LINK_OFFLINE:
        lv_label_set_text_fmt(s.info[0], "离线 %s (%u)", n->ssid, n->last_reason);
        break;
    case NET_LINK_NO_CREDENTIALS:
        lv_label_set_text(s.info[0], "未配网");
        break;
    }
    if (m->snap->partial && m->last_result == GH_FETCH_OK) {
        lv_label_set_text_fmt(s.info[1], "权限不足 · %s", m->last_detail);
    } else if (m->snap->rate_remaining >= 0 && m->last_result == GH_FETCH_OK) {
        lv_label_set_text_fmt(s.info[1], "%s · 余量 %d", fetch_text(m), (int)m->snap->rate_remaining);
    } else {
        lv_label_set_text_fmt(s.info[1], "%s %s", fetch_text(m),
                              m->last_result == GH_FETCH_OK ? "" : m->last_detail);
    }
    char ago[16];
    gh_fmt_ago(ago, sizeof(ago), m->now, m->snap->fetched_at, tz_offset(m->now));
    lv_label_set_text_fmt(s.info[2], "%s · 每 %d 秒", m->syncing ? "进行中" : ago, CONFIG_ASTRA_REFRESH_SECONDS);
    if (m->battery_soc >= 0) {
        lv_label_set_text_fmt(s.info[3], "%d%% · %dmV", m->battery_soc, m->battery_mv);
    } else {
        lv_label_set_text(s.info[3], "电量计不可用");
    }

    char val[UI_MENU_COUNT][16] = { { 0 } };
    snprintf(val[UI_MENU_PROVISION], 16, "%s", n->link == NET_LINK_ONLINE ? "已联网" : "");
    snprintf(val[UI_MENU_SYNC], 16, "%s", m->syncing ? "同步中" : "");
    snprintf(val[UI_MENU_SOUND], 16, "%s", m->prefs.sound_on ? "开" : "关");
    snprintf(val[UI_MENU_CLAP], 16, "%s", m->prefs.clap_wake ? "开" : "关");
    snprintf(val[UI_MENU_BRIGHTNESS], 16, "%d%%", m->prefs.brightness);
    if (m->prefs.screen_off_min) {
        snprintf(val[UI_MENU_SCREEN_OFF], 16, "%u 分钟", m->prefs.screen_off_min);
    } else {
        snprintf(val[UI_MENU_SCREEN_OFF], 16, "从不");
    }
    for (int i = 0; i < UI_MENU_COUNT; i++) {
        row_t *r = &s.menu[i];
        bool sel = i == m->nav->cursor[APP_PAGE_SYSTEM];
        // 选中项反白:墨底纸字(像素菜单的经典高亮)。
        lv_obj_set_style_bg_opa(r->box, sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(r->head, lv_color_hex(sel ? C_PAPER : C_INK), 0);
        lv_obj_set_style_text_color(r->right, lv_color_hex(sel ? C_FAINT : C_MUTED), 0);
        lv_label_set_text(r->right, val[i]);
    }
}

// ---------------------------------------------------------------------------
// 配网页
// ---------------------------------------------------------------------------

static void build_provisioning(lv_obj_t *scr) {
    label(scr, &ui_font_24, C_INK, X0, 30, XW, "网页配网");
    box(scr, X0, 60, XW, 1, C_INK7);
    lv_obj_t *c = card(scr, X0, 68, XW - 2, 160);
    // 标准 Wi-Fi 二维码:手机相机扫码即可加入热点。
    s.prov_qr = lv_qrcode_create(c);
    lv_qrcode_set_size(s.prov_qr, 104);
    lv_qrcode_set_dark_color(s.prov_qr, lv_color_hex(C_INK));
    lv_qrcode_set_light_color(s.prov_qr, lv_color_hex(C_CARD));
    lv_obj_set_pos(s.prov_qr, 6, 6);
    lv_obj_t *steps = label(c, &ui_font_12, C_INK, 116, 8, 0,
                            "1 扫码连热点\n2 网页会弹出\n  或打开\n  " APP_PORTAL_IP "\n3 填 Wi-Fi\n  和 token");
    lv_obj_set_style_text_line_space(steps, 4, 0);
    box(c, 6, 116, XW - 14, 1, C_HAIR);
    static const char *keys[3] = { "热点", "密码", "MAC" };
    for (int i = 0; i < 3; i++) {
        label(c, &ui_font_12, C_MUTED, 8, 120 + i * 13, 30, keys[i]);
        s.prov_info[i] = label(c, &ui_font_12, C_INK, 40, 120 + i * 13, XW - 50, "");
    }
    lv_obj_t *st = card(scr, X0, 238, XW - 2, 40);
    s.prov_icon = icon(st, &ui_icon_issue, 8, 15);   // 与单行文字垂直居中
    s.prov_status = label(st, &ui_font_12, C_INK, 22, 14, XW - 34, "");
    label(scr, &ui_font_12, C_FAINT, X0, 286, XW, "长按 OK 返回");
}

static void refresh_provisioning(const ui_model_t *m) {
    const char *text = "";
    bool warn = false;
    switch (m->net.prov) {
    case NET_PROV_OFF:
    case NET_PROV_STARTING:         text = "正在扫描附近网络并打开热点…"; break;
    case NET_PROV_AP_READY:         text = "等待手机连接热点"; break;
    case NET_PROV_CLIENT_CONNECTED: text = "手机已连接,请在网页中填写"; break;
    case NET_PROV_WIFI_CONNECTING:  text = "正在连接 Wi-Fi…"; break;
    case NET_PROV_WIFI_FAILED:      text = "连接失败:请检查密码后重新提交"; warn = true; break;
    case NET_PROV_DONE:             text = "配网成功,马上开始同步"; break;
    case NET_PROV_FAILED:           text = "热点启动失败,长按 OK 返回"; warn = true; break;
    }
    lv_label_set_text(s.prov_status, text);
    lv_image_set_src(s.prov_icon, warn ? &ui_icon_pr_closed : &ui_icon_issue);
    lv_label_set_text(s.prov_info[0], m->net.ap_ssid[0] ? m->net.ap_ssid : "…");
    lv_label_set_text(s.prov_info[1], m->net.ap_pass[0] ? m->net.ap_pass : "…");
    lv_label_set_text(s.prov_info[2], m->net.mac[0] ? m->net.mac : "读取中…");
    // 只在热点信息变化时重新生成二维码(编码一次要几十毫秒)。
    char qr[96];
    if (m->net.ap_pass[0] && portal_wifi_qr(qr, sizeof(qr), m->net.ap_ssid, m->net.ap_pass) &&
        strcmp(qr, s.prov_qr_data) != 0) {
        strlcpy(s.prov_qr_data, qr, sizeof(s.prov_qr_data));
        lv_qrcode_update(s.prov_qr, qr, (uint32_t)strlen(qr));
    }
}

// ---------------------------------------------------------------------------
// 提示卡
// ---------------------------------------------------------------------------

static void toast_set_y(void *obj, int32_t v) {
    lv_obj_set_y(obj, v);
}

static void toast_slide(int32_t from, int32_t to, uint32_t ms, lv_anim_path_cb_t path) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s.toast);
    lv_anim_set_exec_cb(&a, toast_set_y);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, path);
    lv_anim_start(&a);
}

static void toast_hide(lv_timer_t *t) {
    (void)t;
    s.toast_timer = NULL;
    if (s.toast) toast_slide(lv_obj_get_y(s.toast), -60, 200, lv_anim_path_ease_in);
}

static void build_toast(lv_obj_t *scr) {
    s.toast = box(scr, X0, -60, XW - 2, 42, C_CARD);
    outline(s.toast, C_INK);
    lv_obj_set_style_border_width(s.toast, 2, 0);
    s.toast_icon = icon(s.toast, &ui_icon_star, 8, 8);
    s.toast_head = label(s.toast, &ui_font_12, C_INK, 22, 5, 180, "");
    s.toast_sub = label(s.toast, &ui_font_12, C_MUTED, 22, 21, 180, "");
}

static void toast_show(uint32_t hold_ms) {
    lv_obj_move_foreground(s.toast);
    toast_slide(lv_obj_get_y(s.toast), 30, 300, lv_anim_path_ease_out);
    if (s.toast_timer) lv_timer_delete(s.toast_timer);
    s.toast_timer = lv_timer_create(toast_hide, hold_ms, NULL);
    lv_timer_set_repeat_count(s.toast_timer, 1);
}

void ui_message(const ui_model_t *m, bool warn, const char *head, const char *sub) {
    (void)m;
    if (!s.toast) return;
    lv_image_set_src(s.toast_icon, warn ? &ui_icon_pr_closed : &ui_icon_issue);
    lv_label_set_text(s.toast_head, head);
    lv_label_set_text(s.toast_sub, sub ? sub : "");
    toast_show(warn ? 3500 : 2500);
    if (warn && s.lynn) {
        lynn_anim_react(&s_anim, LA_CONFUSED, lv_tick_get(), 2000);
        apply_pose();
    }
}

void ui_notify(const ui_model_t *m, const gh_feed_item_t *item, int32_t star_delta) {
    if (!s.toast) return;
    lynn_act_t react = LA_HOP;
    if (item) {
        lv_image_set_src(s.toast_icon, event_icon(item->kind));
        if (item->kind == GH_EV_STAR) {
            lv_label_set_text_fmt(s.toast_head, "新星标 · %s", item->repo);
            if (item->actor[0]) {
                lv_label_set_text_fmt(s.toast_sub, "来自 %s", item->actor);
            } else {
                lv_label_set_text_fmt(s.toast_sub, "新增 %u 颗星标", (unsigned)item->number);
            }
        } else {
            lv_label_set_text_fmt(s.toast_head, "%s · %s #%u", event_verb(item->kind), item->repo,
                                  (unsigned)item->number);
            lv_label_set_text(s.toast_sub, item->title);
        }
        switch (item->kind) {
        case GH_EV_ISSUE_OPEN: react = LA_INSPECT; break;
        case GH_EV_PR_OPEN:    react = LA_REVIEW; break;
        case GH_EV_PR_MERGED:  react = LA_PARTY; break;
        default:               react = LA_HOP; break;
        }
    } else {
        lv_image_set_src(s.toast_icon, &ui_icon_star);
        lv_label_set_text_fmt(s.toast_head, "星标 %+d", (int)star_delta);
        lv_label_set_text(s.toast_sub, "组织的仓库又被关注了");
    }
    if (star_delta > 0 && (!item || item->kind != GH_EV_STAR)) {
        char sub[GH_TITLE_LEN];
        snprintf(sub, sizeof(sub), "%s", lv_label_get_text(s.toast_sub));
        lv_label_set_text_fmt(s.toast_sub, "星标 +%d · %s", (int)star_delta, sub);
    }
    // 总星数跨过 50 的整数倍:里程碑,拉礼花。
    uint32_t total = m->snap ? m->snap->total_stars : 0;
    if (star_delta > 0 && total / 50 > (total - (uint32_t)star_delta) / 50) {
        react = LA_PARTY;
        lv_image_set_src(s.toast_icon, &ui_icon_star);
        lv_label_set_text_fmt(s.toast_head, "里程碑 · %u 颗星标", (unsigned)(total / 50 * 50));
        lv_label_set_text_fmt(s.toast_sub, "本次新增 %d 颗", (int)star_delta);
    }
    toast_show(4500);

    // 星标:跳 4 下;合并/里程碑:拉礼花;issue:拿放大镜端详;PR:审阅。播完各保持几秒后回到日常。
    lynn_anim_react(&s_anim, react, lv_tick_get(), react == LA_HOP || react == LA_PARTY ? 3000 : 4000);
    apply_pose();
}

// ---------------------------------------------------------------------------
// 页面生命周期
// ---------------------------------------------------------------------------

void ui_init(void) {
    memset(&s, 0, sizeof(s));
    lynn_anim_init(&s_anim, lv_tick_get(), lv_rand(1, 0x7FFFFFFF));
    s.anim_timer = lv_timer_create(anim_tick, 50, NULL);
}

void ui_build(const ui_model_t *m) {
    lv_obj_t *old = s.scr;
    if (s.toast_timer) {
        lv_timer_delete(s.toast_timer);
        s.toast_timer = NULL;
    }
    if (s.toast) lv_anim_delete(s.toast, toast_set_y);
    // 清空旧页的控件指针(定时器回调可能还会访问它们)。
    lv_timer_t *anim = s.anim_timer;
    uint8_t idle = s.idle;
    memset(&s, 0, sizeof(s));
    s.anim_timer = anim;
    s.idle = idle;

    // 先换上空白新屏并删除旧屏,再往新屏里建控件:LVGL 内存池只有 32KB,
    // 新旧两页同时存在会耗尽它(仓库页最大),lv_*_create 返回 NULL 后崩溃。
    s.scr = box(NULL, 0, 0, W, H, C_PAPER);
    lv_screen_load(s.scr);
    if (old) lv_obj_delete(old);
    s.built_provisioning = m->provisioning;
    s.built_page = m->nav->page;
    static const char *titles[APP_PAGE_COUNT] = { "AstraFoundry", "AstraFoundry", "AstraFoundry", "Astra Monitor" };
    build_chrome(s.scr, m->provisioning ? "Astra Monitor" : titles[m->nav->page], !m->provisioning);
    if (m->provisioning) {
        build_provisioning(s.scr);
    } else {
        switch (m->nav->page) {
        case APP_PAGE_HOME:   build_home(s.scr); break;
        case APP_PAGE_FEED:   build_feed(s.scr); break;
        case APP_PAGE_REPOS:  build_repos(s.scr); break;
        case APP_PAGE_SYSTEM: build_system(s.scr); break;
        default: break;
        }
    }
    build_toast(s.scr);
    ui_refresh(m);
}

void ui_refresh(const ui_model_t *m) {
    if (!s.scr) return;
    if (m->provisioning != s.built_provisioning || (!m->provisioning && m->nav->page != s.built_page)) {
        ui_build(m);
        return;
    }
    s.idle = m->idle;
    refresh_chrome(m);
    if (m->provisioning) {
        refresh_provisioning(m);
        return;
    }
    switch (m->nav->page) {
    case APP_PAGE_HOME:   refresh_home(m); break;
    case APP_PAGE_FEED:   refresh_feed(m); break;
    case APP_PAGE_REPOS:  refresh_repos(m); break;
    case APP_PAGE_SYSTEM: refresh_system(m); break;
    default: break;
    }
}

void ui_set_idle(const ui_model_t *m) {
    s.idle = m->idle;
    if (m->idle == APP_IDLE_OFF) s_anim.reaction = LA_COUNT;   // 关屏时放弃未播完的反应
    ui_refresh(m);
}
