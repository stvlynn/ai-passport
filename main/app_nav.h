// main/app_nav.h —— 页面切换、列表光标与息屏阶段的状态机(纯逻辑,host 可测)。
//
// 三键约定(全应用统一,每个键只有一种含义,避免翻页与选择冲突):
//   上 / 下     上一页 / 下一页(Lynn ⇄ 总览 ⇄ 动态 ⇄ 仓库 ⇄ 设备,首尾相接;开机停在总览)
//   确定 单击   页内选中下一项(总览=焦点仓库,列表=下一条,设备=下一个菜单项),循环
//   确定 双击   页内选中上一项
//   确定 长按   确认:设备页执行选中项;其它页立即同步
// 息屏(变暗/关屏)时的第一次按键只用于唤醒,不触发动作。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_PAGE_LYNN = 0,      // 看板娘舞台(总览左边)
    APP_PAGE_HOME,
    APP_PAGE_FEED,
    APP_PAGE_REPOS,
    APP_PAGE_SYSTEM,
    APP_PAGE_COUNT,
} app_page_t;

typedef enum {
    APP_IDLE_AWAKE = 0,
    APP_IDLE_DIM,
    APP_IDLE_OFF,
} app_idle_t;

typedef enum {
    APP_KEY_UP = 0,
    APP_KEY_DOWN,
    APP_KEY_OK,
    APP_KEY_OK_DOUBLE,
    APP_KEY_OK_LONG,
} app_key_t;

typedef enum {
    APP_NAV_NONE = 0,
    APP_NAV_WAKE,       // 只唤醒屏幕
    APP_NAV_PAGE,       // 页面变化,需重建页面
    APP_NAV_CURSOR,     // 光标变化,刷新页内高亮
    APP_NAV_ACTION,     // 执行当前页动作(页=page,项=cursor[page])
} app_nav_action_t;

typedef struct {
    app_page_t page;
    uint8_t cursor[APP_PAGE_COUNT];
    uint8_t items[APP_PAGE_COUNT];   // 各页可选项数;0 表示该页无光标
    app_idle_t idle;
    int64_t last_activity_ms;
} app_nav_t;

void app_nav_init(app_nav_t *n, int64_t now_ms);

// 更新某页的条目数,并把光标夹回有效范围。
void app_nav_set_items(app_nav_t *n, app_page_t page, uint8_t items);

app_nav_action_t app_nav_key(app_nav_t *n, app_key_t key, int64_t now_ms);

// 新动态到达等外部事件:重置空闲计时并唤醒。返回 true 表示之前处于息屏阶段。
bool app_nav_poke(app_nav_t *n, int64_t now_ms);

// 息屏时间选项(分钟),0 = 从不。设备页"息屏时间"按此顺序循环。
#define APP_SCREEN_OFF_OPTIONS { 1, 5, 10, 30, 0 }

// 下一个息屏时间选项;当前值不在列表中时回到第一项。
uint16_t app_nav_next_screen_off(uint16_t minutes);

// 由息屏时间推出变暗/关屏延时:变暗取息屏时间的一半,但不晚于 dim_max_ms;
// minutes == 0(从不)时只变暗、不关屏。
void app_nav_idle_delays(uint16_t minutes, int64_t dim_max_ms, int64_t *dim_ms, int64_t *off_ms);

// 周期调用。dim_ms/off_ms 为 0 表示关闭该阶段。返回 true 表示阶段发生变化。
bool app_nav_tick(app_nav_t *n, int64_t now_ms, int64_t dim_ms, int64_t off_ms);
