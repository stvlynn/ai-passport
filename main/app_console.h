// main/app_console.h —— USB 串口(USB-Serial-JTAG)上的配置命令行。
//
//   token <value>        保存 GitHub token 到 NVS(不回显)
//   token clear          清除 NVS 中的 token
//   wifi <ssid> [pass]   保存 Wi-Fi 凭据并连接;SSID 含空格时用双引号包住
//   wifi clear           清除 Wi-Fi 凭据
//   refresh              立即刷新
//   status               打印联网/同步状态与内存(不含任何密钥)
//   key up|down|ok|double|long   模拟按键(设备测试用)
//   lynn list                    列出 Lynn 的全部动作(agent 工具 lynn_list_actions)
//   lynn play <id> [台词]        播放动作(agent 工具 lynn_play_action)
//   lynn say <台词>              改台词(agent 工具 lynn_say)
//   lynn status                  当前动作与台词(agent 工具 lynn_status)
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    APP_CONSOLE_REFRESH = 0,
    APP_CONSOLE_STATUS,
    APP_CONSOLE_TOKEN_CHANGED,
    APP_CONSOLE_KEY_UP,
    APP_CONSOLE_KEY_DOWN,
    APP_CONSOLE_KEY_OK,
    APP_CONSOLE_KEY_DOUBLE,
    APP_CONSOLE_KEY_LONG,
    APP_CONSOLE_KEY_PRESS,     // OK down (push-to-talk)
    APP_CONSOLE_KEY_RELEASE,   // OK up
} app_console_cmd_t;

typedef void (*app_console_cb_t)(app_console_cmd_t cmd);

// Lynn 工具命令的执行者(在 console 任务中调用,实现方自行加 LVGL 锁)。
// verb: play / say / status;arg: 动作 id 或台词;line: play 的可选台词。返回 false 表示失败。
typedef bool (*app_console_lynn_cb_t)(const char *verb, const char *arg, const char *line, char *out, size_t cap);

esp_err_t app_console_start(app_console_cb_t cb);
void app_console_set_lynn(app_console_lynn_cb_t cb);
