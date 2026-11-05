// main/lynn_stage.c —— 见 lynn_stage.h。
#include "lynn_stage.h"

#include <string.h>

#define GROUND      (LYNN_STAGE_H - 3)     // 地面所在行
#define FEET_ROW    60                     // 帧内脚底下方一行
#define CELL_X      8                      // 64x64 帧在舞台上的 x
#define EMOTE_X     42                     // 表情气泡相对帧的位置
#define EMOTE_Y     (-6)
#define HAND_X      24

static const lynn_step_t *phase_steps(const lynn_action_t *a, uint8_t phase, uint16_t *count) {
    switch (phase) {
    case 0:  *count = a->n_entry; return &lynn_steps[a->entry];
    case 1:  *count = a->n_loop;  return &lynn_steps[a->loop];
    default: *count = a->n_exit;  return &lynn_steps[a->exit];
    }
}

static uint32_t rnd(lynn_stage_t *s, uint32_t lo, uint32_t hi) {
    uint32_t x = s->rng ? s->rng : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->rng = x;
    return lo + x % (hi - lo + 1);
}

static const lynn_step_t *current(const lynn_stage_t *s) {
    if (s->action < 0) return &lynn_base[s->base_step];
    uint16_t n;
    const lynn_step_t *st = phase_steps(&lynn_actions[s->action], s->phase, &n);
    return n ? &st[s->step] : &lynn_base[0];
}

static void schedule_activity(lynn_stage_t *s) {
    s->next_activity = s->ticks + rnd(s, 8, 15) * LYNN_TICK_HZ;
}

static void set_line(lynn_stage_t *s, const char *text) {
    size_t cap = sizeof(s->line), o = 0;
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    while (*p) {
        size_t n = *p < 0x80 ? 1 : (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : 4;
        if (o + n >= cap) break;
        memcpy(s->line + o, p, n);
        o += n;
        p += n;
    }
    s->line[o] = '\0';
}

static void enter_phase(lynn_stage_t *s, uint8_t phase);

static void finish_action(lynn_stage_t *s) {
    s->action = -1;
    s->base_step = 0;
    s->base_tick = 0;
    s->line_custom = false;
    set_line(s, LYNN_BASE_LINE);
    schedule_activity(s);
}

// 进入某阶段;空阶段直接跳到下一阶段。
static void enter_phase(lynn_stage_t *s, uint8_t phase) {
    const lynn_action_t *a = &lynn_actions[s->action];
    for (;;) {
        if (phase > 2) {
            finish_action(s);
            return;
        }
        uint16_t n;
        phase_steps(a, phase, &n);
        if (n) break;
        phase++;
    }
    s->phase = phase;
    s->step = 0;
    s->step_tick = 0;
}

void lynn_stage_play_index(lynn_stage_t *s, int index) {
    if (index < 0 || index >= (int)lynn_action_count) return;
    const lynn_action_t *a = &lynn_actions[index];
    s->action = (int16_t)index;
    s->loops_left = (uint8_t)rnd(s, a->loops_min, a->loops_max);
    s->perk = false;
    s->plane_on = false;
    memset(s->drops, 0, sizeof(s->drops));
    if (!s->line_custom) set_line(s, a->line);
    s->line_custom = false;
    enter_phase(s, 0);
}

int lynn_action_find(const char *id) {
    for (uint8_t i = 0; i < lynn_action_count; i++) {
        if (strcmp(lynn_actions[i].id, id) == 0) return i;
    }
    return -1;
}

int lynn_stage_play(lynn_stage_t *s, const char *id) {
    int i = lynn_action_find(id);
    if (i >= 0) lynn_stage_play_index(s, i);
    return i;
}

int lynn_stage_play_random(lynn_stage_t *s) {
    int n = (int)lynn_action_count - (int)lynn_idle_count;
    int i;
    do {
        i = (int)lynn_idle_count + (int)rnd(s, 0, (uint32_t)n - 1);
    } while (n > 1 && i == s->action);
    lynn_stage_play_index(s, i);
    return i;
}

void lynn_stage_say(lynn_stage_t *s, const char *text) {
    if (text) {
        set_line(s, text);
        s->line_custom = true;
    } else {
        s->line_custom = false;
        set_line(s, s->action >= 0 ? lynn_actions[s->action].line : LYNN_BASE_LINE);
    }
}

void lynn_stage_init(lynn_stage_t *s, uint32_t seed) {
    memset(s, 0, sizeof(*s));
    s->rng = seed ? seed : 1;
    s->last_activity = -1;
    finish_action(s);
}

static void start_activity(lynn_stage_t *s) {
    int i;
    do {
        i = (int)rnd(s, 0, lynn_idle_count - 1u);
    } while (lynn_idle_count > 1 && i == s->last_activity);
    s->last_activity = (int16_t)i;
    lynn_stage_play_index(s, i);
}

static void advance(lynn_stage_t *s) {
    if (s->action < 0) {
        if (++s->base_tick < lynn_base[s->base_step].ticks) return;
        s->base_tick = 0;
        s->base_step = (uint16_t)((s->base_step + 1) % lynn_base_count);
        if (s->ticks >= s->next_activity) start_activity(s);
        return;
    }
    const lynn_step_t *st = current(s);
    if (++s->step_tick < st->ticks) return;
    s->step_tick = 0;
    uint16_t n;
    phase_steps(&lynn_actions[s->action], s->phase, &n);
    if (++s->step < n) return;
    if (s->phase == 1 && s->loops_left > 1) {
        s->loops_left--;
        s->step = 0;
        if (lynn_actions[s->action].fx == LYNN_FX_POT && s->loops_left == 1) s->perk = true;
        return;
    }
    enter_phase(s, (uint8_t)(s->phase + 1));
}

static void effects(lynn_stage_t *s, const lynn_step_t *st) {
    // 水滴:倒水时每 2 tick 从壶嘴落下一滴,每 tick 下落 1 像素,落到盆口溅起水花。
    if ((st->flags & LYNN_F_POUR) && s->ticks % 2 == 0) {
        for (int i = 0; i < LYNN_MAX_DROPS; i++) {
            if (!s->drops[i][1]) {
                s->drops[i][0] = (int8_t)(lynn_spout_x + 1);
                s->drops[i][1] = (int8_t)(lynn_spout_y + 1);
                break;
            }
        }
    }
    for (int i = 0; i < LYNN_MAX_DROPS; i++) {
        if (!s->drops[i][1]) continue;
        if (++s->drops[i][1] >= lynn_pot_rim + 2) {     // 落进盆口 2 行后溅起水花
            s->drops[i][1] = 0;
            s->splash = 3;
        }
    }
    if (s->splash) s->splash--;
    if (st->flags & LYNN_F_PERK) s->perk = true;
    // 纸飞机:出手后每 tick 向右 2、每 2 tick 向上 1,飞出舞台后消失。
    if ((st->flags & LYNN_F_LAUNCH) && !s->plane_on && s->step_tick == 0) {
        s->plane_on = true;
        s->plane_x = (int16_t)(CELL_X + lynn_launch_x);
        s->plane_y = (int16_t)(GROUND - FEET_ROW + lynn_launch_y);
    }
    if (s->plane_on) {
        s->plane_x += 2;
        if (s->ticks % 2 == 0) s->plane_y -= 1;
        if (s->plane_x > LYNN_STAGE_W || s->plane_y < -4) s->plane_on = false;
    }
    if (st->emote != s->emote) {
        s->emote = st->emote;
        s->emote_age = 0;
    } else if (s->emote_age < 255) {
        s->emote_age++;
    }
}

static uint32_t mix(uint32_t h, int32_t v) {
    h ^= (uint32_t)v + 0x9E3779B9u + (h << 6) + (h >> 2);
    return h;
}

static uint32_t signature(const lynn_stage_t *s) {
    const lynn_step_t *st = current(s);
    uint32_t h = mix(0, st->frame);
    h = mix(h, st->lift);
    h = mix(h, st->dx);
    h = mix(h, st->hx * 16 + st->hy);
    h = mix(h, st->flags & LYNN_F_FLIP);
    h = mix(h, st->hand);
    if (s->emote) h = mix(h, s->emote * 4 + (s->emote_age < 3) * 2 + (s->emote_age / 12) % 2);
    h = mix(h, s->splash * 2 + s->perk);
    h = mix(h, s->plane_on ? s->plane_x * 256 + s->plane_y : -1);
    h = mix(h, s->action >= 0 && lynn_actions[s->action].fx == LYNN_FX_POT);
    for (int i = 0; i < LYNN_MAX_DROPS; i++) h = mix(h, s->drops[i][0] * 256 + s->drops[i][1]);
    return h;
}

bool lynn_stage_tick(lynn_stage_t *s) {
    s->ticks++;
    advance(s);
    effects(s, current(s));
    uint32_t sig = signature(s);
    bool changed = sig != s->sig;
    s->sig = sig;
    return changed;
}

// ---------------------------------------------------------------------------
// 合成
// ---------------------------------------------------------------------------

static lynn_rect_t s_box;                   // 合成时累计的内容包围盒

static void grow(int x, int y) {
    if (x < s_box.x1) s_box.x1 = (int16_t)x;
    if (x > s_box.x2) s_box.x2 = (int16_t)x;
    if (y < s_box.y1) s_box.y1 = (int16_t)y;
    if (y > s_box.y2) s_box.y2 = (int16_t)y;
}

static void blit(uint16_t *buf, const lynn_bmp_t *b, int x0, int y0, bool flip, int head_rows, int hx, int hy) {
    const uint8_t *px = &lynn_pixels[b->offset];
    for (int y = 0; y < b->h; y++) {
        for (int x = 0; x < b->w; x++) {
            uint8_t c = px[y * b->w + x];
            if (!c) continue;
            int cx = b->x + x, cy = b->y + y;            // 帧内坐标
            if (flip) cx = 63 - cx;
            if (cy < head_rows) {                        // 头部:点头/摇头位移
                cx += hx;
                cy += hy;
            }
            int sx = x0 + cx, sy = y0 + cy;
            if (sx < 0 || sy < 0 || sx >= LYNN_STAGE_W || sy >= LYNN_STAGE_H) continue;
            buf[sy * LYNN_STAGE_W + sx] = lynn_palette[c];
            grow(sx, sy);
        }
    }
}

static void sprite_at(uint16_t *buf, int id, int x, int y) {
    blit(buf, &lynn_sprites[id], x - lynn_sprites[id].x, y - lynn_sprites[id].y, false, 0, 0, 0);
}

void lynn_stage_render(const lynn_stage_t *s, uint16_t *buf, lynn_rect_t *content) {
    const lynn_step_t *st = current(s);
    s_box = (lynn_rect_t){ LYNN_STAGE_W, LYNN_STAGE_H, -1, -1 };
    for (int i = 0; i < LYNN_STAGE_W * LYNN_STAGE_H; i++) buf[i] = lynn_palette[LYNN_COLOR_PAPER];
    for (int x = 4; x < LYNN_STAGE_W - 4; x++) buf[GROUND * LYNN_STAGE_W + x] = lynn_palette[LYNN_COLOR_SHADOW];
    // 脚下阴影:离地越高越窄。
    int sw = 16 - st->lift;
    if (sw < 10) sw = 10;
    int cx = CELL_X + 32 + st->dx;
    for (int x = cx - sw / 2; x <= cx + sw / 2; x++) buf[(GROUND - 1) * LYNN_STAGE_W + x] = lynn_palette[LYNN_COLOR_SHADOW];
    grow(cx - 8, GROUND - 1);                       // 阴影最宽 16 像素:整段算进包围盒,宽窄变化都能被重绘
    grow(cx + 8, GROUND - 1);

    int x0 = CELL_X + st->dx;
    int y0 = GROUND - FEET_ROW - st->lift;
    bool pot = s->action >= 0 && lynn_actions[s->action].fx == LYNN_FX_POT;
    blit(buf, &lynn_frames[st->frame], x0, y0, (st->flags & LYNN_F_FLIP) != 0, lynn_head_rows, st->hx, st->hy);

    int cell_y = GROUND - FEET_ROW;
    if (pot) {
        sprite_at(buf, s->perk ? LYNN_SPR_POT_PERK : LYNN_SPR_POT_DROOP, CELL_X + lynn_pot_x, cell_y + lynn_pot_y);
        for (int i = 0; i < LYNN_MAX_DROPS; i++) {
            if (s->drops[i][1]) sprite_at(buf, LYNN_SPR_DROP, CELL_X + s->drops[i][0], cell_y + s->drops[i][1]);
        }
        if (s->splash) sprite_at(buf, LYNN_SPR_SPLASH, CELL_X + lynn_pot_x + 1, cell_y + lynn_pot_rim - 2 - (s->splash % 2));
    }
    if (s->plane_on) sprite_at(buf, LYNN_SPR_PLANE, s->plane_x, s->plane_y);
    if (st->hand != LYNN_HAND_NONE) sprite_at(buf, LYNN_SPR_HAND, x0 + HAND_X, y0 + st->hand);
    if (s->emote) {
        // 气泡跟随身体但滞后(只升起 1/3 跳跃高度);出现时先小后大;每半秒上下浮 1 像素。
        int ex = x0 + EMOTE_X, ey = cell_y + EMOTE_Y - st->lift / 3 - (s->emote_age / 12) % 2;
        if (s->emote_age < 3) {
            sprite_at(buf, LYNN_SPR_POP, ex + 2, ey + 4);
        } else {
            sprite_at(buf, LYNN_SPR_EMOTE_BASE + s->emote - 1, ex, ey);
        }
    }
    if (content) *content = s_box;
}

const char *lynn_stage_line(const lynn_stage_t *s) {
    return s->line;
}

const char *lynn_stage_action_id(const lynn_stage_t *s) {
    return s->action >= 0 ? lynn_actions[s->action].id : "idle";
}

const char *lynn_stage_action_name(const lynn_stage_t *s) {
    return s->action >= 0 ? lynn_actions[s->action].name : "待机";
}

bool lynn_stage_busy(const lynn_stage_t *s) {
    return s->action >= (int)lynn_idle_count;
}
