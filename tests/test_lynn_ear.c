// tests/test_lynn_ear.c —— Lynn 的耳朵:用合成的特征帧走一遍每种声音玩法,检查模式切换、一次性请求、
// 姿势接管、星星小游戏、蛋糕蜡烛,以及道具绘制不越界。
#include "lynn_ear.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static lynn_ear_t e;
static lynn_ear_view_t view;
static uint32_t clock_ms = 1000;
static uint32_t tick_acc;
static bool drowsy;
static uint16_t buf[LYNN_STAGE_W * LYNN_STAGE_H + 1];

// 收集到的一次性请求
static char actions[16][16];
static char lines[16][64];
static int n_actions, n_lines, n_sfx;
static int max_lift, min_dx;
static bool saw_wind, saw_eq, saw_cake, saw_star;

static void collect(void) {
    lynn_ear_cue_t c;
    while (lynn_ear_take_cue(&e, &c)) {
        if (c.action && n_actions < 16) snprintf(actions[n_actions++], 16, "%s", c.action);
        if (c.line && n_lines < 16) snprintf(lines[n_lines++], 64, "%s", c.line);
        n_sfx += c.sfx;
    }
}

static void reset_log(void) {
    n_actions = n_lines = n_sfx = 0;
    max_lift = 0;
    min_dx = 0;
    saw_wind = saw_eq = saw_cake = saw_star = false;
}

static bool did(const char *action) {
    for (int i = 0; i < n_actions; i++) if (strcmp(actions[i], action) == 0) return true;
    return false;
}

static bool said(const char *part) {
    for (int i = 0; i < n_lines; i++) if (strstr(lines[i], part)) return true;
    return false;
}

static void tick(void) {
    lynn_ear_tick(&e, drowsy, &view);
    collect();
    if (view.live) {
        if (view.pose.lift > max_lift) max_lift = view.pose.lift;
        if (view.pose.dx < min_dx) min_dx = view.pose.dx;
        assert(view.pose.frame < lynn_frame_count);
    }
    saw_wind |= view.wind > 0;
    saw_eq |= view.eq;
    saw_cake |= view.cake;
    saw_star |= view.star;
    buf[LYNN_STAGE_W * LYNN_STAGE_H] = 0xBEEF;
    lynn_rect_t box = { LYNN_STAGE_W, LYNN_STAGE_H, -1, -1 };
    lynn_ear_render(&view, e.ticks, buf, &box);
    assert(buf[LYNN_STAGE_W * LYNN_STAGE_H] == 0xBEEF);
    if (box.x1 <= box.x2) assert(box.x1 >= 0 && box.x2 < LYNN_STAGE_W && box.y1 >= 0 && box.y2 < LYNN_STAGE_H);
}

// 以 20 ms 一帧喂入 ms 时长的同一种声音,期间按 24 fps 推进舞台。
static void sound(uint32_t ms, uint8_t level, bool voiced, uint16_t pitch, int8_t tilt) {
    for (uint32_t t = 0; t < ms; t += 20) {
        clock_ms += 20;
        mic_frame_t f = { .t_ms = clock_ms, .level = level, .voiced = voiced, .pitch_hz = voiced ? pitch : 0,
                          .tilt = tilt };
        for (int b = 0; b < MIC_BANDS; b++) f.band[b] = (uint8_t)(level / 4 + b) % 16;
        lynn_ear_feed(&e, &f);
        collect();
        tick_acc += 20 * 24;
        while (tick_acc >= 1000) {
            tick_acc -= 1000;
            tick();
        }
    }
}

static void quiet(uint32_t ms) {
    sound(ms, 0, false, 0, 0);
}

static void clap_at(void) {
    clock_ms += 20;
    mic_frame_t f = { .t_ms = clock_ms, .level = 2, .clap = true, .clap_ms = clock_ms - 60 };
    lynn_ear_feed(&e, &f);
    collect();
}

static void claps(int n, const uint32_t *gaps) {
    for (int i = 0; i < n; i++) {
        clap_at();
        if (i < n - 1) quiet(gaps[i] - 20);
    }
}

int main(void) {
    lynn_ear_init(&e, 42);
    quiet(1000);
    assert(e.mode == EAR_NONE && !view.live && view.vu == 0);

    // ---- 吹气:被推开,风停后走回原位 ----
    reset_log();
    sound(600, 28, false, 0, -15);
    assert(e.mode == EAR_BLOW && view.live && view.wind > 0 && min_dx < 0);
    quiet(2000);
    assert(e.mode == EAR_NONE && e.dx == 0);

    // ---- 一直吹:被吹出舞台,停下后从左边走回来,叹气 ----
    reset_log();
    sound(2600, 30, false, 0, -18);
    assert(e.mode == EAR_GONE && said("吹走"));
    quiet(2500);
    assert(e.mode == EAR_RETURN || e.mode == EAR_NONE);
    quiet(4000);
    assert(e.mode == EAR_NONE && did("sigh") && said("差点") && min_dx <= -90);

    // ---- 哼"啊——":飘起来,音越高飘越高;停下就落地 ----
    reset_log();
    sound(1500, 20, true, 220, -10);
    assert(e.mode == EAR_FLOAT && view.live);
    int low = max_lift;
    sound(1500, 20, true, 330, -10);            // 高 7 个半音
    assert(max_lift > low && max_lift <= 10);
    quiet(1500);
    assert(e.mode == EAR_NONE && view.pose.lift == 0);

    // ---- 星星小游戏:一个"会唱歌的玩家"照着星星的高度调音高,能接住好几颗 ----
    reset_log();
    lynn_ear_init(&e, 7);
    quiet(500);
    sound(600, 14, true, 200, -10);
    assert(e.mode == EAR_FLOAT);
    static const uint16_t SEMI[12] = { 200, 212, 224, 238, 252, 267, 283, 300, 318, 336, 356, 378 };
    for (int i = 0; i < 300; i++) {
        int want = e.star ? e.star_h - 2 : 0;     // target = 2 + 半音差
        if (want < 0) want = 0;
        if (want > 11) want = 11;
        sound(40, 14, true, SEMI[want], -10);
    }
    assert(saw_star && e.score >= 3 && n_sfx >= 3 && e.mode == EAR_FLOAT);   // 只哼不唱:不出蛋糕
    quiet(1500);
    assert(e.mode == EAR_NONE && said("星星"));

    // ---- 唱一段有起伏、有字的歌:小小演唱会;唱完她鼓掌(不会端出蛋糕:生日一年才一次)----
    reset_log();
    lynn_ear_init(&e, 9);
    quiet(500);
    static const uint16_t SONG[] = { 262, 262, 294, 262, 349, 330, 262, 262, 294, 262, 392, 349 };
    for (int i = 0; i < 12 && e.mode != EAR_SING; i++) {
        sound(340, 18, true, SONG[i], -10);       // 每个字之间有短短的停顿
        if (e.mode != EAR_SING) quiet(80);
    }
    assert(e.mode == EAR_SING && !saw_cake);
    sound(1000, 18, true, 300, -10);
    assert(e.mode == EAR_SING && saw_eq && view.live);
    quiet(2000);
    assert(e.mode == EAR_NONE && did("clap") && said("好听") && !saw_cake);

    // ---- 生日蛋糕只由 agent/串口端出;吹灭三根蜡烛 → 撒花 ----
    reset_log();
    lynn_ear_cake(&e);
    quiet(300);
    assert(e.mode == EAR_CAKE && said("生日快乐") && saw_cake && e.candles == 7);
    sound(400, 30, false, 0, -15);
    assert(e.mode == EAR_CAKE && e.candles != 7 && e.candles != 0);
    sound(900, 30, false, 0, -15);
    assert(e.candles == 0);
    quiet(2000);
    assert(e.mode == EAR_NONE && did("party") && said("许愿"));

    // ---- 拍一下:转头看 ----
    reset_log();
    lynn_ear_init(&e, 11);
    quiet(500);
    claps(1, NULL);
    quiet(800);
    assert(e.mode == EAR_LOOK || e.mode == EAR_NONE);
    quiet(1000);
    assert(e.mode == EAR_NONE);

    // ---- 拍两下:她照着节奏拍回两下(带音效)----
    reset_log();
    static const uint32_t TWO[] = { 300 };
    claps(2, TWO);
    quiet(800);
    assert(e.mode == EAR_ECHO && e.echo_n == 2);
    quiet(2000);
    assert(e.mode == EAR_NONE && n_sfx == 2);

    // ---- 暗号"短-短-长":撒花 ----
    reset_log();
    static const uint32_t CODE[] = { 260, 560 };
    claps(3, CODE);
    quiet(1600);
    assert(did("party") && said("暗号"));

    // ---- 均匀拍手:跟着节拍跳舞;停下来鞠躬道谢 ----
    reset_log();
    static const uint32_t BEAT[] = { 500, 500, 500, 500, 500, 500 };
    claps(7, BEAT);
    assert(e.mode == EAR_GROOVE && e.beat_ms >= 450 && e.beat_ms <= 550);
    quiet(200);
    assert(view.live && saw_eq);
    quiet(2000);
    assert(e.mode == EAR_NONE && did("bow"));

    // ---- "嘘——":竖手指,然后睡一会儿 ----
    reset_log();
    sound(700, 20, false, 0, 12);
    assert(e.mode == EAR_SHH && view.pose.frame == LYNN_FR_WINK_1);
    quiet(600);
    assert(e.mode == EAR_NONE && did("sleep"));

    // ---- 小声说话:凑近听,然后眨眼 ----
    reset_log();
    sound(1200, 9, true, 180, -8);
    assert(e.mode == EAR_WHISPER && view.pose.dx > 0);
    quiet(800);
    assert(e.mode == EAR_NONE && did("wink"));

    // ---- 长时间吵闹:捂脸;安静下来叹气 ----
    reset_log();
    sound(3600, 35, false, 0, 4);
    assert(e.mode == EAR_NOISY);
    quiet(1000);
    assert(e.mode == EAR_NONE && did("sigh"));

    // ---- 持续的音乐(没有明显鼓点):戴耳机摇摆,两侧出现频谱 ----
    reset_log();
    sound(5600, 12, false, 0, 4);
    assert(e.mode == EAR_SWAY && saw_eq);
    quiet(600);
    assert(e.mode == EAR_NONE);

    // ---- 一直安静:自己打瞌睡(只一次);打瞌睡时拍手把她吓一跳 ----
    reset_log();
    quiet(46000);
    assert(did("doze") && n_actions == 1);
    quiet(10000);
    assert(n_actions == 1);
    drowsy = true;
    quiet(100);                                 // 舞台 tick 把"正在打瞌睡"告诉耳朵
    claps(1, NULL);
    quiet(100);
    assert(did("surprised"));
    drowsy = false;

    // ---- 麦克风暂停时清掉接管 ----
    reset_log();
    sound(1000, 20, true, 220, -10);
    assert(e.mode == EAR_FLOAT);
    lynn_ear_reset(&e);
    tick();
    assert(e.mode == EAR_NONE && !view.live);

    // ---- 串口/agent 直接端出蛋糕:麦克风暂停也不收走,没人吹就 40 秒后自己收起 ----
    reset_log();
    lynn_ear_cake(&e);
    quiet(200);
    assert(e.mode == EAR_CAKE && view.cake);
    lynn_ear_reset(&e);
    tick();
    assert(e.mode == EAR_CAKE && view.cake && e.candles == 7);
    for (int i = 0; i < 24 * 41; i++) tick();
    assert(e.mode == EAR_NONE && !view.cake && said("收起来"));

    printf("test_lynn_ear: OK\n");
    return 0;
}
