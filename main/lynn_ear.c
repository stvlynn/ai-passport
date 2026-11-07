// main/lynn_ear.c —— 见 lynn_ear.h。
#include "lynn_ear.h"

#include <stdio.h>
#include <string.h>

// ---- 检测门限(dB 均相对底噪;时长为麦克风时钟 ms)----
#define BLOW_LEVEL      18
#define BLOW_TILT       2       // 风噪偏低频
#define BLOW_START      80
#define BLOW_GAP        160
#define BLOW_PER_CANDLE 300     // 吹蛋糕:每吹这么久灭一根
#define SHH_LEVEL       10
#define SHH_TILT        7       // "嘘——"偏高频
#define SHH_START       450
#define SHH_GAP         250
#define FLOAT_LEVEL     14
#define FLOAT_START     350
#define VOICE_GAP       200
#define WHISPER_MIN     5
#define WHISPER_MAX     13
#define WHISPER_START   800
#define WHISPER_GAP     300
#define SOUND_LEVEL     8
#define SOUND_GAP       200
#define SWAY_START      5000
#define NOISY_LEVEL     30
#define NOISY_START     3000
#define NOISY_GAP       400
#define QUIET_LEVEL     4
#define QUIET_DOZE      45000
#define SONG_START      4000
#define SONG_NOTES      4
#define SONG_SYLLS      6       // 至少这么多个字(元音间 40~300 ms 的停顿)
#define SONG_GAP        1200
#define CLAP_END_MIN    700
#define CLAP_END_MAX    1500

// ---- 舞台几何(与 lynn_stage.c 一致,逻辑像素)----
#define GROUND      (LYNN_STAGE_H - 3)
#define CELL_TOP    (GROUND - 60)
#define TPS         24                  // tick / 秒
#define MAX_LIFT    10                  // 再高头顶会碰到台词卡

static void set_mode(lynn_ear_t *e, lynn_ear_mode_t m) {
    e->mode = m;
    e->mode_tick = e->ticks;
}

static uint32_t rnd(lynn_ear_t *e, uint32_t lo, uint32_t hi) {
    uint32_t x = e->rng ? e->rng : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    e->rng = x;
    return lo + x % (hi - lo + 1);
}

static void cue(lynn_ear_t *e, const char *action, const char *line, uint8_t sfx) {
    if (e->cue_count == LYNN_EAR_CUES) {            // 满了丢最旧的
        e->cue_head = (uint8_t)((e->cue_head + 1) % LYNN_EAR_CUES);
        e->cue_count--;
    }
    uint8_t i = (uint8_t)((e->cue_head + e->cue_count) % LYNN_EAR_CUES);
    e->cues[i] = (lynn_ear_cue_t){ action, line, sfx };
    e->cue_count++;
}

bool lynn_ear_take_cue(lynn_ear_t *e, lynn_ear_cue_t *out) {
    if (!e->cue_count) return false;
    *out = e->cues[e->cue_head];
    e->cue_head = (uint8_t)((e->cue_head + 1) % LYNN_EAR_CUES);
    e->cue_count--;
    return true;
}

// 半音差:12 * log2(a / b),四舍五入。
static int log2_q8(uint32_t v) {
    if (!v) return 0;
    int msb = 31;
    while (!(v >> msb)) msb--;
    uint32_t frac = msb >= 8 ? (v - (1u << msb)) >> (msb - 8) : (v - (1u << msb)) << (8 - msb);
    return msb * 256 + (int)frac;
}

static int semis(uint16_t a, uint16_t b) {
    if (!a || !b) return 0;
    int d = (log2_q8(a) - log2_q8(b)) * 12;
    return d >= 0 ? (d + 128) / 256 : -((-d + 128) / 256);
}

// 带容差的持续计时:声音断开不超过 max_gap 仍算连续。
static void run_gap(uint32_t *run, uint32_t *gap, bool on, uint32_t dt, uint32_t max_gap) {
    if (on) {
        *run += dt + *gap;
        *gap = 0;
    } else if (*run) {
        *gap += dt;
        if (*gap > max_gap) *run = *gap = 0;
    }
}

void lynn_ear_init(lynn_ear_t *e, uint32_t seed) {
    memset(e, 0, sizeof(*e));
    e->rng = seed ? seed : 1;
}

void lynn_ear_reset(lynn_ear_t *e) {
    // 蛋糕(可能来自 agent)留在舞台上,到时自己收起;其它进行中的反应都结束。
    lynn_ear_t keep = *e;
    lynn_ear_init(e, keep.rng);
    e->ticks = keep.ticks;
    e->cake_linger = keep.cake_linger;
    if (keep.mode == EAR_CAKE) {
        e->mode = EAR_CAKE;
        e->mode_tick = keep.mode_tick;
        e->candles = keep.candles;
        e->dx = keep.dx;
    }
}

void lynn_ear_cake(lynn_ear_t *e) {
    set_mode(e, EAR_CAKE);
    e->candles = (1u << LYNN_EAR_CANDLES) - 1;
    e->blow_total = 0;
    e->smoke = 0;
    e->lift = 0;
    cue(e, NULL, "生日快乐!快吹蜡烛许个愿~", 0);
}

const char *lynn_ear_mode_name(lynn_ear_mode_t m) {
    static const char *const N[EAR_COUNT] = {
        "", "嗯?", "吹气", "被吹走了", "走回来", "飘浮", "落地", "嘘", "悄悄话",
        "回声拍手", "跟着节拍", "听音乐", "好吵", "生日蛋糕", "听你唱歌",
    };
    return m < EAR_COUNT ? N[m] : "";
}

// ---------------------------------------------------------------------------
// 拍手
// ---------------------------------------------------------------------------

static bool clap_mode(lynn_ear_mode_t m) {
    return m == EAR_NONE || m == EAR_LOOK || m == EAR_SWAY;
}

static void on_clap(lynn_ear_t *e, uint32_t t) {
    if (e->mode == EAR_GROOVE) {
        uint32_t iv = t - e->claps[0];
        if (iv * 100 >= (uint32_t)e->beat_ms * 65 && iv * 100 <= (uint32_t)e->beat_ms * 135) {
            e->beat_ms = (uint16_t)((e->beat_ms * 3u + iv) / 4);
            e->beat_tick = e->ticks;                  // 跟着你的拍子重新对齐
        }
        e->claps[0] = t;
        return;
    }
    if (!clap_mode(e->mode)) return;
    if (e->drowsy && e->clap_n == 0) {                // 打瞌睡时被拍手声惊醒
        cue(e, "surprised", NULL, 0);
        return;
    }
    if (e->clap_n == LYNN_EAR_MAX_CLAPS) {
        memmove(e->claps, e->claps + 1, sizeof(e->claps[0]) * (LYNN_EAR_MAX_CLAPS - 1));
        e->clap_n--;
    }
    e->claps[e->clap_n++] = t;
    if (e->clap_n >= 4) {
        // 最近三个间隔都接近平均值 → 均匀的节拍:跳舞
        uint32_t *c = &e->claps[e->clap_n - 4];
        uint32_t i1 = c[1] - c[0], i2 = c[2] - c[1], i3 = c[3] - c[2], avg = (i1 + i2 + i3) / 3;
        bool even = avg >= 250 && avg <= 1000;
        uint32_t iv[3] = { i1, i2, i3 };
        for (int k = 0; k < 3; k++) {
            if (iv[k] * 100 < avg * 78 || iv[k] * 100 > avg * 122) even = false;
        }
        if (even) {
            e->beat_ms = (uint16_t)avg;
            e->beat_tick = e->ticks;
            e->claps[0] = t;
            e->clap_n = 0;
            set_mode(e, EAR_GROOVE);
        }
    }
}

static void clap_burst_end(lynn_ear_t *e) {
    uint8_t n = e->clap_n;
    uint32_t i1 = n >= 2 ? e->claps[1] - e->claps[0] : 0;
    uint32_t i2 = n >= 3 ? e->claps[2] - e->claps[1] : 0;
    e->clap_n = 0;
    if (n == 1) {
        set_mode(e, EAR_LOOK);
    } else if (n == 3 && i1 <= 600 && i2 * 10 >= i1 * 17) {
        cue(e, "party", "暗号正确!撒花~", 0);    // "短-短-长"
        set_mode(e, EAR_NONE);
    } else if (n <= 5) {
        uint32_t avg = (e->claps[n - 1] - e->claps[0]) / (n - 1u);
        if (avg < 200) avg = 200;
        if (avg > 700) avg = 700;
        e->echo_n = n;
        e->echo_ticks = (uint16_t)(avg * TPS / 1000);
        set_mode(e, EAR_ECHO);
    } else {
        cue(e, "clap", "好热闹呀!", 0);
    }
}

// ---------------------------------------------------------------------------
// 特征帧
// ---------------------------------------------------------------------------

void lynn_ear_feed(lynn_ear_t *e, const mic_frame_t *f) {
    uint32_t dt = e->last.t_ms ? f->t_ms - e->last.t_ms : 20;
    if (dt == 0 || dt > 200) dt = 20;
    e->last = *f;
    e->clap_clock = f->t_ms;
    uint8_t lvl = f->level;

    bool blowing = !f->voiced && lvl >= BLOW_LEVEL && f->tilt <= BLOW_TILT;
    run_gap(&e->blow_run, &e->blow_gap, blowing, dt, BLOW_GAP);
    if (blowing) e->blow_level = (uint8_t)((e->blow_level * 3u + lvl) / 4);
    run_gap(&e->shh_run, &e->shh_gap, !f->voiced && lvl >= SHH_LEVEL && f->tilt >= SHH_TILT, dt, SHH_GAP);
    run_gap(&e->voiced_run, &e->voice_gap, f->voiced && lvl >= WHISPER_MIN, dt, VOICE_GAP);
    run_gap(&e->sound_run, &e->sound_gap, lvl >= SOUND_LEVEL, dt, SOUND_GAP);
    run_gap(&e->loud_run, &e->loud_gap, lvl >= NOISY_LEVEL && !f->voiced, dt, NOISY_GAP);

    // 稳定的音与旋律:同一个音 ±1 半音内持续算"一个音",跳 ≥2 半音算换音。
    if (f->voiced && lvl >= WHISPER_MIN) {
        int d = semis(f->pitch_hz, e->note_hz);
        if (e->note_hz && d >= -1 && d <= 1) {
            e->note_ms += dt;
        } else {
            if (e->note_hz && e->note_ms >= 150 && (d >= 2 || d <= -2) && e->song_notes < 255) e->song_notes++;
            e->note_hz = f->pitch_hz;
            e->note_ms = dt;
        }
        e->song_ms += dt;
        e->song_gap = 0;
        if (e->unvoiced_ms >= 40 && e->unvoiced_ms <= 300 && e->song_ms > dt && e->song_sylls < 255) e->song_sylls++;
        e->unvoiced_ms = 0;
    } else {
        e->unvoiced_ms += dt;
        if (!e->voiced_run) e->note_hz = 0, e->note_ms = 0;
        e->song_gap += dt;
        if (e->song_gap > SONG_GAP) e->song_ms = 0, e->song_notes = 0, e->song_sylls = 0;
    }

    // 悄悄话:小声的元音累计;变大声或停顿太久就清零。
    if (f->voiced && lvl >= WHISPER_MIN && lvl <= WHISPER_MAX) e->whisper_run += dt;
    else if (lvl > WHISPER_MAX + 3 || e->unvoiced_ms > WHISPER_GAP) e->whisper_run = 0;

    if (lvl < QUIET_LEVEL) {
        e->quiet_ms += dt;
    } else {
        e->quiet_ms = 0;
        e->dozed_for_quiet = false;
    }

    if (f->clap) on_clap(e, f->clap_ms);
    if (e->clap_n && clap_mode(e->mode)) {
        uint32_t last_iv = e->clap_n >= 2 ? e->claps[e->clap_n - 1] - e->claps[e->clap_n - 2] : 0;
        uint32_t end = last_iv * 22 / 10;
        if (end < CLAP_END_MIN) end = CLAP_END_MIN;
        if (end > CLAP_END_MAX) end = CLAP_END_MAX;
        if (f->t_ms - e->claps[e->clap_n - 1] > end) clap_burst_end(e);
    }

    // 唱了一段有起伏、有字的歌 → 小小演唱会。只哼不唱(调音高玩星星)不会触发。
    bool singing_ok = e->mode == EAR_NONE || e->mode == EAR_FLOAT || e->mode == EAR_LAND ||
                      e->mode == EAR_SWAY || e->mode == EAR_LOOK;
    if (singing_ok && e->song_ms >= SONG_START && e->song_notes >= SONG_NOTES && e->song_sylls >= SONG_SYLLS) {
        e->lift = 0;
        set_mode(e, EAR_SING);
        return;
    }

    switch (e->mode) {
    case EAR_ECHO:
    case EAR_LAND:
        return;
    case EAR_SING:
        if (!e->song_ms) {                      // 停唱超过 SONG_GAP:鼓掌
            cue(e, "clap", "唱得真好听!", 0);
            set_mode(e, EAR_NONE);
        }
        return;
    case EAR_CAKE:
        if (e->blow_run >= 60) {
            uint32_t before = e->blow_total / BLOW_PER_CANDLE;
            e->blow_total += dt;
            if (e->blow_total / BLOW_PER_CANDLE != before && e->candles) {
                for (int i = LYNN_EAR_CANDLES - 1; i >= 0; i--) {   // 从右往左灭(风从右边来)
                    if (e->candles & (1u << i)) {
                        e->candles &= (uint8_t)~(1u << i);
                        break;
                    }
                }
                e->smoke = 30;
            }
        }
        return;
    case EAR_GONE:
        return;
    case EAR_RETURN:
        if (e->blow_run >= BLOW_START) {
            e->blow_total = 0;
            set_mode(e, EAR_BLOW);              // 走回来的路上又被吹
        }
        return;
    case EAR_BLOW:
        if (e->blow_run) e->blow_total += dt;
        return;
    case EAR_FLOAT:
        if (!e->voiced_run) {
            e->vel = 0;
            e->landed_tick = 0;
            set_mode(e, EAR_LAND);
        } else if (f->voiced) {
            int target = 2 + (lvl - FLOAT_LEVEL) / 4 + semis(f->pitch_hz, e->float_ref_hz);
            e->float_target = (int8_t)(target < 1 ? 1 : target > MAX_LIFT ? MAX_LIFT : target);
        }
        return;
    case EAR_SHH:
        if (!e->shh_run) {
            cue(e, "sleep", "嘘……那我睡一会儿。", 0);
            set_mode(e, EAR_NONE);
        }
        return;
    case EAR_WHISPER:
        if (!e->whisper_run) {
            cue(e, "wink", NULL, 0);
            set_mode(e, EAR_NONE);
        }
        return;
    case EAR_GROOVE:
        if (f->t_ms - e->claps[0] > e->beat_ms * 5u / 2) {
            cue(e, "bow", "谢谢伴奏!", 0);
            set_mode(e, EAR_NONE);
        }
        return;
    case EAR_NOISY:
        if (!e->loud_run) {
            cue(e, "sigh", "终于安静了……", 0);
            set_mode(e, EAR_NONE);
        }
        return;
    case EAR_SWAY:
        if (!e->sound_run) {
            set_mode(e, EAR_NONE);
            return;
        }
        break;
    case EAR_NONE:
    case EAR_LOOK:
    case EAR_COUNT:
        break;
    }

    // 空闲(或只是在听音乐/转头)时,按优先级识别新的声音
    if (e->blow_run >= BLOW_START) {
        e->blow_total = 0;
        e->dx = 0;
        set_mode(e, EAR_BLOW);
    } else if (e->shh_run >= SHH_START) {
        set_mode(e, EAR_SHH);
    } else if (e->voiced_run >= FLOAT_START && lvl >= FLOAT_LEVEL && e->note_ms >= 250) {
        e->float_ref_hz = e->note_hz;
        e->float_target = 2;
        e->score = 0;
        e->star = false;
        e->star_next = e->ticks + TPS;
        set_mode(e, EAR_FLOAT);
    } else if (e->whisper_run >= WHISPER_START) {
        set_mode(e, EAR_WHISPER);
    } else if (e->loud_run >= NOISY_START) {
        set_mode(e, EAR_NOISY);
    } else if (e->mode == EAR_NONE && e->sound_run >= SWAY_START) {
        set_mode(e, EAR_SWAY);
    } else if (e->mode == EAR_NONE && e->quiet_ms >= QUIET_DOZE && !e->dozed_for_quiet && !e->drowsy) {
        e->dozed_for_quiet = true;
        cue(e, "doze", "好安静……我眯一会儿。", 0);
    } else if (e->drowsy && lvl >= 25 && e->sound_run <= 40 && !e->clap_n) {
        cue(e, "surprised", NULL, 0);           // 打瞌睡时突然一声响
    }
}

// ---------------------------------------------------------------------------
// 每个舞台 tick
// ---------------------------------------------------------------------------

static void pose(lynn_ear_view_t *v, uint8_t frame, int lift, int dx, uint8_t emote) {
    v->live = true;
    v->pose.frame = frame;
    v->pose.lift = (int8_t)lift;
    v->pose.dx = (int8_t)(dx < -120 ? -120 : dx > 120 ? 120 : dx);
    v->pose.emote = emote;
    v->pose.hand = LYNN_HAND_NONE;
    v->pose.ticks = 1;
}

static void approach(int16_t *v, int target) {
    if (*v < target) (*v)++;
    else if (*v > target) (*v)--;
}

void lynn_ear_tick(lynn_ear_t *e, bool drowsy, lynn_ear_view_t *v) {
    e->ticks++;
    e->drowsy = drowsy;
    memset(v, 0, sizeof(*v));
    uint32_t t = e->ticks - e->mode_tick;
    if (e->sparkle) e->sparkle--;
    if (e->smoke) e->smoke--;
    v->vu = e->last.level >= 4 ? (uint8_t)(e->last.level - 3 > 40 ? 40 : e->last.level - 3) : 0;
    const char *name = lynn_ear_mode_name(e->mode);
    v->caption = name[0] ? name : NULL;

    switch (e->mode) {
    case EAR_NONE:
        e->dx = 0;
        e->lift = 0;
        if (e->cake_linger) {
            e->cake_linger--;
            v->cake = true;
        }
        break;
    case EAR_LOOK:
        pose(v, LYNN_FR_IDLE_3, 0, 0, t < 12 ? LYNN_EMOTE_BANG : 0);
        if (t >= 20) set_mode(e, EAR_NONE);
        break;
    case EAR_BLOW:
        if (e->blow_run) {
            int target = -(2 + (int)(e->blow_total / 90));
            if (target < -24) target = -24;
            if (e->dx > target) e->dx--;
            bool brace = e->blow_total >= 450;
            int jitter = brace && e->ticks % 4 < 2 ? -1 : 0;
            pose(v, brace ? LYNN_FR_SCARED_1 : LYNN_FR_IDLE_BLINK, 0, e->dx + jitter, brace ? LYNN_EMOTE_SWEAT : 0);
            v->pose.hx = -1;
            v->wind = (uint8_t)(1 + (e->blow_level >= 26) + (e->blow_total >= 700));
            if (e->dx <= -20 || e->blow_total >= 1800) {
                e->vel = 2;
                e->quiet_ticks = 0;
                cue(e, NULL, "哇啊啊——要被吹走啦!", 0);
                set_mode(e, EAR_GONE);
            }
        } else {
            if (e->ticks % 2 == 0) approach(&e->dx, 0);  // 风停了,慢慢站回原位
            pose(v, e->dx < -2 ? LYNN_FR_SURPRISED : LYNN_FR_IDLE, 0, e->dx, 0);
            if (e->dx == 0) set_mode(e, EAR_NONE);
        }
        break;
    case EAR_GONE:
        if (e->dx > -100) {
            e->dx = (int16_t)(e->dx - e->vel);
            if (e->ticks % 3 == 0 && e->vel < 6) e->vel++;
        }
        e->lift = (int16_t)(t < 16 ? t * (16 - t) / 6 : 0);
        pose(v, LYNN_FR_SURPRISED_2, e->lift, e->dx, LYNN_EMOTE_BANG);
        if ((t / 3) % 2) v->pose.flags = LYNN_F_FLIP;      // 翻着跟头飞出去
        v->wind = e->blow_run ? 3 : 0;
        e->quiet_ticks = e->blow_run ? 0 : (uint16_t)(e->quiet_ticks + 1);
        if (e->dx <= -90 && e->quiet_ticks >= 40) {
            e->dx = -64;
            e->lift = 0;
            set_mode(e, EAR_RETURN);
        }
        break;
    case EAR_RETURN:
        e->dx++;
        pose(v, (t / 6) % 2 ? LYNN_FR_IDLE_2 : LYNN_FR_IDLE, (int)((t / 4) % 2), e->dx, LYNN_EMOTE_SWEAT);
        if (e->dx >= 0) {
            e->dx = 0;
            cue(e, "sigh", "呼……差点被吹跑了!", 0);
            set_mode(e, EAR_NONE);
        }
        break;
    case EAR_FLOAT: {
        if (e->ticks % 2 == 0) approach(&e->lift, e->float_target);
        pose(v, (t / 8) % 2 ? LYNN_FR_HAPPY : LYNN_FR_HAPPY_HOP, e->lift, 0, LYNN_EMOTE_NOTE);
        // 音高小游戏:星星从右边飞来,让她飘到同样的高度接住。
        if (!e->star && e->ticks >= e->star_next) {
            e->star = true;
            e->star_x = LYNN_STAGE_W;
            e->star_h = (int8_t)rnd(e, 2, MAX_LIFT);
        }
        if (e->star) {
            e->star_x--;
            if (e->star_x >= 30 && e->star_x <= 46 && e->star_h - e->lift <= 2 && e->lift - e->star_h <= 2) {
                e->star = false;
                if (e->score < 99) e->score++;
                e->sparkle = 10;
                e->star_next = e->ticks + rnd(e, 18, 40);
                cue(e, NULL, NULL, 1);
            } else if (e->star_x < -4) {
                e->star = false;
                e->star_next = e->ticks + 12;
            }
        }
        snprintf(e->caption, sizeof(e->caption), "飘浮 · 星星 %u", (unsigned)e->score);
        v->caption = e->caption;
        break;
    }
    case EAR_LAND:
        if (!e->landed_tick) {
            e->vel++;
            e->lift = (int16_t)(e->lift - e->vel);
            if (e->lift <= 0) {
                e->lift = 0;
                e->landed_tick = e->ticks;
            }
            pose(v, LYNN_FR_SURPRISED_2, e->lift, 0, 0);
        } else {
            uint32_t since = e->ticks - e->landed_tick;
            pose(v, since < 4 ? LYNN_FR_SURPRISED : LYNN_FR_HAPPY_2, 0, 0, since < 4 ? 0 : LYNN_EMOTE_SPARKLE);
            if (since >= 20) {
                if (e->score) {
                    snprintf(e->line, sizeof(e->line), "接住了 %u 颗星星!", (unsigned)e->score);
                    cue(e, NULL, e->line, 0);
                }
                set_mode(e, EAR_NONE);
            }
        }
        break;
    case EAR_SHH:
        pose(v, LYNN_FR_WINK_1, 0, 0, LYNN_EMOTE_DOTS);
        break;
    case EAR_WHISPER:
        pose(v, (t % 48) < 3 ? LYNN_FR_IDLE_BLINK : LYNN_FR_IDLE_2, 0, 3, LYNN_EMOTE_DOTS);
        v->pose.hy = 1;
        break;
    case EAR_ECHO: {
        const uint32_t wait = 10;                       // 先听完,再拍回去
        uint32_t iv = e->echo_ticks < 5 ? 5 : e->echo_ticks;
        if (t < wait) {
            pose(v, LYNN_FR_IDLE, 0, 0, LYNN_EMOTE_DOTS);
        } else if ((t - wait) / iv < e->echo_n) {
            uint32_t in = (t - wait) % iv;
            pose(v, in < 3 ? LYNN_FR_CLAP_2 : LYNN_FR_CLAP_1, 0, 0, 0);
            if (in == 0) cue(e, NULL, NULL, 1);
        } else if (t - wait - iv * e->echo_n < 16) {
            pose(v, LYNN_FR_HAPPY, 0, 0, LYNN_EMOTE_NOTE);
        } else {
            set_mode(e, EAR_NONE);
        }
        break;
    }
    case EAR_GROOVE: {
        uint32_t bt = (uint32_t)e->beat_ms * TPS / 1000;
        if (bt < 6) bt = 6;
        uint32_t since = e->ticks - e->beat_tick;
        pose(v, (since / bt) % 2 ? LYNN_FR_DANCE_2 : LYNN_FR_DANCE_1, since % bt < 3 ? 2 : 0, 0, LYNN_EMOTE_NOTE);
        v->eq = true;
        snprintf(e->caption, sizeof(e->caption), "跟着节拍 · %u BPM", (unsigned)(60000u / (e->beat_ms ? e->beat_ms : 1)));
        v->caption = e->caption;
        break;
    }
    case EAR_SWAY:
        pose(v, (t / 12) % 2 ? LYNN_FR_MUSIC_2 : LYNN_FR_MUSIC_1, 0, 0, LYNN_EMOTE_NOTE);
        v->eq = true;
        break;
    case EAR_NOISY:
        pose(v, LYNN_FR_FACEPALM_1, 0, 0, LYNN_EMOTE_SWEAT);
        v->pose.hx = (int8_t)((t / 3) % 2 ? 1 : -1);
        break;
    case EAR_CAKE:
        if (e->ticks % 2 == 0) approach(&e->dx, -8);
        pose(v, (t / 12) % 2 ? LYNN_FR_HAPPY_2 : LYNN_FR_HAPPY, 0, e->dx, e->candles ? LYNN_EMOTE_HEART : LYNN_EMOTE_SPARKLE);
        v->cake = true;
        v->wind = e->blow_run ? 1 : 0;
        if (!e->candles && !e->smoke) {
            cue(e, "party", "许愿成功!生日快乐~", 0);
            e->cake_linger = TPS * 4;
            set_mode(e, EAR_NONE);
        } else if (t > TPS * 40) {
            cue(e, NULL, "蛋糕先收起来啦,下次再吹~", 0);
            set_mode(e, EAR_NONE);
        }
        break;
    case EAR_SING:
        pose(v, (t / 10) % 2 ? LYNN_FR_HAPPY_2 : LYNN_FR_HAPPY, (t / 10) % 2, (t / 20) % 2 ? 1 : -1, LYNN_EMOTE_NOTE);
        v->eq = true;
        break;
    case EAR_COUNT:
        break;
    }
    v->candles = e->candles;
    v->smoke = e->smoke;
    v->sparkle = e->sparkle;
    v->star = e->mode == EAR_FLOAT && e->star;
    v->star_x = (int8_t)e->star_x;
    v->star_h = e->star_h;
    if (v->eq) memcpy(v->band, e->last.band, sizeof(v->band));
}

// ---------------------------------------------------------------------------
// 道具绘制
// ---------------------------------------------------------------------------

#define RGB565(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define C_INK    RGB565(0x38, 0x32, 0x2E)
#define C_MUTED  RGB565(0x6C, 0x66, 0x60)
#define C_FAINT  RGB565(0xC0, 0xB9, 0xAF)
#define C_YELLOW RGB565(0xFF, 0xCC, 0x00)
#define C_ORANGE RGB565(0xFF, 0x8A, 0x1F)
#define C_CREAM  RGB565(0xF5, 0xE6, 0xC8)
#define C_PINK   RGB565(0xF2, 0x9B, 0xB0)
#define C_RED    RGB565(0xE0, 0x3F, 0x1F)
#define C_BLUE   RGB565(0x5B, 0xB7, 0xDE)
#define C_WHITE  RGB565(0xFF, 0xFF, 0xFF)

static void px(uint16_t *buf, lynn_rect_t *box, int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= LYNN_STAGE_W || y >= LYNN_STAGE_H) return;
    buf[y * LYNN_STAGE_W + x] = c;
    if (x < box->x1) box->x1 = (int16_t)x;
    if (x > box->x2) box->x2 = (int16_t)x;
    if (y < box->y1) box->y1 = (int16_t)y;
    if (y > box->y2) box->y2 = (int16_t)y;
}

static void hline(uint16_t *buf, lynn_rect_t *box, int x0, int x1, int y, uint16_t c) {
    for (int x = x0; x <= x1; x++) px(buf, box, x, y, c);
}

static void draw_star(uint16_t *buf, lynn_rect_t *box, int x, int y, bool twinkle) {
    px(buf, box, x, y, C_WHITE);
    px(buf, box, x - 1, y, C_YELLOW);
    px(buf, box, x + 1, y, C_YELLOW);
    px(buf, box, x, y - 1, C_YELLOW);
    px(buf, box, x, y + 1, C_YELLOW);
    px(buf, box, x - 2, y, C_ORANGE);
    px(buf, box, x + 2, y, C_ORANGE);
    px(buf, box, x, y - 2, C_ORANGE);
    px(buf, box, x, y + 2, C_ORANGE);
    if (twinkle) {
        px(buf, box, x - 2, y - 2, C_YELLOW);
        px(buf, box, x + 2, y + 2, C_YELLOW);
        px(buf, box, x + 2, y - 2, C_YELLOW);
        px(buf, box, x - 2, y + 2, C_YELLOW);
    }
}

static void draw_cake(uint16_t *buf, lynn_rect_t *box, const lynn_ear_view_t *v, uint32_t ticks) {
    const int x0 = 60, x1 = 76;          // 盘子
    hline(buf, box, x0, x1, GROUND - 1, C_INK);
    for (int y = GROUND - 7; y <= GROUND - 2; y++) {      // 蛋糕体
        px(buf, box, x0 + 2, y, C_INK);
        hline(buf, box, x0 + 3, x1 - 3, y, C_CREAM);
        px(buf, box, x1 - 2, y, C_INK);
    }
    hline(buf, box, x0 + 2, x1 - 2, GROUND - 9, C_INK);     // 糖霜顶
    hline(buf, box, x0 + 3, x1 - 3, GROUND - 8, C_PINK);
    for (int x = x0 + 3; x <= x1 - 3; x += 3) px(buf, box, x, GROUND - 7, C_PINK);   // 糖霜滴落
    for (int x = x0 + 4; x <= x1 - 4; x += 4) px(buf, box, x, GROUND - 4, C_RED);    // 草莓夹层
    for (int i = 0; i < LYNN_EAR_CANDLES; i++) {
        int cx = x0 + 5 + i * 3;
        for (int y = GROUND - 13; y <= GROUND - 10; y++) px(buf, box, cx, y, i == 1 ? C_PINK : C_BLUE);
        px(buf, box, cx, GROUND - 14, C_INK);                 // 烛芯
        if (v->candles & (1u << i)) {
            // 火苗:跳动;有风时向左倒
            int lean = v->wind ? -1 : 0;
            int flick = (int)((ticks / 3 + (uint32_t)i) % 3) - 1;
            px(buf, box, cx + lean, GROUND - 15, C_YELLOW);
            px(buf, box, cx + lean + (flick < 0 ? -1 : 0), GROUND - 16, C_ORANGE);
            px(buf, box, cx + lean + (v->wind ? -1 : flick > 0 ? 1 : 0), GROUND - 17, flick ? C_ORANGE : C_YELLOW);
        } else if (v->smoke) {
            int rise = (30 - v->smoke) / 3;
            px(buf, box, cx + (rise / 2) % 2, GROUND - 16 - rise, C_FAINT);
            if (rise > 2) px(buf, box, cx - (rise / 3) % 2, GROUND - 14 - rise, C_FAINT);
        }
    }
}

void lynn_ear_render(const lynn_ear_view_t *v, uint32_t ticks, uint16_t *buf, lynn_rect_t *box) {
    // 音量线:地面下方一行,从中间向两边展开 —— 她在听。
    if (v->vu) {
        int half = v->vu;
        uint16_t c = v->vu >= 30 ? C_ORANGE : v->vu >= 16 ? C_YELLOW : C_FAINT;
        hline(buf, box, 40 - half, 39 + half, LYNN_STAGE_H - 1, c);
    }
    // 风:从右往左的短划线,风越大越多、越长、越快。
    for (int i = 0; i < v->wind * 3; i++) {
        int y = 18 + (i * 37) % 48;
        int speed = 3 + i % 2 + v->wind;
        int len = 3 + i % 4 + v->wind;
        int x = LYNN_STAGE_W - (int)((ticks * (uint32_t)speed + (uint32_t)i * 29) % 110);
        px(buf, box, x, y, C_MUTED);
        hline(buf, box, x + 1, x + len, y, C_FAINT);
    }
    if (v->cake) draw_cake(buf, box, v, ticks);
    // 频谱柱:左右各 6 根,外侧低频、内侧高频,顶上一格黄色。
    if (v->eq) {
        for (int b = 0; b < MIC_BANDS; b++) {
            int h = v->band[b];
            int xl = 3 + b * 3, xr = LYNN_STAGE_W - 5 - b * 3;
            for (int k = 0; k < h; k++) {
                uint16_t c = k == h - 1 ? C_YELLOW : C_INK;
                px(buf, box, xl, GROUND - 1 - k, c);
                px(buf, box, xl + 1, GROUND - 1 - k, c);
                px(buf, box, xr, GROUND - 1 - k, c);
                px(buf, box, xr + 1, GROUND - 1 - k, c);
            }
        }
    }
    if (v->star) draw_star(buf, box, v->star_x, CELL_TOP + 30 - v->star_h, (ticks / 4) % 2);
    if (v->sparkle) {
        int r = 10 - v->sparkle + 3, cx = 40, cy = CELL_TOP + 26;
        px(buf, box, cx - r, cy, C_YELLOW);
        px(buf, box, cx + r, cy, C_YELLOW);
        px(buf, box, cx, cy - r, C_YELLOW);
        px(buf, box, cx - r * 2 / 3, cy - r * 2 / 3, C_ORANGE);
        px(buf, box, cx + r * 2 / 3, cy - r * 2 / 3, C_ORANGE);
    }
}
