// main/sound_synth.h —— 8-bit 风格方波合成与拍手检测(纯逻辑,host 可测)。
//
// 合成器按"音符序列"逐块生成 16-bit 单声道 PCM;音频任务把块交给 bsp_audio_write。
// 拍手检测器对麦克风块做峰值包络判断,两次拍手落在窗口内即触发。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SOUND_RATE_HZ 16000

typedef enum {
    SOUND_NONE = 0,
    SOUND_BOOT,
    SOUND_STAR,
    SOUND_ISSUE,
    SOUND_PR,
    SOUND_MERGED,
    SOUND_TICK,      // 按键反馈
    SOUND_ERROR,
    SOUND_COUNT,
} sound_id_t;

typedef struct {
    uint16_t hz;     // 0 = 休止
    uint16_t ms;
} sound_note_t;

typedef struct {
    const sound_note_t *notes;
    uint8_t count;
    uint8_t note;            // 当前音符下标
    uint32_t sample_in_note; // 当前音符已生成的样本数
    uint32_t phase;          // 32 位相位累加器,满量程 = 一个周期
    int16_t amplitude;
} sound_player_t;

// 开始播放某个音效。amplitude 为方波峰值(建议 3000~9000,避免小喇叭破音)。
void sound_player_start(sound_player_t *p, sound_id_t id, int16_t amplitude);

// 生成最多 max 个样本,返回实际写入数;返回 0 表示播放结束。
size_t sound_player_render(sound_player_t *p, int16_t *out, size_t max);

// 该音效的总时长(ms),用于测试与调度。
uint32_t sound_duration_ms(sound_id_t id);

typedef struct {
    uint16_t threshold;      // 峰值门限(绝对值)
    uint32_t window_ms;      // 两次拍手的最大间隔
    uint32_t refractory_ms;  // 一次拍手后忽略的时长(回声/尾音)
    int64_t last_clap_ms;    // 上一次单次拍手的时刻;<0 = 无
    int64_t quiet_until_ms;
} clap_detector_t;

void clap_detector_init(clap_detector_t *d, uint16_t threshold);

// 喂入一块麦克风样本(now_ms 为该块结束时刻)。检测到"两次拍手"返回 true。
bool clap_detector_feed(clap_detector_t *d, const int16_t *pcm, size_t n, int64_t now_ms);
