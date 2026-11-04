// main/app_portal.h —— 配网热点上的网页服务与 DNS 劫持(captive portal)。
//
// 只在配网期间运行:单客户端、3 个 HTTP socket、请求体上限 1KB。
// 表单提交后调用 app_net_try_credentials() 试连 Wi-Fi,token 写入 NVS;
// 结果显示在设备屏幕上(试连时热点可能随 STA 换信道而短暂断开)。
#pragma once

#include "esp_err.h"

#define APP_PORTAL_IP "192.168.4.1"

esp_err_t app_portal_start(void);
void app_portal_stop(void);
