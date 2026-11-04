// main/sound_synth.c —— 见 sound_synth.h。音效旋律为本项目原创的短琶音。
#include "sound_synth.h"

#include <string.h>

// 音高(Hz),取整即可:方波在小喇叭上听不出 1Hz 误差。
enum { C5 = 523, E5 = 659, G5 = 784, A5 = 880, C6 = 1047, D6 = 1175, E6 = 1319,
       G6 = 1568, A6 = 1760, C7 = 2093 };

static const sound_note_t BOOT[]   = { {C5, 70}, {E5, 70}, {G5, 70}, {C6, 160} };
static const sound_note_t STAR[]   = { {E6, 50}, {G6, 50}, {C7, 50}, {0, 30}, {C7, 120} };
static const sound_note_t ISSUE[]  = { {A5, 90}, {0, 30}, {E5, 140} };
static const sound_note_t PR[]     = { {G5, 70}, {C6, 70}, {E6, 150} };
static const sound_note_t MERGED[] = { {C6, 60}, {E6, 60}, {G6, 60}, {A6, 60}, {C7, 220} };
static const sound_note_t TICK[]   = { {D6, 12} };
static const sound_note_t ERROR_[] = { {E5, 90}, {0, 40}, {C5, 200} };

typedef struct {
    const sound_note_t *notes;
    uint8_t count;
} sound_def_t;

#define DEF(x) { x, (uint8_t)(sizeof(x) / sizeof(x[0])) }
static const sound_def_t DEFS[SOUND_COUNT] = {
    [SOUND_NONE] = { NULL, 0 },
    [SOUND_BOOT] = DEF(BOOT),
    [SOUND_STAR] = DEF(STAR),
    [SOUND_ISSUE] = DEF(ISSUE),
    [SOUND_PR] = DEF(PR),
    [SOUND_MERGED] = DEF(MERGED),
    [SOUND_TICK] = DEF(TICK),
    [SOUND_ERROR] = DEF(ERROR_),
};

void sound_player_start(sound_player_t *p, sound_id_t id, int16_t amplitude) {
    memset(p, 0, sizeof(*p));
    if (id <= SOUND_NONE || id >= SOUND_COUNT) return;
    p->notes = DEFS[id].notes;
    p->count = DEFS[id].count;
    p->amplitude = amplitude < 0 ? 0 : amplitude;
}

uint32_t sound_duration_ms(sound_id_t id) {
    if (id <= SOUND_NONE || id >= SOUND_COUNT) return 0;
    uint32_t ms = 0;
    for (uint8_t i = 0; i < DEFS[id].count; i++) ms += DEFS[id].notes[i].ms;
    return ms;
}

size_t sound_player_render(sound_player_t *p, int16_t *out, size_t max) {
    size_t n = 0;
    while (n < max && p->note < p->count) {
        const sound_note_t *note = &p->notes[p->note];
        uint32_t total = (uint32_t)note->ms * SOUND_RATE_HZ / 1000;
        if (p->sample_in_note >= total) {
            p->note++;
            p->sample_in_note = 0;
            continue;
        }
        int16_t v = 0;
        if (note->hz) {
            // 线性起音 2ms、释音 8ms:去掉音符边界的"咔哒"声。
            uint32_t attack = SOUND_RATE_HZ * 2 / 1000;
            uint32_t release = SOUND_RATE_HZ * 8 / 1000;
            uint32_t left = total - p->sample_in_note;
            int32_t amp = p->amplitude;
            if (p->sample_in_note < attack) amp = amp * (int32_t)p->sample_in_note / (int32_t)attack;
            if (left < release) amp = amp * (int32_t)left / (int32_t)release;
            // 25% 占空比方波:比 50% 更"芯片音"。
            v = (int16_t)(p->phase < 0x40000000u ? amp : -amp);
            p->phase += (uint32_t)(((uint64_t)note->hz << 32) / SOUND_RATE_HZ);
        }
        out[n++] = v;
        p->sample_in_note++;
    }
    return n;
}

void clap_detector_init(clap_detector_t *d, uint16_t threshold) {
    memset(d, 0, sizeof(*d));
    d->threshold = threshold;
    d->window_ms = 700;
    d->refractory_ms = 120;
    d->last_clap_ms = -1;
}

bool clap_detector_feed(clap_detector_t *d, const int16_t *pcm, size_t n, int64_t now_ms) {
    int32_t peak = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t a = pcm[i] < 0 ? -(int32_t)pcm[i] : pcm[i];
        if (a > peak) peak = a;
    }
    if (now_ms < d->quiet_until_ms || peak < d->threshold) return false;
    d->quiet_until_ms = now_ms + (int64_t)d->refractory_ms;
    if (d->last_clap_ms >= 0 && now_ms - d->last_clap_ms <= (int64_t)d->window_ms) {
        d->last_clap_ms = -1;
        return true;
    }
    d->last_clap_ms = now_ms;
    return false;
}
