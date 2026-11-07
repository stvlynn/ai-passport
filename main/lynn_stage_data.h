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

#define LYNN_EMOTE_HEART 1
#define LYNN_EMOTE_NOTE 2
#define LYNN_EMOTE_BANG 3
#define LYNN_EMOTE_QUESTION 4
#define LYNN_EMOTE_SWEAT 5
#define LYNN_EMOTE_ZZZ 6
#define LYNN_EMOTE_ANGER 7
#define LYNN_EMOTE_SPARKLE 8
#define LYNN_EMOTE_DOTS 9
#define LYNN_EMOTE_TEAR 10

#define LYNN_FR_READ_1 0
#define LYNN_FR_READ_2 1
#define LYNN_FR_READ_3 2
#define LYNN_FR_READ_4 3
#define LYNN_FR_IDLE_BLINK 4
#define LYNN_FR_TYPE_1 5
#define LYNN_FR_TYPE_2 6
#define LYNN_FR_TYPE_3 7
#define LYNN_FR_TYPE_4 8
#define LYNN_FR_TEA_1 9
#define LYNN_FR_TEA_2 10
#define LYNN_FR_TEA_3 11
#define LYNN_FR_MUSIC_1 12
#define LYNN_FR_MUSIC_2 13
#define LYNN_FR_MUSIC_3 14
#define LYNN_FR_STRETCH_1 15
#define LYNN_FR_STRETCH_2 16
#define LYNN_FR_STRETCH_3 17
#define LYNN_FR_DOZE_1 18
#define LYNN_FR_DOZE_2 19
#define LYNN_FR_DOZE_4 20
#define LYNN_FR_DOZE_3 21
#define LYNN_FR_WATER_1 22
#define LYNN_FR_WATER_2 23
#define LYNN_FR_WATER_3 24
#define LYNN_FR_WATER_4 25
#define LYNN_FR_SWEEP_1 26
#define LYNN_FR_SWEEP_2 27
#define LYNN_FR_IDLE 28
#define LYNN_FR_PLANE_1 29
#define LYNN_FR_PLANE_2 30
#define LYNN_FR_PLANE_3 31
#define LYNN_FR_DOODLE_1 32
#define LYNN_FR_DOODLE_2 33
#define LYNN_FR_DOODLE_3 34
#define LYNN_FR_PAT_1 35
#define LYNN_FR_PAT_2 36
#define LYNN_FR_PAT_3 37
#define LYNN_FR_PAT_4 38
#define LYNN_FR_WAVE_1 39
#define LYNN_FR_WAVE_2 40
#define LYNN_FR_WAVE_3 41
#define LYNN_FR_HAPPY 42
#define LYNN_FR_HAPPY_HOP 43
#define LYNN_FR_PARTY_1 44
#define LYNN_FR_PARTY_2 45
#define LYNN_FR_PARTY_3 46
#define LYNN_FR_HAPPY_2 47
#define LYNN_FR_INSPECT_1 48
#define LYNN_FR_INSPECT_2 49
#define LYNN_FR_SURPRISED 50
#define LYNN_FR_SURPRISED_2 51
#define LYNN_FR_CONFUSED 52
#define LYNN_FR_CONFUSED_2 53
#define LYNN_FR_CONFUSED_3 54
#define LYNN_FR_REVIEW 55
#define LYNN_FR_REVIEW_2 56
#define LYNN_FR_REVIEW_3 57
#define LYNN_FR_SLEEP 58
#define LYNN_FR_SLEEP_2 59
#define LYNN_FR_SLEEP_3 60
#define LYNN_FR_SHY_1 61
#define LYNN_FR_SHY_2 62
#define LYNN_FR_POUT_1 63
#define LYNN_FR_POUT_2 64
#define LYNN_FR_CRY_1 65
#define LYNN_FR_CRY_2 66
#define LYNN_FR_LAUGH_1 67
#define LYNN_FR_LAUGH_2 68
#define LYNN_FR_BOW_2 69
#define LYNN_FR_BOW_1 70
#define LYNN_FR_HEART_1 71
#define LYNN_FR_HEART_2 72
#define LYNN_FR_THUMBS_1 73
#define LYNN_FR_THUMBS_2 74
#define LYNN_FR_PEACE_1 75
#define LYNN_FR_PEACE_2 76
#define LYNN_FR_SALUTE_1 77
#define LYNN_FR_CLAP_1 78
#define LYNN_FR_CLAP_2 79
#define LYNN_FR_SHRUG_1 80
#define LYNN_FR_FACEPALM_1 81
#define LYNN_FR_POINT_1 82
#define LYNN_FR_SPIN_SIDE 83
#define LYNN_FR_SPIN_BACK 84
#define LYNN_FR_DANCE_1 85
#define LYNN_FR_DANCE_2 86
#define LYNN_FR_DANCE_3 87
#define LYNN_FR_SCARED_1 88
#define LYNN_FR_SNEEZE_1 89
#define LYNN_FR_SNEEZE_2 90
#define LYNN_FR_SNACK_1 91
#define LYNN_FR_SNACK_2 92
#define LYNN_FR_WINK_1 93
#define LYNN_FR_SIGH_1 94
#define LYNN_FR_IDLE_2 95
#define LYNN_FR_IDLE_3 96

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
