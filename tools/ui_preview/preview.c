// tools/ui_preview/preview.c —— 在电脑上用真实的 main/ui.c + LVGL 渲染界面截图。
//
// 不依赖硬件:用示例数据构造 ui_model_t,逐个场景 ui_build/ui_refresh,
// 把 240x320 帧缓冲按设备的 30px 圆角遮罩写成 PPM。由 tools/ui_preview/run.sh 调用。
#include "ui.h"

#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 240
#define H 320
#define RADIUS 30

static uint16_t s_fb[W * H];
static uint8_t s_draw_buf[W * H * 2];
static uint32_t s_tick;

static uint32_t tick_cb(void) { return s_tick; }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    const uint16_t *src = (const uint16_t *)px;
    for (int32_t y = area->y1; y <= area->y2; y++) {
        for (int32_t x = area->x1; x <= area->x2; x++) s_fb[y * W + x] = *src++;
    }
    lv_display_flush_ready(disp);
}

static void pump(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) {
        s_tick += 10;
        lv_timer_handler();
    }
}

static bool outside_corner(int x, int y) {
    int cx = x < RADIUS ? RADIUS : x >= W - RADIUS ? W - RADIUS - 1 : x;
    int cy = y < RADIUS ? RADIUS : y >= H - RADIUS ? H - RADIUS - 1 : y;
    int dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy > RADIUS * RADIUS;
}

static void dump(const char *dir, const char *name) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint16_t c = outside_corner(x, y) ? 0 : s_fb[y * W + x];
            uint8_t rgb[3] = { (uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 0x3F) << 2), (uint8_t)((c & 0x1F) << 3) };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    printf("wrote %s\n", path);
}

static gh_feed_item_t item(gh_ev_kind_t kind, const char *repo, const char *actor, uint32_t num,
                           const char *title, int64_t at) {
    gh_feed_item_t it = { .kind = kind, .number = num, .at = at };
    snprintf(it.repo, sizeof(it.repo), "%s", repo);
    snprintf(it.actor, sizeof(it.actor), "%s", actor);
    snprintf(it.title, sizeof(it.title), "%s", title);
    return it;
}

static void sample(gh_snapshot_t *s, int64_t now) {
    static const struct { const char *name; uint32_t stars, issues, prs; bool priv; int64_t ago; } R[] = {
        { "KumoApp", 297, 4, 1, false, 3600 * 30 }, { "Arion", 86, 1, 0, false, 3600 * 5 },
        { "republic", 4, 0, 0, false, 86400 * 9 }, { "design", 3, 0, 0, false, 86400 * 2 },
        { "telo", 2, 0, 1, false, 86400 * 4 }, { "Lyra", 0, 1, 0, true, 3600 * 70 },
        { "Arion-evaluation", 0, 1, 0, true, 86400 * 2 }, { "astrafoundry.si", 0, 0, 0, false, 3600 * 9 },
        { "workspace", 0, 0, 0, true, 86400 * 4 }, { "Kumo-docs", 0, 0, 0, false, 86400 * 170 },
    };
    memset(s, 0, sizeof(*s));
    for (size_t i = 0; i < sizeof(R) / sizeof(R[0]); i++) {
        gh_repo_t *r = &s->repos[s->repo_count++];
        snprintf(r->name, sizeof(r->name), "%s", R[i].name);
        r->stars = R[i].stars;
        r->issues = R[i].issues;
        r->prs = R[i].prs;
        r->is_private = R[i].priv;
        r->pushed_at = now - R[i].ago;
    }
    s->repo_total = 15;
    gh_feed_item_t f[] = {
        item(GH_EV_PR_MERGED, "Arion", "stvlynn", 42, "修复同步时的内存泄漏", now - 300),
        item(GH_EV_STAR, "KumoApp", "xdroff", 0, "", now - 1200),
        item(GH_EV_ISSUE_OPEN, "Lyra", "k-azv", 17, "Crash when opening settings on iPad", now - 3600 * 2),
        item(GH_EV_STAR, "design", "k-azv", 0, "", now - 3600 * 7),
        item(GH_EV_PR_OPEN, "telo", "tszlznl", 9, "支持多账号切换", now - 86400),
        item(GH_EV_ISSUE_CLOSED, "Arion-evaluation", "mcwq123", 3, "Benchmark 结果不一致", now - 86400 * 2),
        item(GH_EV_PR_CLOSED, "republic", "NieXi", 5, "Draft: new layout", now - 86400 * 3),
    };
    for (size_t i = 0; i < sizeof(f) / sizeof(f[0]); i++) gh_feed_insert(s, &f[i]);
    gh_snapshot_totals(s);
    s->rate_remaining = 4987;
    s->fetched_at = now - 40;
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : ".";
    setenv("TZ", "CST-8", 1);
    tzset();
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_draw_buf, NULL, sizeof(s_draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);

    int64_t now = 1793771325;   // 2026-11-04 13:48 (UTC+8)
    static gh_snapshot_t snap, empty;
    sample(&snap, now);
    empty.rate_remaining = -1;

    app_nav_t nav;
    app_nav_init(&nav, 0);
    app_nav_set_items(&nav, APP_PAGE_HOME, snap.repo_count);
    app_nav_set_items(&nav, APP_PAGE_FEED, snap.feed_count);
    app_nav_set_items(&nav, APP_PAGE_REPOS, snap.repo_count);
    app_nav_set_items(&nav, APP_PAGE_SYSTEM, UI_MENU_COUNT);

    ui_model_t m = {
        .snap = &snap, .nav = &nav, .org = "AstraFoundry",
        .net = { .link = NET_LINK_ONLINE, .ssid = "Studio-2.4G", .ip = "192.168.1.42", .rssi = -58, .time_synced = true },
        .prefs = { .sound_on = false, .clap_wake = true, .brightness = 100, .screen_off_min = 10 },
        .today_stars = 3, .battery_soc = 86, .battery_mv = 4012, .token_set = true,
        .last_result = GH_FETCH_OK, .now = now, .next_sync_in_s = 80,
    };

    ui_init();
    nav.page = APP_PAGE_LYNN;
    ui_build(&m);
    ui_lynn_play("pat", NULL);
    pump(1500);
    dump(out, "00-lynn");
    // Grok replies are capped at 30 characters by the bot: one page.
    ui_lynn_bot_text("今天北京晴，最高18度，适合出门散步，记得带件外套哦。");
    pump(100);
    dump(out, "00b-lynn-reply");
    if (ui_lynn_page_next()) { fprintf(stderr, "FAIL: a 30-char reply must fit one page\n"); return 1; }
    // Longer text pages with an OK tap.
    ui_lynn_bot_text("Connector unreachable. Check that the connector service is running on your server, "
                     "then hold OK to retry. The device keeps your settings.");
    pump(100);
    dump(out, "00c-lynn-page1");
    if (!ui_lynn_page_next()) { fprintf(stderr, "FAIL: long text must page\n"); return 1; }
    pump(100);
    dump(out, "00d-lynn-page2");
    // 回归:在 Lynn 页关屏后唤醒,再触发动作,舞台必须继续走帧(曾经停在关屏前的画面)。
    m.idle = APP_IDLE_OFF;
    ui_set_idle(&m);
    pump(500);
    m.idle = APP_IDLE_AWAKE;
    ui_refresh(&m);                       // 唤醒路径只调用 ui_refresh
    ui_lynn_play("spin", NULL);
    char st0[160], st1[160];
    ui_lynn_status(st0, sizeof(st0));
    pump(3000);
    ui_lynn_status(st1, sizeof(st1));
    printf("after wake: %s -> %s\n", st0, st1);
    if (strstr(st1, "action=spin")) {
        fprintf(stderr, "FAIL: stage stayed frozen after waking on the Lynn page\n");
        return 1;
    }
    nav.page = APP_PAGE_HOME;
    ui_build(&m);
    pump(200);
    dump(out, "01-home");

    nav.cursor[APP_PAGE_HOME] = 1;
    ui_refresh(&m);
    ui_notify(&m, &snap.feed[0], 2);
    pump(600);
    dump(out, "02-home-notify");

    pump(7000);
    nav.page = APP_PAGE_FEED;
    nav.cursor[APP_PAGE_FEED] = 1;
    ui_refresh(&m);
    pump(200);
    dump(out, "03-feed");

    nav.page = APP_PAGE_REPOS;
    nav.cursor[APP_PAGE_REPOS] = 0;
    ui_refresh(&m);
    pump(200);
    dump(out, "04-repos");

    nav.page = APP_PAGE_SYSTEM;
    nav.cursor[APP_PAGE_SYSTEM] = UI_MENU_SCREEN_OFF;
    ui_refresh(&m);
    pump(200);
    dump(out, "05-system");

    m.provisioning = true;
    m.net.prov = NET_PROV_CLIENT_CONNECTED;
    snprintf(m.net.ap_ssid, sizeof(m.net.ap_ssid), "AstraMonitor-0554");
    snprintf(m.net.ap_pass, sizeof(m.net.ap_pass), "40726315");
    snprintf(m.net.mac, sizeof(m.net.mac), "4C:11:AE:31:05:54");
    ui_refresh(&m);
    pump(200);
    dump(out, "06-provisioning");

    m.provisioning = false;
    nav.page = APP_PAGE_HOME;
    m.snap = &empty;
    m.today_stars = -1;
    m.net = (net_status_t){ .link = NET_LINK_NO_CREDENTIALS };
    m.battery_soc = -1;
    m.now = 0;
    ui_refresh(&m);
    pump(200);
    dump(out, "07-home-no-wifi");

    m.snap = &snap;
    m.net = (net_status_t){ .link = NET_LINK_ONLINE, .ssid = "Studio-2.4G", .rssi = -75, .time_synced = true };
    m.now = now;
    m.today_stars = 0;
    m.battery_soc = 12;
    m.idle = APP_IDLE_OFF;
    ui_set_idle(&m);
    pump(200);
    dump(out, "08-home-sleep");

    m.idle = APP_IDLE_AWAKE;
    m.battery_soc = 86;
    nav.page = APP_PAGE_SYSTEM;
    nav.cursor[APP_PAGE_SYSTEM] = UI_MENU_FORGET_WIFI;
    ui_refresh(&m);
    ui_message(&m, true, "确认清除 Wi-Fi?", "3 秒内再长按 OK 确认");
    pump(600);
    dump(out, "09-system-confirm");
    return 0;
}
