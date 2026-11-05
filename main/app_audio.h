// main/app_audio.h —— 音频工作任务:播放芯片音效,并在息屏时用麦克风检测"拍两下"唤醒。
// I2S operations are serialized with the bounded voice capture worker.
#pragma once

#include "esp_err.h"
#include "sound_synth.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*app_audio_clap_cb_t)(void);

// audio_ok=false(codec 初始化失败)时任务仍创建,但所有请求被忽略。
esp_err_t app_audio_start(bool audio_ok, app_audio_clap_cb_t on_clap);

void app_audio_play(sound_id_t id);
void app_audio_set_enabled(bool sound_on);
// 打开后任务在空闲时持续读取麦克风;播放音效期间自动暂停检测。
void app_audio_set_listening(bool listen);

// Push-to-talk recorder. rec_start begins capturing immediately into buf as
// 8 kHz IMA ADPCM (voice_codec.h); recording ends on rec_stop, when buf is full
// or on a read error. buf must stay valid until rec_bytes reports done.
// Playback and clap detection pause while recording.
bool app_audio_rec_start(uint8_t *buf, size_t cap);
void app_audio_rec_stop(void);
// Bytes already encoded (safe to upload). done/err report completion.
size_t app_audio_rec_bytes(bool *done, esp_err_t *err);
