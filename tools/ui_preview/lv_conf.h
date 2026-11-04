// LVGL configuration for the host UI preview (tools/ui_preview). Mirrors the
// device's colour depth and UTF-8 settings; memory is generous on the host.
#pragma once
#define LV_COLOR_DEPTH 16
#define LV_USE_OS LV_OS_NONE
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (512 * 1024)
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_USE_FONT_PLACEHOLDER 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_QRCODE 1
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0
