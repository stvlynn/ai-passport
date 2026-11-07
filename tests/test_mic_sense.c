// tests/test_mic_sense.c —— 声音特征:dB 近似、音高、吹气/嘘声的频谱倾斜、拍手确认与说话起音的区分。
#include "mic_sense.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static uint32_t rng = 1;
static double noise(void) {
    rng = rng * 1103515245u + 12345u;
    return ((rng >> 8) & 0xFFFF) / 32768.0 - 1.0;
}
static int16_t clip(double v) {
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

typedef enum { SIG_QUIET, SIG_HUM, SIG_HISS, SIG_WIND, SIG_CLAP, SIG_VOWEL_START } sig_kind_t;

static mic_sense_t m;
static uint32_t now;
static double lp, hp_prev;
static long k;

static void chunk(sig_kind_t sig, double hz, int index, mic_frame_t *f) {
    int16_t b[320];
    for (int i = 0; i < 320; i++, k++) {
        double v = noise() * 30;
        switch (sig) {
        case SIG_QUIET: break;
        case SIG_HUM: v += 3000 * sin(2 * M_PI * hz * k / 16000.0); break;
        case SIG_VOWEL_START: v += 9000 * sin(2 * M_PI * hz * k / 16000.0); break;
        case SIG_HISS: { double w = noise() * 3000; v += w - hp_prev * 0.9; hp_prev = w; } break;
        case SIG_WIND: lp += (noise() * 20000 - lp) * 0.02; v += lp * 2; break;
        case SIG_CLAP:
            if (index == 0 && i >= 100) v += noise() * 12000 * exp(-(i - 100) / 300.0);
            if (index == 1) v += noise() * 12000 * exp(-(i + 220) / 300.0);
            break;
        }
        b[i] = clip(v);
    }
    now += 20;
    mic_sense_feed(&m, b, 320, now, f);
}

// 喂 n 块并返回最后一帧;统计 clap。
static int feed(sig_kind_t sig, double hz, int n, mic_frame_t *last) {
    int claps = 0;
    for (int i = 0; i < n; i++) {
        chunk(sig, hz, i, last);
        claps += last->clap;
    }
    return claps;
}

int main(void) {
    // dB:功率每翻 10 倍 +10 dB
    assert(mic_db_q4(0) == 0);
    int d1 = mic_db_q4(1000), d2 = mic_db_q4(10000), d3 = mic_db_q4(1u << 30);
    assert(d2 - d1 >= 16 * 9 && d2 - d1 <= 16 * 11);
    assert(d3 >= 16 * 89 && d3 <= 16 * 91);

    mic_sense_init(&m);
    mic_frame_t f;
    assert(feed(SIG_QUIET, 0, 50, &f) == 0);
    assert(f.level <= 2 && !f.voiced);

    // 哼唱:有音高,频率准确;低频为主 → 倾斜为负
    const double tones[] = { 140, 220, 330, 440 };
    for (int i = 0; i < 4; i++) {
        feed(SIG_HUM, tones[i], 20, &f);
        assert(f.voiced && f.level >= 25);
        assert(fabs(f.pitch_hz - tones[i]) <= tones[i] * 0.04);
        feed(SIG_QUIET, 0, 30, &f);
    }

    // "嘘——":无音高、偏高频;吹气(风噪):无音高、明显偏低频
    feed(SIG_HISS, 0, 20, &f);
    assert(!f.voiced && f.level >= 20 && f.tilt >= 7);
    feed(SIG_QUIET, 0, 30, &f);
    feed(SIG_WIND, 0, 20, &f);
    assert(!f.voiced && f.level >= 20 && f.tilt <= -10);
    feed(SIG_QUIET, 0, 30, &f);

    // 拍手:陡峭起音、迅速衰减 → 确认一次;时刻在拍手所在的块附近
    for (int n = 0; n < 3; n++) {
        uint32_t start = now;
        int claps = 0;
        uint32_t at = 0;
        for (int i = 0; i < 15; i++) {
            chunk(SIG_CLAP, 0, i, &f);
            if (f.clap) {
                claps++;
                at = f.clap_ms;
            }
        }
        assert(claps == 1);
        assert(at > start && at <= start + 40);
    }

    // 突然开始的大声元音:起音同样陡峭,但持续不衰减 → 不算拍手
    feed(SIG_QUIET, 0, 30, &f);
    assert(feed(SIG_VOWEL_START, 260, 25, &f) == 0);
    assert(f.voiced);

    // 频谱柱:大声时保留形状(最高段 15,不是全部顶满)
    feed(SIG_HUM, 220, 10, &f);
    int top = 0, full = 0;
    for (int b = 0; b < MIC_BANDS; b++) {
        if (f.band[b] > top) top = f.band[b];
        full += f.band[b] == 15;
    }
    assert(top == 15 && full < MIC_BANDS);

    printf("test_mic_sense: OK\n");
    return 0;
}
