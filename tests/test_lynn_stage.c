// tests/test_lynn_stage.c —— Lynn 舞台引擎:每个动作都能播完并回到待机、渲染不越界、
// 待机日程会自动开始活动、浇花/纸飞机特效、台词截断、无变化时不重绘。
#include "lynn_stage.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t buf[LYNN_STAGE_W * LYNN_STAGE_H + 1];

static int run_until_idle(lynn_stage_t *s, int limit, bool *saw_plane, bool *saw_drop) {
    for (int t = 0; t < limit; t++) {
        lynn_stage_tick(s);
        buf[LYNN_STAGE_W * LYNN_STAGE_H] = 0xBEEF;        // 越界哨兵
        lynn_stage_render(s, buf, NULL);
        assert(buf[LYNN_STAGE_W * LYNN_STAGE_H] == 0xBEEF);
        if (saw_plane && s->plane_on) *saw_plane = true;
        if (saw_drop) {
            for (int i = 0; i < LYNN_MAX_DROPS; i++) {
                if (s->drops[i][1]) *saw_drop = true;
            }
        }
        if (s->action < 0) return t + 1;
    }
    return -1;
}

int main(void) {
    lynn_stage_t s;
    lynn_stage_init(&s, 12345);
    assert(s.action == -1 && strcmp(lynn_stage_action_id(&s), "idle") == 0);
    assert(lynn_action_count >= 40 && lynn_idle_count >= 10);
    assert(lynn_action_count - lynn_idle_count >= 30);

    // 每个动作都能在合理时间内播完并回到基础待机。
    for (int i = 0; i < lynn_action_count; i++) {
        lynn_stage_init(&s, 99u + (uint32_t)i);
        lynn_stage_play_index(&s, i);
        assert(s.action == i);
        assert(strcmp(lynn_stage_line(&s), lynn_actions[i].line) == 0);
        bool plane = false, drop = false;
        int t = run_until_idle(&s, 24 * 60, &plane, &drop);
        if (t < 0) {
            printf("action %s never finished\n", lynn_actions[i].id);
            assert(0);
        }
        if (lynn_actions[i].fx == LYNN_FX_PLANE) assert(plane);
        if (lynn_actions[i].fx == LYNN_FX_POT) assert(drop);
        assert(strcmp(lynn_stage_line(&s), LYNN_BASE_LINE) == 0);
    }

    // 按 id 播放;未知 id 不影响当前状态。
    lynn_stage_init(&s, 7);
    assert(lynn_stage_play(&s, "pat") >= 0 && strcmp(lynn_stage_action_id(&s), "pat") == 0);
    assert(lynn_stage_busy(&s));
    assert(lynn_stage_play(&s, "no_such_action") == -1 && strcmp(lynn_stage_action_id(&s), "pat") == 0);

    // 随机互动:只挑互动动作,且不连续重复。
    int prev = -1;
    for (int k = 0; k < 200; k++) {
        int i = lynn_stage_play_random(&s);
        assert(i >= lynn_idle_count && i < lynn_action_count && i != prev);
        prev = i;
    }

    // 基础待机会在 8~15 秒后自动开始一个待机活动,活动结束后回到待机。
    lynn_stage_init(&s, 4242);
    int seen = 0;
    for (int t = 0; t < 24 * 60 * 5 && seen < 3; t++) {
        lynn_stage_tick(&s);
        if (s.action >= 0) {
            assert(s.action < lynn_idle_count);       // 自动开始的只会是待机活动
            assert(!lynn_stage_busy(&s));
            int end = run_until_idle(&s, 24 * 90, NULL, NULL);
            assert(end > 0);
            seen++;
        }
    }
    assert(seen == 3);

    // 台词:自定义台词用于下一个动作;超长时在 UTF-8 字符边界截断;NULL 恢复默认。
    lynn_stage_init(&s, 1);
    lynn_stage_say(&s, "测试");
    assert(strcmp(lynn_stage_line(&s), "测试") == 0);
    lynn_stage_play(&s, "wave");
    assert(strcmp(lynn_stage_line(&s), "测试") == 0);
    lynn_stage_say(&s, NULL);
    assert(strcmp(lynn_stage_line(&s), lynn_actions[lynn_action_find("wave")].line) == 0);
    char longtext[300];
    longtext[0] = '\0';
    for (int i = 0; i < 40; i++) strcat(longtext, "中");
    lynn_stage_say(&s, longtext);
    size_t n = strlen(lynn_stage_line(&s));
    assert(n < LYNN_LINE_MAX && n % 3 == 0);

    // 长时间保持同一帧且没有粒子/气泡时,tick 报告"无变化",界面可跳过重绘。
    lynn_stage_init(&s, 3);
    lynn_stage_tick(&s);
    int changed = 0;
    for (int t = 0; t < 20; t++) changed += lynn_stage_tick(&s);
    assert(changed == 0);

    // 内容包围盒:覆盖人物与阴影,且在舞台内;浇花时还要覆盖花盆。
    lynn_stage_init(&s, 5);
    lynn_rect_t box;
    lynn_stage_render(&s, buf, &box);
    assert(box.x1 >= 0 && box.y1 >= 0 && box.x2 < LYNN_STAGE_W && box.y2 < LYNN_STAGE_H && box.x1 < box.x2);
    assert(box.y2 >= LYNN_STAGE_H - 4);            // 阴影在地面上
    // 包围盒之外只能是背景色或地平线。
    for (int y = 0; y < LYNN_STAGE_H; y++) {
        for (int x = 0; x < LYNN_STAGE_W; x++) {
            if (x >= box.x1 && x <= box.x2 && y >= box.y1 && y <= box.y2) continue;
            uint16_t c = buf[y * LYNN_STAGE_W + x];
            assert(c == lynn_palette[LYNN_COLOR_PAPER] || c == lynn_palette[LYNN_COLOR_SHADOW]);
        }
    }
    lynn_stage_play(&s, "water");
    lynn_rect_t wide = box;
    for (int t = 0; t < 24 * 3; t++) {
        lynn_stage_tick(&s);
        lynn_stage_render(&s, buf, &box);
        if (box.x2 > wide.x2) wide.x2 = box.x2;
    }
    assert(wide.x2 >= 8 + lynn_pot_x + 8);           // 花盆在人物右侧,也被包含

    puts("test_lynn_stage: PASS");
    return 0;
}
