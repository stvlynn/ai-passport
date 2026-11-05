// main/lynn_stage_data.h —— 由 tools/gen_lynn_stage.py 生成,请勿手改。
#pragma once

#include <stdint.h>

#define LYNN_STAGE_W 80
#define LYNN_STAGE_H 76
#define LYNN_FX_NONE 0
#define LYNN_FX_HAND 1
#define LYNN_FX_POT 2
#define LYNN_FX_PLANE 3
#define LYNN_F_FLIP 1
#define LYNN_F_POUR 2
#define LYNN_F_LAUNCH 4
#define LYNN_F_PERK 8
#define LYNN_HAND_NONE (-128)
#define LYNN_COLOR_PAPER 219
#define LYNN_COLOR_SHADOW 220
#define LYNN_BASE_LINE "今天也在这里陪你。"

#define LYNN_SPR_HAND 0
#define LYNN_SPR_POT_DROOP 1
#define LYNN_SPR_POT_PERK 2
#define LYNN_SPR_DROP 3
#define LYNN_SPR_SPLASH 4
#define LYNN_SPR_PLANE 5
#define LYNN_SPR_POP 6
#define LYNN_SPR_EMOTE_HEART 7
#define LYNN_SPR_EMOTE_NOTE 8
#define LYNN_SPR_EMOTE_BANG 9
#define LYNN_SPR_EMOTE_QUESTION 10
#define LYNN_SPR_EMOTE_SWEAT 11
#define LYNN_SPR_EMOTE_ZZZ 12
#define LYNN_SPR_EMOTE_ANGER 13
#define LYNN_SPR_EMOTE_SPARKLE 14
#define LYNN_SPR_EMOTE_DOTS 15
#define LYNN_SPR_EMOTE_TEAR 16
#define LYNN_SPR_EMOTE_BASE 7

typedef struct { int8_t x, y; uint8_t w, h; uint32_t offset; } lynn_bmp_t;
typedef struct { uint8_t frame, ticks; int8_t lift, dx, hx, hy; uint8_t flags, emote; int8_t hand; } lynn_step_t;
typedef struct {
    const char *id, *name, *line, *desc;
    uint8_t kind, fx;      // kind: 0 = 待机活动, 1 = 互动
    uint16_t entry, n_entry, loop, n_loop, exit, n_exit;
    uint8_t loops_min, loops_max;
} lynn_action_t;

extern const uint16_t lynn_palette[];
extern const uint8_t lynn_pixels[];
extern const lynn_bmp_t lynn_frames[];
extern const lynn_bmp_t lynn_sprites[];
extern const lynn_step_t lynn_steps[];
extern const lynn_step_t lynn_base[];
extern const lynn_action_t lynn_actions[];
extern const uint8_t lynn_action_count, lynn_idle_count, lynn_head_rows;
extern const uint16_t lynn_frame_count, lynn_base_count;
extern const int8_t lynn_spout_x, lynn_spout_y, lynn_launch_x, lynn_launch_y;
extern const int8_t lynn_pot_x, lynn_pot_y, lynn_pot_rim;
