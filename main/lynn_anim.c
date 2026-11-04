// main/lynn_anim.c —— 见 lynn_anim.h。
#include "lynn_anim.h"

#include <string.h>

typedef struct {
    lynn_frame_t frame;
    uint16_t ms;
} key_t;

typedef struct {
    const key_t *keys;
    uint8_t count;
} clip_t;

#define CLIP(x) { x, (uint8_t)(sizeof(x) / sizeof(x[0])) }

// 站着:眨眼、左右张望(间隔不等,避免机械感)。
static const key_t IDLE[] = { {LF_IDLE, 2400}, {LF_BLINK, 140}, {LF_IDLE, 1500}, {LF_IDLE_L, 900},
                              {LF_IDLE, 600}, {LF_BLINK, 120}, {LF_IDLE, 1800}, {LF_IDLE_R, 900},
                              {LF_IDLE, 400}, {LF_BLINK, 120} };
// 看书:读 → 翻页(抬手、页翻起)→ 读到有趣处微笑 → 再翻一页。
static const key_t READ[] = { {LF_READ_1, 2000}, {LF_READ_2, 220}, {LF_READ_3, 260}, {LF_READ_1, 1200},
                              {LF_READ_4, 1400}, {LF_READ_1, 900}, {LF_READ_2, 200}, {LF_READ_3, 240} };
// 敲电脑:两帧交替打字 → 抬头看你 → 再打一串 → 潇洒地按回车。
static const key_t TYPE[] = { {LF_TYPE_1, 160}, {LF_TYPE_2, 160}, {LF_TYPE_1, 160}, {LF_TYPE_2, 160},
                              {LF_TYPE_1, 160}, {LF_TYPE_2, 160}, {LF_TYPE_3, 900}, {LF_TYPE_1, 160},
                              {LF_TYPE_2, 160}, {LF_TYPE_1, 160}, {LF_TYPE_4, 600} };
// 打瞌睡:眯眼 → 点头 → 鼻涕泡 → 猛地惊醒。
static const key_t DOZE[] = { {LF_DOZE_1, 1400}, {LF_DOZE_2, 1600}, {LF_DOZE_4, 1400}, {LF_DOZE_3, 300},
                              {LF_DOZE_1, 900} };
static const key_t TEA[] = { {LF_TEA_1, 1600}, {LF_TEA_2, 1400}, {LF_TEA_3, 1200}, {LF_TEA_1, 1000} };
static const key_t STRETCH[] = { {LF_STRETCH_1, 900}, {LF_STRETCH_2, 1200}, {LF_STRETCH_1, 500},
                                 {LF_STRETCH_3, 1400} };
static const key_t MUSIC[] = { {LF_MUSIC_1, 400}, {LF_MUSIC_2, 400}, {LF_MUSIC_1, 400}, {LF_MUSIC_2, 400},
                               {LF_MUSIC_3, 800} };
static const key_t WAVE[] = { {LF_WAVE_1, 200}, {LF_WAVE_2, 200}, {LF_WAVE_3, 200}, {LF_WAVE_2, 200},
                              {LF_WAVE_1, 200}, {LF_WAVE_2, 200}, {LF_WAVE_3, 200}, {LF_HAPPY, 600} };
static const key_t WAKE[] = { {LF_SURPRISED, 400}, {LF_SURPRISED_2, 300}, {LF_WAVE_1, 220}, {LF_WAVE_2, 220},
                              {LF_WAVE_3, 220}, {LF_WAVE_2, 220} };
static const key_t HOP[] = { {LF_HAPPY, 150}, {LF_HOP, 150}, {LF_HAPPY, 150}, {LF_HOP, 150},
                             {LF_HAPPY, 150}, {LF_HOP, 150}, {LF_HAPPY, 150}, {LF_HOP, 150}, {LF_HAPPY, 400} };
// 星标里程碑 / PR 合并:拉响礼花,彩纸落下。
static const key_t PARTY[] = { {LF_PARTY_1, 400}, {LF_PARTY_2, 300}, {LF_PARTY_3, 600}, {LF_PARTY_2, 300},
                               {LF_PARTY_3, 900}, {LF_HAPPY_2, 500} };
// 新 issue:拿放大镜左右端详。
static const key_t INSPECT[] = { {LF_INSPECT_1, 700}, {LF_INSPECT_2, 700}, {LF_INSPECT_1, 700},
                                 {LF_INSPECT_2, 900} };
// 心情:循环播放的小动作。
static const key_t HAPPY[] = { {LF_HAPPY, 600}, {LF_HAPPY_2, 600} };
static const key_t SURPRISED[] = { {LF_SURPRISED, 500}, {LF_SURPRISED_2, 500} };
static const key_t REVIEW[] = { {LF_REVIEW, 1400}, {LF_REVIEW_2, 800}, {LF_REVIEW, 900}, {LF_REVIEW_3, 1200} };
static const key_t CONFUSED[] = { {LF_CONFUSED, 900}, {LF_CONFUSED_2, 900}, {LF_CONFUSED_3, 700} };
static const key_t SLEEP[] = { {LF_SLEEP, 1200}, {LF_SLEEP_2, 1200}, {LF_SLEEP_3, 1400} };

static const clip_t CLIPS[LA_COUNT] = {
    [LA_IDLE] = CLIP(IDLE), [LA_HAPPY] = CLIP(HAPPY), [LA_SURPRISED] = CLIP(SURPRISED),
    [LA_REVIEW] = CLIP(REVIEW), [LA_CONFUSED] = CLIP(CONFUSED), [LA_SLEEP] = CLIP(SLEEP),
    [LA_READ] = CLIP(READ), [LA_TYPE] = CLIP(TYPE), [LA_DOZE] = CLIP(DOZE),
    [LA_TEA] = CLIP(TEA), [LA_STRETCH] = CLIP(STRETCH), [LA_MUSIC] = CLIP(MUSIC),
    [LA_WAVE] = CLIP(WAVE), [LA_WAKE] = CLIP(WAKE), [LA_HOP] = CLIP(HOP),
    [LA_PARTY] = CLIP(PARTY), [LA_INSPECT] = CLIP(INSPECT),
};

uint32_t lynn_clip_ms(lynn_act_t act) {
    if (act >= LA_COUNT) return 0;
    uint32_t ms = 0;
    for (uint8_t i = 0; i < CLIPS[act].count; i++) ms += CLIPS[act].keys[i].ms;
    return ms;
}

// 循环播放:elapsed 对一轮时长取模后查表。
static lynn_frame_t clip_frame(lynn_act_t act, uint32_t elapsed, bool loop) {
    const clip_t *c = &CLIPS[act];
    uint32_t total = lynn_clip_ms(act);
    if (loop) {
        elapsed %= total;
    } else if (elapsed >= total) {
        return c->keys[c->count - 1].frame;   // 一次性片段播完后停在末帧
    }
    for (uint8_t i = 0; i < c->count; i++) {
        if (elapsed < c->keys[i].ms) return c->keys[i].frame;
        elapsed -= c->keys[i].ms;
    }
    return c->keys[c->count - 1].frame;
}

static uint32_t rnd(lynn_anim_t *a, uint32_t lo, uint32_t hi) {
    // xorshift32:够用的伪随机,决定活动种类与时长。
    uint32_t x = a->rng ? a->rng : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    a->rng = x;
    return lo + x % (hi - lo + 1);
}

static bool before(uint32_t now, uint32_t t) {
    return (int32_t)(now - t) < 0;
}

static void start_routine(lynn_anim_t *a, lynn_act_t act, uint32_t now) {
    a->routine = act;
    a->routine_start = now;
    switch (act) {
    case LA_READ: a->routine_until = now + rnd(a, 10000, 16000); break;
    case LA_TYPE: a->routine_until = now + rnd(a, 7000, 11000); break;
    case LA_DOZE: a->routine_until = now + rnd(a, 9000, 13000); break;
    case LA_TEA:  a->routine_until = now + rnd(a, 9000, 13000); break;
    case LA_MUSIC: a->routine_until = now + rnd(a, 8000, 12000); break;
    case LA_STRETCH: a->routine_until = now + lynn_clip_ms(LA_STRETCH) + rnd(a, 0, 1500); break;
    default:      a->routine_until = now + rnd(a, 7000, 12000); break;
    }
}

void lynn_anim_init(lynn_anim_t *a, uint32_t now_ms, uint32_t seed) {
    memset(a, 0, sizeof(*a));
    a->rng = seed;
    a->reaction = LA_COUNT;
    start_routine(a, LA_IDLE, now_ms);
}

lynn_frame_t lynn_anim_frame(lynn_anim_t *a, const lynn_input_t *in, uint32_t now) {
    if (a->reaction != LA_COUNT) {
        if (before(now, a->reaction_until)) {
            return clip_frame(a->reaction, now - a->reaction_start, false);
        }
        a->reaction = LA_COUNT;
        start_routine(a, LA_IDLE, now);       // 反应结束后先站一会儿
    }
    if (in->mood == LA_SLEEP) return LF_SLEEP;
    if (in->dim) return clip_frame(LA_DOZE, now - a->routine_start, true);
    if (in->syncing) return clip_frame(LA_TYPE, now - a->routine_start, true);
    if (in->mood != LA_IDLE) return clip_frame(in->mood, now - a->routine_start, true);

    if (!before(now, a->routine_until)) {
        if (a->routine == LA_IDLE) {
            // 权重:看书 3、敲电脑 3、喝茶 2、听音乐 2、伸懒腰 1、打瞌睡 1。
            static const lynn_act_t PICK[12] = { LA_READ, LA_READ, LA_READ, LA_TYPE, LA_TYPE, LA_TYPE,
                                                 LA_TEA, LA_TEA, LA_MUSIC, LA_MUSIC, LA_STRETCH, LA_DOZE };
            start_routine(a, PICK[rnd(a, 0, 11)], now);
        } else {
            start_routine(a, LA_IDLE, now);
        }
    }
    return clip_frame(a->routine, now - a->routine_start, true);
}

void lynn_anim_react(lynn_anim_t *a, lynn_act_t act, uint32_t now, uint32_t hold_ms) {
    if (act >= LA_COUNT) return;
    a->reaction = act;
    a->reaction_start = now;
    a->reaction_until = now + lynn_clip_ms(act) + hold_ms;
}

lynn_act_t lynn_anim_poke(lynn_anim_t *a, const lynn_input_t *in, uint32_t now) {
    bool dozing = in->dim || (a->reaction == LA_COUNT && a->routine == LA_DOZE && in->mood == LA_IDLE);
    lynn_act_t act = dozing ? LA_WAKE : (a->pokes++ % 2 == 0 ? LA_WAVE : LA_HOP);
    lynn_anim_react(a, act, now, 0);
    return act;
}
