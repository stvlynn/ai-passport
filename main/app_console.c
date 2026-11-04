// main/app_console.c —— 见 app_console.h。
#include "app_console.h"

#include "app_net.h"
#include "app_settings.h"

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "console";

#define LINE_MAX_LEN 192

static app_console_cb_t s_cb;

// 取下一个参数:支持双引号包裹(SSID 可含空格)。返回 NULL 表示没有更多参数。
static char *next_arg(char **cursor) {
    char *p = *cursor;
    while (*p == ' ') p++;
    if (!*p) return NULL;
    char *start;
    if (*p == '"') {
        start = ++p;
        while (*p && *p != '"') p++;
    } else {
        start = p;
        while (*p && *p != ' ') p++;
    }
    if (*p) *p++ = '\0';
    *cursor = p;
    return start;
}

static void handle_line(char *line) {
    char *cur = line;
    char *cmd = next_arg(&cur);
    if (!cmd) return;
    if (strcmp(cmd, "token") == 0) {
        char *val = next_arg(&cur);
        if (!val) {
            printf("usage: token <value> | token clear\n");
            return;
        }
        bool clear = strcmp(val, "clear") == 0;
        esp_err_t err = app_settings_set_token(clear ? NULL : val);
        memset(line, 0, LINE_MAX_LEN);
        printf("token %s: %s\n", clear ? "cleared" : "saved", esp_err_to_name(err));
        if (err == ESP_OK && s_cb) s_cb(APP_CONSOLE_TOKEN_CHANGED);
    } else if (strcmp(cmd, "wifi") == 0) {
        char *ssid = next_arg(&cur);
        if (!ssid) {
            printf("usage: wifi <ssid> [password] | wifi clear\n");
            return;
        }
        esp_err_t err;
        if (strcmp(ssid, "clear") == 0) {
            err = app_net_clear_credentials();
        } else {
            err = app_net_set_credentials(ssid, next_arg(&cur));
        }
        memset(line, 0, LINE_MAX_LEN);
        printf("wifi: %s\n", esp_err_to_name(err));
    } else if (strcmp(cmd, "refresh") == 0) {
        if (s_cb) s_cb(APP_CONSOLE_REFRESH);
        printf("refresh requested\n");
    } else if (strcmp(cmd, "key") == 0) {
        static const char *names[] = { "up", "down", "ok", "double", "long" };
        char *k = next_arg(&cur);
        for (int i = 0; k && i < 5; i++) {
            if (strcmp(k, names[i]) == 0) {
                if (s_cb) s_cb((app_console_cmd_t)(APP_CONSOLE_KEY_UP + i));
                return;
            }
        }
        printf("usage: key up|down|ok|double|long\n");
    } else if (strcmp(cmd, "status") == 0) {
        if (s_cb) s_cb(APP_CONSOLE_STATUS);
    } else {
        printf("commands: token, wifi, refresh, status, key\n");
    }
}

static void console_task(void *arg) {
    (void)arg;
    static char line[LINE_MAX_LEN];
    size_t len = 0;
    for (;;) {
        uint8_t c;
        if (usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY) != 1) continue;
        if (c == '\r' || c == '\n') {
            if (len == 0) continue;
            line[len] = '\0';
            handle_line(line);
            memset(line, 0, sizeof(line));
            len = 0;
        } else if (c == 0x7f || c == '\b') {
            if (len) len--;
        } else if (len < sizeof(line) - 1) {
            line[len++] = (char)c;
        }
    }
}

esp_err_t app_console_start(app_console_cb_t cb) {
    s_cb = cb;
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB 串口驱动安装失败: %s", esp_err_to_name(err));
        return err;
    }
    usb_serial_jtag_vfs_use_driver();
    if (xTaskCreate(console_task, "console", 3072, NULL, 2, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
