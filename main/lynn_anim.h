// main/lynn_anim.h —— 看板娘 Lynn 的逐帧动画与待机日程(纯逻辑,host 可测)。
//
// 只决定"此刻显示哪一帧";帧到图片的映射在 ui.c。规则:
//   1. 反应(新动态、被戳)优先,播完一次后回到日常。
//   2. 屏幕关闭 → 睡觉;屏幕变暗 → 打瞌睡;正在同步 → 敲电脑。
//   3. 其它心情(开心、审阅、困惑…)循环播放对应的小动作。
//   4. 心情为"待机"时按日程轮换:站着(眨眼、左右张望)→ 看书 / 敲电脑 / 喝茶 /
//      听音乐 / 伸懒腰 / 打瞌睡 → 站着…
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LF_IDLE = 0, LF_BLINK, LF_IDLE_L, LF_IDLE_R,
    LF_HAPPY, LF_HAPPY_2, LF_HOP,
    LF_SURPRISED, LF_SURPRISED_2,
    LF_REVIEW, LF_REVIEW_2, LF_REVIEW_3,
    LF_SLEEP, LF_SLEEP_2, LF_SLEEP_3,
    LF_CONFUSED, LF_CONFUSED_2, LF_CONFUSED_3,
    LF_READ_1, LF_READ_2, LF_READ_3, LF_READ_4,
    LF_TYPE_1, LF_TYPE_2, LF_TYPE_3, LF_TYPE_4,
    LF_DOZE_1, LF_DOZE_2, LF_DOZE_3, LF_DOZE_4,
    LF_WAVE_1, LF_WAVE_2, LF_WAVE_3,
    LF_TEA_1, LF_TEA_2, LF_TEA_3,
    LF_STRETCH_1, LF_STRETCH_2, LF_STRETCH_3,
    LF_MUSIC_1, LF_MUSIC_2, LF_MUSIC_3,
    LF_PARTY_1, LF_PARTY_2, LF_PARTY_3,
    LF_INSPECT_1, LF_INSPECT_2,
    LF_COUNT,
} lynn_frame_t;

typedef enum {
    // 心情(由界面状态决定的基础姿态)
    LA_IDLE = 0, LA_HAPPY, LA_SURPRISED, LA_REVIEW, LA_CONFUSED, LA_SLEEP,
    // 日常活动
    LA_READ, LA_TYPE, LA_DOZE, LA_TEA, LA_STRETCH, LA_MUSIC,
    // 一次性反应
    LA_WAVE, LA_WAKE, LA_HOP, LA_PARTY, LA_INSPECT,
    LA_COUNT,
} lynn_act_t;

typedef struct {
    lynn_act_t routine;         // 待机日程中的当前活动(LA_IDLE 或某个日常活动)
    uint32_t routine_start;
    uint32_t routine_until;
    lynn_act_t reaction;        // 正在播放的反应;LA_COUNT = 无
    uint32_t reaction_start;
    uint32_t reaction_until;
    uint8_t pokes;              // 被戳次数,用于轮换反应
    uint32_t rng;
} lynn_anim_t;

typedef struct {
    lynn_act_t mood;            // LA_IDLE..LA_SLEEP
    bool syncing;
    bool dim;                   // 屏幕变暗
} lynn_input_t;

void lynn_anim_init(lynn_anim_t *a, uint32_t now_ms, uint32_t seed);

// 返回此刻应显示的帧。now_ms 单调递增(允许 32 位回绕)。
lynn_frame_t lynn_anim_frame(lynn_anim_t *a, const lynn_input_t *in, uint32_t now_ms);

// 播放一次反应片段;hold_ms 为片段播完后停在末帧的额外时长。
void lynn_anim_react(lynn_anim_t *a, lynn_act_t act, uint32_t now_ms, uint32_t hold_ms);

// 被用户"戳了一下":打瞌睡时惊醒并招手,否则在招手/跳起来之间轮换。返回所播放的片段。
lynn_act_t lynn_anim_poke(lynn_anim_t *a, const lynn_input_t *in, uint32_t now_ms);

// 片段总时长(ms);循环片段返回一轮的时长。
uint32_t lynn_clip_ms(lynn_act_t act);
