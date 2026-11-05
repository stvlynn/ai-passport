// main/app_audio.h —— 音频工作任务:播放芯片音效,并在息屏时用麦克风检测"拍两下"唤醒。
// I2S operations are serialized with the bounded voice capture worker.
#pragma once

#include "esp_err.h"
#include "sound_synth.h"

#include <stdbool.h>
#include <stddef.h>

typedef void (*app_audio_clap_cb_t)(void);

// audio_ok=false(codec 初始化失败)时任务仍创建,但所有请求被忽略。
esp_err_t app_audio_start(bool audio_ok, app_audio_clap_cb_t on_clap);

void app_audio_play(sound_id_t id);
void app_audio_set_enabled(bool sound_on);
// 打开后任务在空闲时持续读取麦克风;播放音效期间自动暂停检测。
void app_audio_set_listening(bool listen);

// Worker-only, synchronous streaming capture. Each callback receives 20ms of
// 16kHz mono signed little-endian PCM. No full utterance is allocated. Playback
// and clap detection pause while capturing. A slow/failed sink aborts capture.
typedef esp_err_t (*app_audio_sink_t)(const void *pcm, size_t bytes, void *arg);
esp_err_t app_audio_capture(app_audio_sink_t sink, void *arg, unsigned seconds);
