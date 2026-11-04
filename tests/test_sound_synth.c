// tests/test_sound_synth.c —— 方波合成长度/幅度与拍手检测窗口。
#include "sound_synth.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t render_all(sound_id_t id, int16_t amp, int16_t *peak, int *edges) {
    sound_player_t p;
    sound_player_start(&p, id, amp);
    int16_t buf[97];   // 奇数块长:覆盖音符跨块边界
    size_t total = 0, n;
    int16_t last = 0;
    *peak = 0;
    *edges = 0;
    while ((n = sound_player_render(&p, buf, sizeof(buf) / sizeof(buf[0]))) > 0) {
        for (size_t i = 0; i < n; i++) {
            int16_t a = buf[i] < 0 ? (int16_t)-buf[i] : buf[i];
            if (a > *peak) *peak = a;
            if ((buf[i] > 0) != (last > 0)) (*edges)++;
            last = buf[i];
        }
        total += n;
    }
    return total;
}

int main(void) {
    for (int id = SOUND_BOOT; id < SOUND_COUNT; id++) {
        int16_t peak;
        int edges;
        size_t n = render_all((sound_id_t)id, 6000, &peak, &edges);
        size_t expect = sound_duration_ms((sound_id_t)id) * SOUND_RATE_HZ / 1000;
        assert(n + 8 >= expect && n <= expect + 8);   // 每个音符取整误差 <1 样本
        assert(peak <= 6000 && peak > 3000);
        assert(edges > 4);
    }
    int16_t peak;
    int edges;
    assert(render_all(SOUND_NONE, 6000, &peak, &edges) == 0);
    assert(sound_duration_ms(SOUND_COUNT) == 0);

    // 1kHz 音 1 秒:25% 占空比约 1000 个正脉冲 → ~2000 次过零。
    // 用 STAR 的已知时长验证频率合理即可,这里检查 TICK(1175Hz,12ms)≈ 28 次过零。
    render_all(SOUND_TICK, 6000, &peak, &edges);
    assert(edges >= 24 && edges <= 32);

    int16_t quiet[320], loud[320];
    memset(quiet, 0, sizeof(quiet));
    for (int i = 0; i < 320; i++) loud[i] = (int16_t)(i % 2 ? 20000 : -20000);
    clap_detector_t d;
    clap_detector_init(&d, 12000);
    assert(!clap_detector_feed(&d, quiet, 320, 0));
    assert(!clap_detector_feed(&d, loud, 320, 100));     // 第一次
    assert(!clap_detector_feed(&d, loud, 320, 150));     // 不应期内的尾音
    assert(clap_detector_feed(&d, loud, 320, 500));      // 窗口内第二次
    assert(!clap_detector_feed(&d, loud, 320, 2000));    // 重新开始计数
    assert(!clap_detector_feed(&d, loud, 320, 3000));    // 超出窗口:仍算第一次
    assert(clap_detector_feed(&d, loud, 320, 3400));

    puts("test_sound_synth: PASS");
    return 0;
}
