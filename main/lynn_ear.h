// main/lynn_ear.h —— Lynn 的"耳朵":把麦克风特征变成舞台上的即时反应(纯逻辑,host 可测)。
//
// 输入是 mic_sense 每 20 ms 的特征帧(lynn_ear_feed),输出在每个舞台 tick(24 fps,lynn_ear_tick)
// 给出一份"视图":是否接管 Lynn 的姿势(帧、离地高度、左右位移……),以及舞台道具(风、蛋糕、
// 星星、频谱柱、音量线)。一次性的请求(播放某个动作、改台词、播放音效)通过 lynn_ear_take_cue 取走。
//
// 玩法(都只在本地处理,声音不会上传):
//   吹气        风从右往左刮,Lynn 眯眼、抱紧自己、被一点点推开;一直吹就被吹出舞台,过一会儿再走回来
//   哼"啊——"   她飘起来,越大声/音越高飘得越高;飘着时会有星星飞来,对准高度就能接住(音高小游戏)
//   唱一段歌词  小小演唱会:她跟着摇摆,两侧亮起频谱;唱完为你鼓掌
//   生日蛋糕    一年一次,不由声音触发:agent 或串口 "lynn play cake" 端出,对着麦克风吹灭三根蜡烛就撒花许愿
//   拍手        一下:她转头看你;两到五下:她照你的节奏拍回来;"短-短-长"三下:暗号,撒花;
//               连续均匀地拍:跟着你的节拍跳舞,停下来她会鞠躬道谢
//   "嘘——"     她竖起手指,然后乖乖睡一会儿
//   小声说话    她凑近听,然后眨眼:"这是我们的秘密"
//   持续的音乐  戴上耳机摇摆,舞台两侧出现像素频谱
//   长时间吵闹  她捂住脸;安静下来才松一口气
//   一直安静    她会自己打起瞌睡;这时突然有声音会把她吓一跳
#pragma once

#include "lynn_stage.h"
#include "mic_sense.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    EAR_NONE = 0,
    EAR_LOOK,        // 单次拍手:转头看
    EAR_BLOW,        // 被风吹
    EAR_GONE,        // 被吹出舞台
    EAR_RETURN,      // 走回来
    EAR_FLOAT,       // 哼唱飘浮
    EAR_LAND,        // 落地
    EAR_SHH,         // 嘘
    EAR_WHISPER,     // 悄悄话
    EAR_ECHO,        // 回声拍手
    EAR_GROOVE,      // 跟着节拍跳舞
    EAR_SWAY,        // 听音乐摇摆
    EAR_NOISY,       // 太吵了
    EAR_CAKE,        // 生日蛋糕
    EAR_SING,        // 听你唱歌
    EAR_COUNT,
} lynn_ear_mode_t;

#define LYNN_EAR_CANDLES 3
#define LYNN_EAR_MAX_CLAPS 8
#define LYNN_EAR_CUES 4

typedef struct {
    // 姿势接管
    bool live;
    lynn_step_t pose;
    // 道具
    uint8_t wind;                  // 0..3 风力
    bool cake;
    uint8_t candles;               // 仍亮着的蜡烛位掩码
    uint8_t smoke;                 // 刚熄灭的蜡烛冒烟剩余 tick
    bool eq;
    uint8_t band[MIC_BANDS];       // 0..15
    uint8_t vu;                    // 0..40:地面下方的音量线半宽
    bool star;
    int8_t star_x, star_h;         // 星星的舞台 x 与"高度"(与 lift 同一标尺)
    uint8_t sparkle;               // 接住星星的闪光剩余 tick
    const char *caption;           // 卡片小字;NULL = 舞台默认
} lynn_ear_view_t;

typedef struct {
    const char *action;            // 让舞台播放的动作 id;NULL = 无
    const char *line;              // 气泡台词;NULL = 不改
    uint8_t sfx;                   // 非 0:播放提示音
} lynn_ear_cue_t;

typedef struct {
    lynn_ear_mode_t mode;
    uint32_t ticks, mode_tick;     // 舞台 tick 计数与进入当前模式时的 tick
    uint32_t rng;
    bool drowsy;                   // 舞台上她正在打瞌睡/睡觉(由 tick 传入)
    mic_frame_t last;

    // 各类声音的持续时长(ms,麦克风时钟)
    uint32_t blow_run, blow_gap, blow_total;
    uint8_t blow_level;
    uint32_t shh_run, shh_gap;
    uint32_t voiced_run, voice_gap;
    uint32_t whisper_run;
    uint32_t sound_run, sound_gap;
    uint32_t loud_run, loud_gap;
    uint32_t quiet_ms;
    bool dozed_for_quiet;
    uint16_t note_hz;              // 当前稳定音的音高
    uint32_t note_ms;
    uint16_t float_ref_hz;         // 飘浮开始时的音高(参考点)
    uint32_t song_ms, song_gap;
    uint8_t song_notes;
    uint8_t song_sylls;            // 唱词的字:元音之间的短停顿(哼唱/拖长音没有)
    uint32_t unvoiced_ms;          // 当前这段无音高的时长

    // 拍手
    uint32_t claps[LYNN_EAR_MAX_CLAPS];
    uint8_t clap_n;
    uint32_t clap_clock;           // 最近一帧的麦克风时钟
    uint8_t echo_n;
    uint16_t echo_ticks;           // 回声拍手的间隔(tick)
    uint16_t beat_ms;
    uint32_t beat_tick;            // 最近一次对齐节拍的 tick

    // 动画
    int16_t dx, lift, vel;
    uint16_t quiet_ticks;          // 被吹走后多久没再吹
    uint32_t landed_tick;          // 落地时刻;0 = 还在下落
    int8_t float_target;
    bool star;
    int16_t star_x;
    int8_t star_h;
    uint32_t star_next;
    uint8_t sparkle;
    uint8_t score;
    uint8_t candles, smoke;
    uint16_t cake_linger;          // 撒花期间蛋糕继续留在舞台上
    char caption[32];
    char line[48];                 // 动态台词(接住几颗星星)

    lynn_ear_cue_t cues[LYNN_EAR_CUES];
    uint8_t cue_head, cue_count;
} lynn_ear_t;

void lynn_ear_init(lynn_ear_t *e, uint32_t seed);

// 喂入一帧麦克风特征(时间戳来自麦克风时钟,只需单调)。
void lynn_ear_feed(lynn_ear_t *e, const mic_frame_t *f);

// 前进一个舞台 tick。drowsy:舞台上她正在打瞌睡/睡觉。view 为本 tick 的视图。
void lynn_ear_tick(lynn_ear_t *e, bool drowsy, lynn_ear_view_t *view);

// 取走一个一次性请求;没有返回 false。
bool lynn_ear_take_cue(lynn_ear_t *e, lynn_ear_cue_t *cue);

// 直接端出生日蛋糕(agent / 串口命令 "lynn play cake")。
void lynn_ear_cake(lynn_ear_t *e);

// 麦克风暂停(录音、离开 Lynn 页)时调用:清掉进行中的检测,结束接管(蛋糕保留,到时自己收起)。
void lynn_ear_reset(lynn_ear_t *e);

const char *lynn_ear_mode_name(lynn_ear_mode_t mode);

// 把道具画进舞台缓冲(在 lynn_stage_render 之后调用),并扩展内容包围盒。
void lynn_ear_render(const lynn_ear_view_t *v, uint32_t ticks, uint16_t *buf, lynn_rect_t *box);
