// main/app_net.h —— Wi-Fi STA 连接、SoftAP 网页配网与 SNTP 校时。
//
// Wi-Fi 常驻:有凭据就自动连接并按指数退避重连。配网时切到 APSTA:设备开一个
// WPA2 热点(随机 8 位密码,屏幕上以二维码展示),手机连上后由 app_portal 提供
// 网页表单。新凭据先试连,连上才算数;失败则恢复原来的凭据,不会把设备"配丢"。
// 最多保存 5 组凭据到应用 NVS;断线轮换重试,日志和网页不回显密码。
#pragma once

#include "esp_err.h"
#include "wifi_profiles.h"
#include <stdbool.h>
#include <stdint.h>

#define APP_NET_AP_PREFIX "AstraMonitor-"
#define APP_NET_SCAN_MAX  12

typedef enum {
    NET_LINK_NO_CREDENTIALS = 0,
    NET_LINK_CONNECTING,
    NET_LINK_ONLINE,
    NET_LINK_OFFLINE,        // 有凭据但连不上,等待退避重连
} net_link_t;

typedef enum {
    NET_PROV_OFF = 0,
    NET_PROV_STARTING,
    NET_PROV_AP_READY,           // 热点已开,等手机连接
    NET_PROV_CLIENT_CONNECTED,   // 手机已连上热点并拿到地址
    NET_PROV_WIFI_CONNECTING,    // 正在试连网页提交的 Wi-Fi
    NET_PROV_WIFI_FAILED,        // 试连失败,已恢复原凭据,可重新提交
    NET_PROV_DONE,               // 新 Wi-Fi 已连上
    NET_PROV_FAILED,             // 热点或网页服务启动失败
} net_prov_t;

typedef struct {
    net_link_t link;
    net_prov_t prov;
    char ssid[33];
    char ip[16];
    int8_t rssi;             // 0 = 未知
    bool time_synced;
    uint8_t last_reason;     // 最近一次断开原因(wifi_err_reason_t 低 8 位)
    char mac[18];            // Wi-Fi STA MAC,"AA:BB:CC:DD:EE:FF";空 = 未知
    char ap_ssid[33];        // 配网热点名与密码(仅配网期间有效)
    char ap_pass[12];
} net_status_t;

// 状态变化时在 Wi-Fi 事件任务等上下文里调用;实现只能投递事件,不得阻塞。
typedef void (*net_listener_t)(void);

esp_err_t app_net_init(net_listener_t listener);
net_status_t app_net_status(void);
esp_err_t app_net_set_credentials(const char *ssid, const char *password);
esp_err_t app_net_clear_credentials(void);

// 已保存网络名(不返回密码),以及删除一组凭据。
int app_net_saved_networks(char ssids[][33], int max);
esp_err_t app_net_forget_network(const char *ssid);

// 空密码保留同名已存密码;新网络空密码表示开放网络。
// 试连一组新凭据:成功后保留;失败(约 20 秒内连不上)则恢复之前的凭据。
// 可在 HTTP 服务任务里调用,不阻塞。
esp_err_t app_net_try_credentials(const char *ssid, const char *password);

// 配网期间缓存的扫描结果(按信号强度降序),供网页下拉列表使用。返回条数。
int app_net_scan_results(char ssids[][33], int max);

// 在普通任务里调用(会阻塞约 2 秒做一次扫描);不要在按键回调或持有 LVGL 锁时调用。
esp_err_t app_net_provision_start(void);
void app_net_provision_stop(void);
