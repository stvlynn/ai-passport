// main/ui.h —— Astra Monitor 的界面:像素风 + AstraFoundry Sunbeam 配色 + 看板娘 Lynn。
//
// 所有 ui_* 函数只能在持有 bsp_lvgl_lock() 时调用。界面只读 ui_model_t,
// 不保存对模型的长期引用(snapshot 指针除外,调用方保证其在下次 ui_* 调用前有效)。
#pragma once

#include "app_nav.h"
#include "app_net.h"
#include "app_settings.h"
#include "gh_fetch.h"
#include "gh_model.h"
#include "mic_sense.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    UI_POSE_IDLE = 0,
    UI_POSE_HAPPY,
    UI_POSE_SURPRISED,
    UI_POSE_REVIEW,
    UI_POSE_SLEEP,
    UI_POSE_CONFUSED,
} ui_pose_t;

// 设备页菜单项(顺序即光标顺序)。
typedef enum {
    UI_MENU_PROVISION = 0,
    UI_MENU_SYNC,
    UI_MENU_SOUND,
    UI_MENU_CLAP,
    UI_MENU_BRIGHTNESS,
    UI_MENU_SCREEN_OFF,
    UI_MENU_FORGET_WIFI,
    UI_MENU_COUNT,
} ui_menu_t;

typedef struct {
    const gh_snapshot_t *snap;
    const app_nav_t *nav;
    const char *org;
    net_status_t net;
    app_prefs_t prefs;
    int32_t today_stars;          // 今日新增星标;<0 = 未知
    int battery_soc;              // -1 = 不可用
    int battery_mv;
    bool token_set;
    bool syncing;
    bool provisioning;            // 显示配网页而不是 nav->page
    gh_fetch_result_t last_result;
    char last_detail[48];
    int64_t now;                  // UTC 秒;0 = 尚未校时
    int64_t next_sync_in_s;       // 距下次自动同步的秒数
    uint8_t idle;                 // app_idle_t
} ui_model_t;

void ui_init(void);

// 按 m->provisioning / m->nav->page 重建整页。
void ui_build(const ui_model_t *m);

// 只更新当前页的动态内容(数字、列表、光标、状态栏)。
void ui_refresh(const ui_model_t *m);

// 新动态提示:顶部滑入卡片 + 看板娘做出反应。item 可为 NULL(只提示 star_delta)。
void ui_notify(const ui_model_t *m, const gh_feed_item_t *item, int32_t star_delta);

// 操作反馈:顶部滑入一张只有文字的提示卡。warn=true 用红色图标,看板娘短暂困惑。
void ui_message(const ui_model_t *m, bool warn, const char *head, const char *sub);

// ---- Lynn 舞台页(动作同时是 agent 工具,见 docs/lynn-tools.json)----
// 播放动作;id 为 NULL 时随机一个互动。line 非 NULL 时作为台词。返回 false 表示 id 不存在。
bool ui_lynn_play(const char *id, const char *line);
// 改台词(NULL 恢复默认)。
void ui_lynn_say(const char *text);
// 当前动作 id 与台词,写入 buf。
void ui_lynn_status(char *buf, size_t cap);

// 用户在总览页"戳"了看板娘:她会招手/跳起来,打瞌睡时会被惊醒。
// 当前页没有看板娘时返回 false。
bool ui_poke(const ui_model_t *m);

// 屏幕阶段变化(唤醒/变暗/关屏)时调用:关屏后停止眨眼等动画。
void ui_set_idle(const ui_model_t *m);

// 声音互动:pop 非阻塞地取一帧麦克风特征(NULL = 不听),sfx 播放一下提示音(可为 NULL)。
// Lynn 页的舞台定时器每拍取完所有帧交给 lynn_ear,让 Lynn 对吹气、哼唱、拍手等做出反应。
typedef bool (*ui_mic_pop_t)(mic_frame_t *f);
typedef void (*ui_sfx_t)(void);
void ui_lynn_set_ears(ui_mic_pop_t pop, ui_sfx_t sfx);
// 当前声音互动状态(串口 "lynn ear"),写入 buf。
void ui_lynn_ear_status(char *buf, size_t cap);

// Persistent, scrolling connector reply; independent from idle action captions.
void ui_lynn_bot_text(const char *text);
// Show the next three-line page of the bubble (wraps). False if it fits one page.
bool ui_lynn_page_next(void);
