// main/app_main.c —— Astra Monitor:GitHub 组织监控面板(AstraFoundry)。
//
// 任务划分:
//   app     唯一操作界面与应用状态的任务:按键、同步结果、联网变化、息屏计时。
//   sync    周期调用 GitHub GraphQL;结果经队列交给 app,不直接碰界面。
//   audio   独占 I2S:芯片音效 + 息屏时的拍手检测(app_audio.c)。
//   console USB 串口命令:写入 token / Wi-Fi(app_console.c)。
// 按键回调与网络/音频回调只投递事件,不阻塞、不访问 LVGL。
//
// 注:main/ 下的 main.c、demo_*.c、ui_pixel* 是仓库基线的硬件测试演示,
// 不参与本应用的构建,仅为宿主测试与参考保留。
#include "app_audio.h"
#include "app_console.h"
#include "app_nav.h"
#include "app_net.h"
#include "app_settings.h"
#include "gh_fetch.h"
#include "gh_model.h"
#include "ui.h"

#include "lvgl.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "astra";

#define DIM_PERCENT        12
#define TICK_MS            250
#define BATTERY_PERIOD_MS  30000
#define PROV_DONE_LINGER_MS 3000
#define CONFIRM_WINDOW_MS  3000

typedef enum {
    EV_KEY = 0,
    EV_SYNC_START,
    EV_SYNC_DONE,
    EV_NET,
    EV_CLAP,
    EV_CONSOLE,
} ev_type_t;

typedef struct {
    ev_type_t type;
    uint8_t a;
    uint8_t b;
} app_event_t;

static QueueHandle_t s_events;
static TaskHandle_t s_sync_task;

// sync → app 的交接区
static SemaphoreHandle_t s_handoff_lock;
static gh_snapshot_t s_handoff;
static gh_fetch_result_t s_handoff_result;
static char s_handoff_detail[48];

// app 任务私有状态
static gh_snapshot_t s_snap;
static app_nav_t s_nav;
static ui_model_t s_model;
static bool s_provisioning;
static int64_t s_prov_done_at;
static int64_t s_confirm_until;
static int64_t s_last_sync_ms;
static bool s_manual_sync;          // 用户长按触发的同步:结束时给出结果提示
static uint32_t s_day_key, s_day_stars;
static bool s_ui_ready;

static int64_t now_ms(void) {
    return esp_timer_get_time() / 1000;
}

static void post(ev_type_t type, uint8_t a, uint8_t b) {
    app_event_t ev = { .type = type, .a = a, .b = b };
    (void)xQueueSend(s_events, &ev, 0);
}

// ---------------------------------------------------------------------------
// 回调(其它任务上下文):只投递事件
// ---------------------------------------------------------------------------

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    // 上/下 长按留给"戳看板娘";上/下 双击不使用(单击即翻页)。
    if (ev == BSP_BTN_CLICK || ev == BSP_BTN_LONG || (btn == BSP_BTN_OK && ev == BSP_BTN_DOUBLE)) {
        post(EV_KEY, (uint8_t)btn, (uint8_t)ev);
    }
}

static void on_net(void) {
    post(EV_NET, 0, 0);
}

static void on_clap(void) {
    post(EV_CLAP, 0, 0);
}

static void on_console(app_console_cmd_t cmd) {
    post(EV_CONSOLE, (uint8_t)cmd, 0);
}

// ---------------------------------------------------------------------------
// 同步任务
// ---------------------------------------------------------------------------

static void sync_task(void *arg) {
    (void)arg;
    static gh_snapshot_t buf;
    static char token[APP_TOKEN_MAX];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CONFIG_ASTRA_REFRESH_SECONDS * 1000));
        if (s_provisioning || app_net_status().link != NET_LINK_ONLINE) continue;
        char detail[48];
        gh_fetch_result_t r;
        if (!app_settings_token(token, sizeof(token))) {
            r = GH_FETCH_NO_TOKEN;
            detail[0] = '\0';
        } else {
            post(EV_SYNC_START, 0, 0);
            r = gh_fetch_snapshot(CONFIG_ASTRA_GH_ORG, token, (int64_t)time(NULL), &buf,
                                  detail, sizeof(detail));
        }
        memset(token, 0, sizeof(token));
        xSemaphoreTake(s_handoff_lock, portMAX_DELAY);
        s_handoff_result = r;
        strlcpy(s_handoff_detail, detail, sizeof(s_handoff_detail));
        if (r == GH_FETCH_OK) s_handoff = buf;
        xSemaphoreGive(s_handoff_lock);
        post(EV_SYNC_DONE, 0, 0);
        ESP_LOGI(TAG, "同步结果 %d,空闲堆 %u,最大块 %u", r,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
}

static void request_sync(void) {
    if (s_sync_task) xTaskNotifyGive(s_sync_task);
}

static void refresh_model(void);

static void show_message(bool warn, const char *head, const char *sub) {
    app_audio_play(warn ? SOUND_ERROR : SOUND_TICK);
    refresh_model();
    if (!bsp_lvgl_lock(500)) return;
    ui_message(&s_model, warn, head, sub);
    bsp_lvgl_unlock();
}

// 长按确认"立即同步":能同步就开始并提示,不能同步就说明原因(而不是静默忽略)。
static void manual_sync(void) {
    net_status_t net = app_net_status();
    if (net.link == NET_LINK_NO_CREDENTIALS) {
        show_message(true, "无法同步", "还没有 Wi-Fi,去设备页网页配网");
    } else if (net.link != NET_LINK_ONLINE) {
        show_message(true, "无法同步", "Wi-Fi 未连接,正在重连");
    } else if (!app_settings_has_token()) {
        show_message(true, "无法同步", "缺少 token:串口输入 token <值>");
    } else if (s_model.syncing) {
        show_message(false, "正在同步", "请稍候");
    } else {
        s_manual_sync = true;
        request_sync();
        show_message(false, "开始同步", CONFIG_ASTRA_GH_ORG);
    }
}

// ---------------------------------------------------------------------------
// 应用状态
// ---------------------------------------------------------------------------

static void apply_backlight(void) {
    uint8_t level = s_model.prefs.brightness;
    if (s_nav.idle == APP_IDLE_DIM) level = DIM_PERCENT;
    if (s_nav.idle == APP_IDLE_OFF) level = 0;
    bsp_display_backlight(level);
    // 只有屏幕没亮时才听拍手:醒着的时候用按键就够了。
    app_audio_set_listening(s_model.prefs.clap_wake && s_nav.idle != APP_IDLE_AWAKE);
}

static void update_today(void) {
    s_model.today_stars = -1;
    if (s_snap.fetched_at == 0 || !s_model.net.time_synced) return;
    time_t t = time(NULL);
    struct tm lt;
    localtime_r(&t, &lt);
    uint32_t key = (uint32_t)((lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday);
    if (gh_day_baseline(key, s_snap.total_stars, s_snap.partial, &s_day_key, &s_day_stars,
                        &s_model.today_stars)) {
        app_settings_set_day_baseline(s_day_key, s_day_stars);
    }
}

static void refresh_model(void) {
    s_model.snap = &s_snap;
    s_model.nav = &s_nav;
    s_model.org = CONFIG_ASTRA_GH_ORG;
    s_model.net = app_net_status();
    s_model.prefs = app_settings_prefs();
    s_model.token_set = app_settings_has_token();
    s_model.provisioning = s_provisioning;
    s_model.now = s_model.net.time_synced ? (int64_t)time(NULL) : 0;
    int64_t next = CONFIG_ASTRA_REFRESH_SECONDS - (now_ms() - s_last_sync_ms) / 1000;
    s_model.next_sync_in_s = next < 0 ? 0 : next;
    s_model.idle = (uint8_t)s_nav.idle;
    update_today();
}

static void ui_update(bool rebuild) {
    refresh_model();
    if (!bsp_lvgl_lock(500)) return;
    if (rebuild) {
        ui_build(&s_model);
    } else {
        ui_refresh(&s_model);
    }
    bsp_lvgl_unlock();
}

static sound_id_t sound_for(const gh_feed_item_t *it, int32_t star_delta) {
    if (!it) return star_delta > 0 ? SOUND_STAR : SOUND_NONE;
    switch (it->kind) {
    case GH_EV_STAR:       return SOUND_STAR;
    case GH_EV_ISSUE_OPEN: return SOUND_ISSUE;
    case GH_EV_PR_OPEN:    return SOUND_PR;
    case GH_EV_PR_MERGED:  return SOUND_MERGED;
    default:               return star_delta > 0 ? SOUND_STAR : SOUND_TICK;
    }
}

static void handle_sync_done(void) {
    static gh_snapshot_t next;
    xSemaphoreTake(s_handoff_lock, portMAX_DELAY);
    gh_fetch_result_t r = s_handoff_result;
    s_model.last_result = r;
    strlcpy(s_model.last_detail, s_handoff_detail, sizeof(s_model.last_detail));
    if (r == GH_FETCH_OK) next = s_handoff;
    xSemaphoreGive(s_handoff_lock);
    s_model.syncing = false;
    s_last_sync_ms = now_ms();

    bool manual = s_manual_sync;
    s_manual_sync = false;
    if (r != GH_FETCH_OK) {
        ESP_LOGW(TAG, "同步失败 %d: %s", r, s_model.last_detail);
        ui_update(false);
        if (manual) show_message(true, "同步失败", s_model.last_detail[0] ? s_model.last_detail : "稍后重试");
        return;
    }
    gh_changes_t ch;
    gh_carry_star_events(&s_snap, &next, next.fetched_at);
    gh_diff(&s_snap, &next, &ch);
    s_snap = next;
    app_nav_set_items(&s_nav, APP_PAGE_HOME, s_snap.repo_count);
    app_nav_set_items(&s_nav, APP_PAGE_FEED, s_snap.feed_count);
    app_nav_set_items(&s_nav, APP_PAGE_REPOS, s_snap.repo_count);

    const gh_feed_item_t *head = ch.headline >= 0 ? &s_snap.feed[ch.headline] : NULL;
    bool notable = head || ch.star_delta > 0;
    if (notable) {
        bool was_idle = app_nav_poke(&s_nav, now_ms());
        if (was_idle) apply_backlight();
        app_audio_play(sound_for(head, ch.star_delta));
    }
    refresh_model();
    if (!bsp_lvgl_lock(500)) return;
    ui_refresh(&s_model);
    if (notable) {
        ui_notify(&s_model, head, ch.star_delta);
    } else if (manual) {
        ui_message(&s_model, false, "同步完成", "没有新动态");
    }
    bsp_lvgl_unlock();
}

static void provisioning_enter(void) {
    s_provisioning = true;
    s_prov_done_at = 0;
    ui_update(true);
    app_net_provision_start();
    ui_update(false);
}

static void provisioning_exit(void) {
    app_net_provision_stop();
    s_provisioning = false;
    s_nav.page = APP_PAGE_HOME;
    ui_update(true);
    request_sync();
}

static void run_menu(ui_menu_t item) {
    app_prefs_t p = app_settings_prefs();
    switch (item) {
    case UI_MENU_PROVISION:
        provisioning_enter();
        return;
    case UI_MENU_SYNC:
        manual_sync();
        return;
    case UI_MENU_FORGET_WIFI:
        if (app_net_status().link == NET_LINK_NO_CREDENTIALS) {
            show_message(true, "没有可清除的 Wi-Fi", "设备上未保存 Wi-Fi");
            return;
        }
        // 破坏性操作:需要在 3 秒内再长按一次确认。
        if (now_ms() > s_confirm_until) {
            s_confirm_until = now_ms() + CONFIRM_WINDOW_MS;
            show_message(true, "确认清除 Wi-Fi?", "3 秒内再长按 OK 确认");
            return;
        }
        s_confirm_until = 0;
        app_net_clear_credentials();
        ui_update(false);
        show_message(false, "已清除 Wi-Fi", "可通过网页配网重新设置");
        return;
    case UI_MENU_SOUND:
        p.sound_on = !p.sound_on;
        app_audio_set_enabled(true);   // 先确保"开启"的提示音能播出
        break;
    case UI_MENU_CLAP:
        p.clap_wake = !p.clap_wake;
        break;
    case UI_MENU_BRIGHTNESS:
        p.brightness = p.brightness > 70 ? 70 : p.brightness > 40 ? 40 : p.brightness > 20 ? 20 : 100;
        break;
    case UI_MENU_SCREEN_OFF:
        p.screen_off_min = (uint8_t)app_nav_next_screen_off(p.screen_off_min);
        break;
    default:
        return;
    }
    app_settings_set_prefs(&p);
    apply_backlight();
    ui_update(false);
    char sub[48];
    switch (item) {
    case UI_MENU_SOUND:
        show_message(false, "提示音", p.sound_on ? "已开启" : "已关闭");
        app_audio_set_enabled(p.sound_on);
        break;
    case UI_MENU_CLAP:
        show_message(false, "拍手唤醒", p.clap_wake ? "已开启:息屏时拍两下手" : "已关闭");
        break;
    case UI_MENU_SCREEN_OFF:
        if (p.screen_off_min) {
            snprintf(sub, sizeof(sub), "%u 分钟无操作后关屏", p.screen_off_min);
        } else {
            snprintf(sub, sizeof(sub), "从不关屏(仍会变暗)");
        }
        show_message(false, "息屏时间", sub);
        break;
    default:
        snprintf(sub, sizeof(sub), "%d%%", p.brightness);
        show_message(false, "屏幕亮度", sub);
        break;
    }
}

static app_key_t map_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (btn == BSP_BTN_UP) return APP_KEY_UP;
    if (btn == BSP_BTN_DOWN) return APP_KEY_DOWN;
    if (ev == BSP_BTN_DOUBLE) return APP_KEY_OK_DOUBLE;
    if (ev == BSP_BTN_LONG) return APP_KEY_OK_LONG;
    return APP_KEY_OK;
}

// 总览页长按 上/下:戳一下 Lynn。息屏时先唤醒(唤醒那一下不算戳)。
static void poke_lynn(void) {
    if (app_nav_poke(&s_nav, now_ms())) {
        apply_backlight();
        ui_update(false);
        return;
    }
    if (s_provisioning || s_nav.page != APP_PAGE_HOME) return;
    refresh_model();
    if (!bsp_lvgl_lock(500)) return;
    bool poked = ui_poke(&s_model);
    bsp_lvgl_unlock();
    if (poked) app_audio_play(SOUND_TICK);
}

static void handle_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (btn != BSP_BTN_OK && ev == BSP_BTN_LONG) {
        poke_lynn();
        return;
    }
    app_key_t key = map_key(btn, ev);
    if (s_provisioning) {
        app_nav_poke(&s_nav, now_ms());
        if (key == APP_KEY_OK_LONG) provisioning_exit();
        return;
    }
    switch (app_nav_key(&s_nav, key, now_ms())) {
    case APP_NAV_WAKE:
        apply_backlight();
        ui_update(false);
        break;
    case APP_NAV_PAGE:
        ui_update(true);
        break;
    case APP_NAV_CURSOR:
        ui_update(false);
        break;
    case APP_NAV_ACTION:
        if (s_nav.page == APP_PAGE_SYSTEM) {
            run_menu((ui_menu_t)s_nav.cursor[APP_PAGE_SYSTEM]);
        } else {
            manual_sync();
        }
        break;
    case APP_NAV_NONE:
        break;
    }
}

static void print_status(void) {
    refresh_model();
    printf("wifi=%d ssid=\"%s\" ip=%s mac=%s rssi=%d prov=%d time=%d\n", s_model.net.link,
           s_model.net.ssid, s_model.net.ip, s_model.net.mac, s_model.net.rssi, s_model.net.prov, s_model.net.time_synced);
    printf("org=%s token=%s last=%d partial=%d \"%s\" repos=%u stars=%u issues=%u prs=%u rate=%d today=%d\n",
           CONFIG_ASTRA_GH_ORG, s_model.token_set ? "set" : "missing", s_model.last_result, s_snap.partial,
           s_model.last_detail, s_snap.repo_count, (unsigned)s_snap.total_stars,
           (unsigned)s_snap.total_issues, (unsigned)s_snap.total_prs, (int)s_snap.rate_remaining,
           (int)s_model.today_stars);
    if (bsp_lvgl_lock(500)) {
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        bsp_lvgl_unlock();
        printf("lvgl used=%u%% max=%u free_biggest=%u\n", mon.used_pct, (unsigned)mon.max_used,
               (unsigned)mon.free_biggest_size);
    }
    printf("heap free=%u min=%u largest=%u\n", (unsigned)esp_get_free_heap_size(),
           (unsigned)esp_get_minimum_free_heap_size(),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static void app_task(void *arg) {
    (void)arg;
    int64_t last_ui = 0, last_batt = -BATTERY_PERIOD_MS;
    net_link_t last_link = NET_LINK_NO_CREDENTIALS;
    for (;;) {
        app_event_t ev;
        if (xQueueReceive(s_events, &ev, pdMS_TO_TICKS(TICK_MS)) == pdTRUE) {
            switch (ev.type) {
            case EV_KEY:
                handle_key((bsp_btn_t)ev.a, (bsp_btn_ev_t)ev.b);
                break;
            case EV_SYNC_START:
                s_model.syncing = true;
                ui_update(false);
                break;
            case EV_SYNC_DONE:
                handle_sync_done();
                break;
            case EV_NET: {
                net_status_t st = app_net_status();
                if (st.link == NET_LINK_ONLINE && last_link != NET_LINK_ONLINE) request_sync();
                last_link = st.link;
                if (s_provisioning && st.prov == NET_PROV_DONE && !s_prov_done_at) {
                    s_prov_done_at = now_ms();
                    app_audio_play(SOUND_MERGED);
                }
                ui_update(false);
                break;
            }
            case EV_CLAP:
                if (app_nav_poke(&s_nav, now_ms())) {
                    apply_backlight();
                    ui_update(false);
                }
                break;
            case EV_CONSOLE:
                if (ev.a == APP_CONSOLE_STATUS) {
                    print_status();
                } else if (ev.a >= APP_CONSOLE_KEY_UP) {
                    static const struct { bsp_btn_t btn; bsp_btn_ev_t ev; } K[] = {
                        { BSP_BTN_UP, BSP_BTN_CLICK }, { BSP_BTN_DOWN, BSP_BTN_CLICK },
                        { BSP_BTN_OK, BSP_BTN_CLICK }, { BSP_BTN_OK, BSP_BTN_DOUBLE },
                        { BSP_BTN_OK, BSP_BTN_LONG },
                    };
                    int i = ev.a - APP_CONSOLE_KEY_UP;
                    handle_key(K[i].btn, K[i].ev);
                } else {
                    request_sync();
                }
                break;
            }
        }

        int64_t t = now_ms();
        if (s_provisioning) {
            app_nav_poke(&s_nav, t);
            if (s_prov_done_at && t - s_prov_done_at > PROV_DONE_LINGER_MS) provisioning_exit();
        }
        int64_t dim_ms, off_ms;
        app_nav_idle_delays(s_model.prefs.screen_off_min, CONFIG_ASTRA_DIM_SECONDS * 1000LL, &dim_ms, &off_ms);
        if (app_nav_tick(&s_nav, t, dim_ms, off_ms)) {
            apply_backlight();
            refresh_model();
            if (bsp_lvgl_lock(500)) {
                ui_set_idle(&s_model);
                bsp_lvgl_unlock();
            }
        }
        if (t - last_batt >= BATTERY_PERIOD_MS) {
            last_batt = t;
            s_model.battery_soc = bsp_battery_soc();
            s_model.battery_mv = bsp_battery_mv();
        }
        // 每秒刷新一次(时钟、相对时间);关屏时不必重绘。
        if (s_ui_ready && t - last_ui >= 1000 && s_nav.idle != APP_IDLE_OFF) {
            last_ui = t;
            ui_update(false);
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Astra Monitor 启动,组织 %s", CONFIG_ASTRA_GH_ORG);
    s_events = xQueueCreate(16, sizeof(app_event_t));
    s_handoff_lock = xSemaphoreCreateMutex();
    if (!s_events || !s_handoff_lock) return;

    bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示初始化失败,应用无法继续");
        return;
    }
    bsp_display_backlight(0);
    app_settings_init();
    app_settings_day_baseline(&s_day_key, &s_day_stars);
    app_nav_init(&s_nav, now_ms());
    app_nav_set_items(&s_nav, APP_PAGE_SYSTEM, UI_MENU_COUNT);
    s_model.battery_soc = -1;
    s_model.last_result = GH_FETCH_OK;   // 尚未失败过
    s_snap.rate_remaining = -1;

    bool audio_ok = bsp_audio_init() == ESP_OK;
    if (bsp_battery_init() != ESP_OK) ESP_LOGW(TAG, "电量计不可用");
    esp_err_t err = app_net_init(on_net);
    if (err != ESP_OK) ESP_LOGE(TAG, "Wi-Fi 初始化失败: %s", esp_err_to_name(err));

    if (bsp_lvgl_lock(1000)) {
        ui_init();
        refresh_model();
        ui_build(&s_model);
        bsp_lvgl_unlock();
    }
    s_ui_ready = true;
    apply_backlight();

    err = app_audio_start(audio_ok, on_clap);
    if (err != ESP_OK) ESP_LOGW(TAG, "音频不可用: %s", esp_err_to_name(err));
    app_audio_set_enabled(app_settings_prefs().sound_on);
    app_audio_play(SOUND_BOOT);

    if (xTaskCreate(app_task, "app", 6144, NULL, 5, NULL) != pdPASS ||
        xTaskCreate(sync_task, "sync", 8192, NULL, 4, &s_sync_task) != pdPASS) {
        ESP_LOGE(TAG, "任务创建失败");
        return;
    }
    if (bsp_button_init(on_key, NULL) != ESP_OK) ESP_LOGE(TAG, "按键初始化失败");
    app_console_start(on_console);
    ESP_LOGI(TAG, "就绪:audio=%d 空闲堆 %u", audio_ok, (unsigned)esp_get_free_heap_size());
}
