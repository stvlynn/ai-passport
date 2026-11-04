// tests/test_lynn_anim.c —— 看板娘逐帧动画:片段时序、优先级、待机日程与戳一下。
#include "lynn_anim.h"

#include <assert.h>
#include <stdio.h>

static bool is_routine_frame(lynn_frame_t f) {
    return (f >= LF_IDLE && f <= LF_IDLE_R) || (f >= LF_READ_1 && f <= LF_DOZE_4) ||
           (f >= LF_TEA_1 && f <= LF_MUSIC_3);
}

int main(void) {
    lynn_anim_t a;
    lynn_input_t in = { .mood = LA_IDLE };
    lynn_anim_init(&a, 1000, 42);

    // 待机开头站着,2.4 秒时眨眼。
    assert(lynn_anim_frame(&a, &in, 1000) == LF_IDLE);
    assert(lynn_anim_frame(&a, &in, 1000 + 2400) == LF_BLINK);
    assert(lynn_anim_frame(&a, &in, 1000 + 2540) == LF_IDLE);

    // 日程会轮换到所有日常活动,并且回到站立;每帧都属于日常片段。
    int seen[LF_COUNT] = { 0 };
    for (uint32_t t = 1000; t < 1000 + 1800000; t += 50) {
        lynn_frame_t f = lynn_anim_frame(&a, &in, t);
        assert(is_routine_frame(f));
        seen[f]++;
    }
    for (int f = 0; f < LF_COUNT; f++) {
        if (is_routine_frame((lynn_frame_t)f) && !seen[f]) {
            printf("routine frame %d never shown\n", f);
            assert(0);
        }
    }

    // 优先级:关屏睡觉 > 变暗打瞌睡 > 同步敲电脑 > 心情静帧。
    uint32_t t = 1900000;
    lynn_input_t s = { .mood = LA_SLEEP, .syncing = true, .dim = true };
    assert(lynn_anim_frame(&a, &s, t) == LF_SLEEP);
    s.mood = LA_IDLE;
    lynn_frame_t f = lynn_anim_frame(&a, &s, t);
    assert(f >= LF_DOZE_1 && f <= LF_DOZE_3);
    s.dim = false;
    f = lynn_anim_frame(&a, &s, t);
    assert(f >= LF_TYPE_1 && f <= LF_TYPE_3);
    s.syncing = false;
    // 心情循环播放自己的小动作。
    s.mood = LA_CONFUSED;
    int conf = 0;
    for (uint32_t d = 0; d < 3000; d += 50) {
        f = lynn_anim_frame(&a, &s, t + d);
        assert(f >= LF_CONFUSED && f <= LF_CONFUSED_3);
        conf |= 1 << (f - LF_CONFUSED);
    }
    assert(conf == 7);
    s.mood = LA_REVIEW;
    f = lynn_anim_frame(&a, &s, t);
    assert(f >= LF_REVIEW && f <= LF_REVIEW_3);

    // 反应优先于一切心情,播完停在末帧再保持 hold,然后回到站立。
    lynn_anim_react(&a, LA_HOP, t, 1000);
    assert(lynn_anim_frame(&a, &s, t) == LF_HAPPY);
    assert(lynn_anim_frame(&a, &s, t + 150) == LF_HOP);
    uint32_t end = t + lynn_clip_ms(LA_HOP);
    assert(lynn_anim_frame(&a, &s, end + 500) == LF_HAPPY);      // 末帧保持

    // 庆祝与放大镜反应。
    lynn_anim_react(&a, LA_PARTY, end + 2000, 0);
    assert(lynn_anim_frame(&a, &s, end + 2000) == LF_PARTY_1);
    assert(lynn_anim_frame(&a, &s, end + 2000 + lynn_clip_ms(LA_PARTY) - 1) == LF_HAPPY_2);
    lynn_anim_react(&a, LA_INSPECT, end + 2000, 0);
    assert(lynn_anim_frame(&a, &s, end + 2000 + 700) == LF_INSPECT_2);
    lynn_anim_react(&a, LA_HOP, t, 1000);
    assert(lynn_anim_frame(&a, &in, end + 1001) == LF_IDLE);     // 结束 → 站立
    assert(a.reaction == LA_COUNT);

    // 戳一下:平时在招手/跳起来之间轮换;变暗打瞌睡时先惊醒再招手。
    t = end + 5000;
    assert(lynn_anim_poke(&a, &in, t) == LA_WAVE);
    assert(lynn_anim_frame(&a, &in, t) == LF_WAVE_1);
    assert(lynn_anim_frame(&a, &in, t + 220) == LF_WAVE_2);
    t += 10000;
    assert(lynn_anim_poke(&a, &in, t) == LA_HOP);
    t += 10000;
    lynn_input_t dim = { .mood = LA_IDLE, .dim = true };
    assert(lynn_anim_poke(&a, &dim, t) == LA_WAKE);
    assert(lynn_anim_frame(&a, &dim, t) == LF_SURPRISED);
    assert(lynn_anim_frame(&a, &dim, t + 400) == LF_SURPRISED_2);
    assert(lynn_anim_frame(&a, &dim, t + 700) == LF_WAVE_1);

    // 32 位时间回绕不影响反应计时。
    lynn_anim_init(&a, 0xFFFFFF00u, 7);
    lynn_anim_react(&a, LA_WAVE, 0xFFFFFF00u, 0);
    assert(lynn_anim_frame(&a, &in, 0x00000010u) == LF_WAVE_2);   // 已过 0x110=272 ms

    assert(lynn_clip_ms(LA_COUNT) == 0 && lynn_clip_ms(LA_WAVE) == 2000);
    for (int c = 0; c < LA_COUNT; c++) assert(lynn_clip_ms((lynn_act_t)c) > 0);
    puts("test_lynn_anim: PASS");
    return 0;
}
