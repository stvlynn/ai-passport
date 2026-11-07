// main/mic_sense.h —— 麦克风"听觉":把 20 ms 的 PCM 块变成 Lynn 能理解的声音特征(纯逻辑,host 可测)。
//
// 只用整数运算(ESP32-C3 没有 FPU)。所有响度都相对于自适应的底噪,因此不依赖麦克风增益:
//   level   整体响度,比底噪高多少 dB
//   voiced  是否有音高(哼唱、说话的元音);pitch_hz 为基频
//   tilt    高频比低频响多少 dB:吹气(风噪)偏低频 → 负值;"嘘——"偏高频 → 正值
//   band[]  6 段频谱柱(150 Hz … 3.2 kHz),用于像素均衡器
//   clap_ms 一次"确认的短促声"(拍手、敲桌、鼓点)的起始时刻:起音陡峭,且 100 ms 内回落
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MIC_RATE_HZ 16000
#define MIC_BANDS 6
#define MIC_SUBS 4                // 每块再分 4 个 5 ms 子块,用于起音检测

typedef struct {
    uint32_t t_ms;                // 本块结束时刻
    uint8_t level;                // 0..60 dB,高于底噪
    bool voiced;
    uint16_t pitch_hz;            // 0 = 无音高
    int8_t tilt;                  // dB,高频段 - 低频段
    uint8_t band[MIC_BANDS];      // 0..15
    bool clap;                    // 本块确认了一次短促声
    uint32_t clap_ms;             // 该短促声的起音时刻
} mic_frame_t;

typedef struct {
    int32_t floor_q4;             // 底噪(dB*16)
    int32_t band_floor_q4[MIC_BANDS];
    int32_t env_q4;               // 子块包络(dB*16):快升慢降
    bool primed;
    // 待确认的起音
    bool pending;
    uint32_t pending_ms;
    int32_t pending_peak_q4;
    uint8_t pending_chunks;
    uint32_t refractory_until;
    // 运算缓冲放在这里而不是栈上:音频任务的栈很小。
    int32_t dec[MIC_RATE_HZ / 100], hp[MIC_RATE_HZ / 100], ac[84];
} mic_sense_t;

void mic_sense_init(mic_sense_t *m);

// 喂入一块 16 kHz 单声道样本(建议 320 个 = 20 ms),now_ms 为块结束时刻。
void mic_sense_feed(mic_sense_t *m, const int16_t *pcm, size_t n, uint32_t now_ms, mic_frame_t *out);

// 整数 dB:10*log10(power),power 为均方值。返回 dB*16。暴露出来供测试。
int32_t mic_db_q4(uint64_t power);
