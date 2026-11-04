// main/app_console.h —— USB 串口(USB-Serial-JTAG)上的配置命令行。
//
//   token <value>        保存 GitHub token 到 NVS(不回显)
//   token clear          清除 NVS 中的 token
//   wifi <ssid> [pass]   保存 Wi-Fi 凭据并连接;SSID 含空格时用双引号包住
//   wifi clear           清除 Wi-Fi 凭据
//   refresh              立即刷新
//   status               打印联网/同步状态与内存(不含任何密钥)
//   key up|down|ok|double|long   模拟按键(设备测试用)
#pragma once

#include "esp_err.h"

typedef enum {
    APP_CONSOLE_REFRESH = 0,
    APP_CONSOLE_STATUS,
    APP_CONSOLE_TOKEN_CHANGED,
    APP_CONSOLE_KEY_UP,
    APP_CONSOLE_KEY_DOWN,
    APP_CONSOLE_KEY_OK,
    APP_CONSOLE_KEY_DOUBLE,
    APP_CONSOLE_KEY_LONG,
} app_console_cmd_t;

typedef void (*app_console_cb_t)(app_console_cmd_t cmd);

esp_err_t app_console_start(app_console_cb_t cb);
