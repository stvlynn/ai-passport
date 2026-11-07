// main/lynn_stage.h —— Lynn 舞台页的动画引擎(纯 C,不依赖 LVGL/ESP-IDF,host 可测)。
//
// 动作数据(帧、时间轴、表情、道具)由 tools/gen_lynn_stage.py 从 tools/lynn_actions.py 生成到
// lynn_stage_data.c。引擎以 24 fps 走表:每一步把一张像素画保持若干 tick(像素动画的"一拍二/一拍三"),
// 手、水滴、纸飞机、表情气泡则每 tick 移动,保证运动顺滑。
//
// 画面合成到 LYNN_STAGE_W x LYNN_STAGE_H 的 RGB565 缓冲(逻辑像素),界面再整数 3 倍放大显示。
// 这些动作同时是 agent 的工具:lynn_stage_play() 按 id 播放,lynn_stage_say() 改台词。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lynn_stage_data.h"

#define LYNN_TICK_HZ 24
#define LYNN_MAX_DROPS 8
#define LYNN_LINE_MAX 96

typedef struct {
    int16_t action;            // 正在播放的动作下标;-1 = 基础待机(呼吸/眨眼/张望)
    uint8_t phase;             // 0 entry / 1 loop / 2 exit
    uint16_t step;             // 当前阶段内的步序号
    uint16_t step_tick;        // 当前步已经保持的 tick
    uint8_t loops_left;
    uint16_t base_step, base_tick;
    uint32_t ticks;            // 总 tick
    uint32_t next_activity;    // 基础待机到这个 tick 后开始一个待机活动
    int16_t last_activity;
    uint8_t emote, emote_age;  // 当前表情气泡及其显示时长
    int8_t drops[LYNN_MAX_DROPS][2];
    uint8_t splash;
    bool perk;                 // 浇花后花开了
    bool plane_on;
    int16_t plane_x, plane_y;
    uint32_t rng;
    uint32_t sig;              // 上一次渲染的画面签名,用于跳过无变化的重绘
    char line[LYNN_LINE_MAX];  // 当前台词(动作默认台词或 agent 指定)
    bool line_custom;
    bool live;                 // 姿势由外部(声音互动)逐 tick 接管,动作时间轴暂停
    lynn_step_t live_step;
} lynn_stage_t;

void lynn_stage_init(lynn_stage_t *s, uint32_t seed);

// 按 id 播放动作(待机活动或互动)。返回动作下标,找不到返回 -1。
int lynn_stage_play(lynn_stage_t *s, const char *id);
// 按下标播放。
void lynn_stage_play_index(lynn_stage_t *s, int index);
// 随机播放一个互动动作(不与上一个重复)。
int lynn_stage_play_random(lynn_stage_t *s);
// 设置台词;text 为 NULL 恢复动作默认台词。UTF-8,超长时在字符边界截断。
void lynn_stage_say(lynn_stage_t *s, const char *text);

// 前进一个 tick(1/24 秒)。返回 true 表示画面有变化,需要重绘。
bool lynn_stage_tick(lynn_stage_t *s);

typedef struct {
    int16_t x1, y1, x2, y2;     // 含端点;x1 > x2 表示空
} lynn_rect_t;

// 把当前画面合成到 buf(LYNN_STAGE_W * LYNN_STAGE_H 个 RGB565)。content 非 NULL 时写入
// 本帧所有非背景内容(人物、道具、气泡、阴影)的包围盒;界面用"上一帧 ∪ 本帧"的包围盒做局部重绘。
void lynn_stage_render(const lynn_stage_t *s, uint16_t *buf, lynn_rect_t *content);

const char *lynn_stage_line(const lynn_stage_t *s);
// 当前动作的 id 与中文名;基础待机返回 "idle" / "待机"。
const char *lynn_stage_action_id(const lynn_stage_t *s);
const char *lynn_stage_action_name(const lynn_stage_t *s);
bool lynn_stage_busy(const lynn_stage_t *s);   // 是否在播放互动动作

// 声音互动接管姿势:pose 非 NULL 时本 tick 起显示该姿势(动作时间轴暂停),NULL 恢复动作/待机。
void lynn_stage_live(lynn_stage_t *s, const lynn_step_t *pose);

int lynn_action_find(const char *id);
