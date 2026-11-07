// main/mic_sense.c —— 见 mic_sense.h。
#include "mic_sense.h"

#include <string.h>

#define Q4(db)            ((int32_t)((db) * 16))
#define FLOOR_MIN         Q4(12)
#define ONSET_JUMP        Q4(10)    // 子块比包络高这么多 → 起音
#define ONSET_LEVEL       Q4(18)    // 且至少高于底噪这么多
#define ENV_DECAY         Q4(1.5)   // 每 5 ms 子块包络下降
#define CLAP_DROP         Q4(9)     // 起音后 ~100 ms 至少回落这么多才算"短促声"
#define CLAP_CONFIRM      4         // 起音后第几个块做判定
#define CLAP_REFRACTORY   120
#define VOICED_MIN_Q10    560       // 归一化自相关峰 ≥ 0.55 视为有音高
#define DEC_N             (MIC_RATE_HZ / 100)   // 2 倍抽取后 8 kHz 的样本数(20 ms)
#define LAG_MIN           16        // 500 Hz
#define LAG_MAX           80        // 100 Hz

// 10*log10(p) = 3.0103 * log2(p);log2 用"最高位 + 线性尾数"近似,误差 < 0.3 dB。
int32_t mic_db_q4(uint64_t p) {
    if (p == 0) return 0;
    int msb = 63;
    while (!(p >> msb)) msb--;
    uint64_t frac = msb >= 8 ? (p - (1ull << msb)) >> (msb - 8) : (p - (1ull << msb)) << (8 - msb);
    int64_t log2_q8 = (int64_t)msb * 256 + (int64_t)frac;
    return (int32_t)(log2_q8 * 3083 / 16384);
}

void mic_sense_init(mic_sense_t *m) {
    memset(m, 0, sizeof(*m));
}

static const int32_t GOERTZEL_Q14[MIC_BANDS] = { 32541, 31863, 29197, 19261, 0, -26510 };  // 150..3200 Hz @ 8 kHz

static uint64_t goertzel(const int32_t *x, int n, int32_t coeff) {
    int64_t s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        int64_t s0 = x[i] + ((coeff * s1) >> 14) - s2;
        s2 = s1;
        s1 = s0;
    }
    int64_t p = s1 * s1 + s2 * s2 - ((coeff * s1) >> 14) * s2;
    return p < 0 ? 0 : (uint64_t)p / (uint64_t)n;
}

static void floor_track(int32_t *floor, int32_t db, uint32_t t_ms) {
    if (db < *floor) {
        *floor -= (*floor - db + 3) / 4;           // 安静下来时快速跟随
    } else if ((t_ms / 20) % 4 == 0) {
        *floor += 1;                               // 持续有声时缓慢上调(约 0.8 dB/s)
    }
    if (*floor < FLOOR_MIN) *floor = FLOOR_MIN;
}

static uint8_t clamp_u8(int32_t v, int32_t hi) {
    return (uint8_t)(v < 0 ? 0 : v > hi ? hi : v);
}

void mic_sense_feed(mic_sense_t *m, const int16_t *pcm, size_t n, uint32_t now_ms, mic_frame_t *out) {
    memset(out, 0, sizeof(*out));
    out->t_ms = now_ms;
    if (n < MIC_SUBS * 4) return;

    // 整体能量与 5 ms 子块能量(起音检测)
    uint64_t total = 0;
    size_t sub = n / MIC_SUBS;
    int32_t sub_db[MIC_SUBS];
    for (int s = 0; s < MIC_SUBS; s++) {
        uint64_t e = 0;
        for (size_t i = s * sub; i < (s + 1) * sub; i++) e += (uint64_t)((int32_t)pcm[i] * pcm[i]);
        total += e;
        sub_db[s] = mic_db_q4(e / sub);
    }
    int32_t db = mic_db_q4(total / (MIC_SUBS * sub));
    if (!m->primed) {
        m->primed = true;
        m->floor_q4 = db < FLOOR_MIN ? FLOOR_MIN : db;
        m->env_q4 = db;
        for (int b = 0; b < MIC_BANDS; b++) m->band_floor_q4[b] = FLOOR_MIN;
    }
    floor_track(&m->floor_q4, db, now_ms);
    int32_t rel = db - m->floor_q4;
    out->level = clamp_u8(rel / 16, 60);

    // 短促声:陡峭起音 → 等 ~80 ms 看是否回落。回落了才算拍手/敲击,没回落就是说话、哼唱的开头。
    if (m->pending) {
        for (int s = 0; s < MIC_SUBS; s++) {
            if (sub_db[s] > m->pending_peak_q4) m->pending_peak_q4 = sub_db[s];
        }
        if (++m->pending_chunks >= CLAP_CONFIRM) {
            m->pending = false;
            if (db <= m->pending_peak_q4 - CLAP_DROP) {
                out->clap = true;
                out->clap_ms = m->pending_ms;
            }
        }
    }
    for (int s = 0; s < MIC_SUBS; s++) {
        uint32_t t = now_ms - (uint32_t)(MIC_SUBS - 1 - s) * 5;
        if (sub_db[s] > m->env_q4) {
            bool fresh = (int32_t)(t - m->refractory_until) >= 0;
            if (fresh && !m->pending && sub_db[s] - m->env_q4 >= ONSET_JUMP &&
                sub_db[s] - m->floor_q4 >= ONSET_LEVEL) {
                m->pending = true;
                m->pending_ms = t;
                m->pending_peak_q4 = sub_db[s];
                m->pending_chunks = 0;
                m->refractory_until = t + CLAP_REFRACTORY;
            }
            m->env_q4 = sub_db[s];
        } else {
            m->env_q4 -= ENV_DECAY;
            if (m->env_q4 < sub_db[s]) m->env_q4 = sub_db[s];
        }
    }

    // 2 倍抽取到 8 kHz(相邻平均 = 粗低通),顺便分出低频/高频两路做"频谱倾斜"。
    int32_t *d = m->dec;
    int dn = (int)(n / 2 < DEC_N ? n / 2 : DEC_N);
    int64_t mean = 0;
    for (int i = 0; i < dn; i++) {
        d[i] = ((int32_t)pcm[2 * i] + pcm[2 * i + 1]) >> 1;
        mean += d[i];
    }
    mean /= dn;
    uint64_t lo = 0, hi = 0;
    int32_t lp = 0;
    for (int i = 0; i < dn; i++) {
        d[i] -= (int32_t)mean;
        lp += (d[i] - lp) / 8;                         // 一阶低通,~170 Hz
        int32_t hp = i ? d[i] - d[i - 1] : 0;          // 一阶差分,偏高频
        lo += (uint64_t)((int64_t)lp * lp);
        hi += (uint64_t)((int64_t)hp * hp);
    }
    // 白噪声约 0 dB;风噪(低频)明显为负;"嘘——"、嘶声明显为正。
    int32_t tilt = (mic_db_q4(hi / dn + 1) - mic_db_q4(lo / dn + 1)) / 16 - 14;
    out->tilt = (int8_t)(tilt < -60 ? -60 : tilt > 60 ? 60 : tilt);

    // 频谱柱:6 个 Goertzel 单频点,各自有自适应底噪。
    // 最高的一段顶到 15 时整体下移,保留频谱形状而不是全部顶满。
    int32_t rel_b[MIC_BANDS], top = 0;
    for (int b = 0; b < MIC_BANDS; b++) {
        int32_t bdb = mic_db_q4(goertzel(d, dn, GOERTZEL_Q14[b]) + 1);
        floor_track(&m->band_floor_q4[b], bdb, now_ms);
        rel_b[b] = (bdb - m->band_floor_q4[b] - Q4(4)) * 2 / Q4(5);
        if (rel_b[b] > top) top = rel_b[b];
    }
    for (int b = 0; b < MIC_BANDS; b++) out->band[b] = clamp_u8(rel_b[b] - (top > 15 ? top - 15 : 0), 15);

    // 音高:对差分(去掉低频风噪)后的信号做归一化自相关,取区间内第一个足够高的局部峰。
    if (out->level < 6) return;
    int32_t *y = m->hp;
    for (int i = 0; i < dn; i++) y[i] = (i ? d[i] - d[i - 1] * 7 / 8 : 0) >> 2;
    int64_t e0 = 0;
    for (int i = 0; i < dn; i++) e0 += (int64_t)y[i] * y[i];
    if (e0 <= 0 || dn <= LAG_MAX + 2) return;
    int32_t *r = m->ac;
    int32_t best = 0;
    for (int lag = LAG_MIN - 1; lag <= LAG_MAX + 1; lag++) {
        int64_t acc = 0;
        for (int i = 0; i + lag < dn; i++) acc += (int64_t)y[i] * y[i + lag];
        int64_t v = acc * 1024 * dn / ((int64_t)(dn - lag) * e0);
        r[lag] = (int32_t)(v > 2048 ? 2048 : v < -2048 ? -2048 : v);
    }
    for (int lag = LAG_MIN; lag <= LAG_MAX; lag++) {
        if (r[lag] > r[lag - 1] && r[lag] >= r[lag + 1] && r[lag] > best) best = r[lag];
    }
    if (best < VOICED_MIN_Q10) return;
    for (int lag = LAG_MIN; lag <= LAG_MAX; lag++) {
        if (r[lag] > r[lag - 1] && r[lag] >= r[lag + 1] && r[lag] * 8 >= best * 7) {
            out->voiced = true;
            out->pitch_hz = (uint16_t)(8000 / lag);
            break;
        }
    }
}
