// tools/lynn_stage_preview/dump.c —— 用真实的 main/lynn_stage.c 与生成数据,把动作逐 tick 渲染成原始帧。
// 输出:每个动作一个 <id>.bin(连续的 80x76 RGB565 帧)和 <id>.txt(第一行台词,第二行帧数)。
// 由 tools/lynn_stage_preview/run.sh 调用,再由 Python 合成 240x320 页面 GIF。
#include "lynn_stage.h"

#include <stdio.h>
#include <string.h>

static uint16_t buf[LYNN_STAGE_W * LYNN_STAGE_H];

static void dump(const char *dir, int index) {
    lynn_stage_t s;
    lynn_stage_init(&s, 2026u + (uint32_t)index);
    // 动作前后各留一小段基础待机,方便看进出。
    for (int t = 0; t < 10; t++) lynn_stage_tick(&s);
    lynn_stage_play_index(&s, index);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.bin", dir, lynn_actions[index].id);
    FILE *f = fopen(path, "wb");
    int frames = 0, after = 0;
    for (int t = 0; t < 24 * 40 && after < 12; t++) {
        lynn_stage_tick(&s);
        lynn_stage_render(&s, buf, NULL);
        fwrite(buf, sizeof(buf), 1, f);
        frames++;
        if (s.action < 0) after++;
    }
    fclose(f);
    snprintf(path, sizeof(path), "%s/%s.txt", dir, lynn_actions[index].id);
    f = fopen(path, "w");
    fprintf(f, "%s\n%s\n%d\n%s\n", lynn_actions[index].line, lynn_actions[index].name, frames,
            index < lynn_idle_count ? "待机" : "互动");
    fclose(f);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    for (int i = 0; i < lynn_action_count; i++) {
        if (argc > 2) {
            int keep = 0;
            for (int a = 2; a < argc; a++) keep |= strcmp(argv[a], lynn_actions[i].id) == 0;
            if (!keep) continue;
        }
        dump(dir, i);
    }
    return 0;
}
