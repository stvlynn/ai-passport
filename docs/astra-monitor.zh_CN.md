<p align="right">
  <strong>简体中文</strong> · <a href="astra-monitor.md">English</a>
</p>

# Astra Monitor

运行在 FoloToy AI Passport 上的 [AstraFoundry](https://github.com/AstraFoundry)
组织像素风 GitHub 监控面板，分支 `feature/astra-monitor`。它显示星标数、开放
issue 与 PR、最新动态和仓库排行。看板娘是 Lynn（[lynn.stv.pm](https://lynn.stv.pm)），
配色遵循 AstraFoundry Sunbeam 设计系统（`AstraFoundry/design`）：暖纸底与墨色文字，
黄色只用于"今日新增星标"。

## 页面与按键

| 页面 | 内容 |
| --- | --- |
| Lynn | 大尺寸全身 Lynn（3 倍像素画，24 fps）与台词卡：10 个待机活动、31 个互动动作；见 [Lynn 动作](lynn-actions.zh_CN.md) |
| 总览 | 总星标、今日新增、开放 issue、开放 PR、仓库数、焦点仓库，以及带状态气泡的 Lynn |
| 动态 | 最近 12 条星标增长、issue、PR |
| 仓库 | 按星标排序的仓库，含私有标记、issue/PR 数和平方根比例星数条 |
| 设备 | Wi-Fi、GitHub、同步、电池状态，以及设置 |

| 按键 | 动作 |
| --- | --- |
| 上 / 下 | 上一页 / 下一页（首尾相接） |
| 确定 单击 / 双击 | 页内选中下一项 / 上一项：焦点仓库、列表行或设备页菜单项（循环） |
| 确定 长按 | 确认。设备页：执行选中项。其他页：立即同步。配网页：返回 |
| 上 / 下 长按 | 总览或 Lynn 页：戳一下 Lynn |
| Lynn 页确定单击 / 长按 | 随机一个互动 / 摸头 |

页面顺序为 Lynn ⇄ 总览 ⇄ 动态 ⇄ 仓库 ⇄ 设备，开机停在总览。

每个键在所有页面上只有一种含义，翻页与页内选择不会冲突。每次长按都有可见反馈：
提示卡会说明同步已开始、无法同步的原因（没有 Wi-Fi、没有 token）、设置的新值，
或同步结果。

设备页菜单：网页配网、立即同步、提示音（默认关闭）、拍手唤醒、屏幕亮度（100/70/40/20%）、
息屏时间（1/5/10/30 分钟或从不，默认 10 分钟）、清除 Wi-Fi（需在 3 秒内再长按一次确认）。

Lynn 是逐帧动画（`main/lynn_anim.c`）。没有需要关注的事情时，她按待机日程轮换：
站着眨眼、左右张望，然后看书（翻页、读到有趣处微笑）、敲电脑（抬头看你、潇洒地
按回车）、喝茶、戴耳机听音乐、伸懒腰，或打瞌睡（点头、冒鼻涕泡、猛地惊醒），
之后回到站立。看书和敲电脑最常见，其次是喝茶和听音乐，伸懒腰和打瞌睡较少。
同步时她在敲电脑，屏幕变暗时打瞌睡，关屏时睡觉。心情姿态（开心、审阅、困惑、
惊讶、睡觉）会循环播放小动作，而不是停在一帧。

反应动画只播一次，之后回到日常：新星标时跳起来；PR 合并或总星数跨过 50 的整数倍时
拉礼花；新 issue 时拿放大镜端详；新 PR 时审阅；警告时困惑。戳她会轮流招手或跳起来；
打瞌睡时被戳会先惊醒。

背光在息屏时间的一半时变暗（最晚 90 秒），到息屏时间时关闭；选"从不"时只变暗不关屏。第一次按键只唤醒屏幕。屏幕变暗或关闭时，
0.7 秒内拍手两下也会唤醒（使用麦克风，可关闭）。有新动态时会点亮屏幕、播放
8-bit 音效（提示音开启时）、滑入提示卡，Lynn 也会做出反应：星标与合并时跳起来，issue 时惊讶，
PR 时进入审阅状态。

## 硬件使用

| 硬件 | 用途 |
| --- | --- |
| ST7789 屏幕、LVGL | 四个页面、配网页、提示卡、精灵动画 |
| 三个 ADC 按键 | 单击、双击、长按 |
| ES8311 喇叭 | 16 kHz 实时合成的方波音效 |
| ES8311 麦克风 | 屏幕变暗或关闭时检测拍手两下 |
| CW2017 电量计 | 状态栏电量百分比，设备页电压 |
| Wi-Fi STA | HTTPS GraphQL 请求与 SNTP 校时 |
| Wi-Fi SoftAP | 配网热点、DNS 劫持与网页表单，只在配网页运行 |
| NVS | Wi-Fi 凭据、GitHub token、设置、每日星标基线 |
| USB Serial/JTAG | 配置命令行 |

## 首次设置

1. 在 `0x0` 烧录合并镜像 `build/FoloToy-AI-Passport-full.bin`。这会清空 NVS，
   包括已保存的 Wi-Fi 和 token。
2. 为该组织创建只读的 fine-grained GitHub token（Metadata、Issues、
   Pull requests 读权限）。
3. 在设备上：设备页 → 网页配网，长按 OK。设备会开启名为 `AstraMonitor-XXXX`
   的 WPA2 热点，密码为随机 8 位数字，并以 Wi-Fi 二维码显示。
4. 用手机相机扫码，或手动连接该热点。配网网页通常会自动弹出；否则用浏览器打开
   `http://192.168.4.1`。选择 2.4 GHz 网络，填写密码，粘贴 token 后提交。
5. 设备会先试连新的 Wi-Fi。连上后热点自动关闭并开始第一次同步；约 20 秒内连不上
   则恢复原来的 Wi-Fi，可在网页上重新提交。

配网网页还会显示本机的 Wi-Fi MAC 地址，可用于路由器白名单或 DHCP 静态分配；
串口 `status` 命令也会打印它。FoloToy 配网小程序只接入官方固件，因此本应用参照
社区经验 [SoftAP 配网与资源预算](reference/phoenixzhc/softap-provisioning-and-resource-budget.zh_CN.md)
改用 SoftAP 网页配网，而不是 BLUFI。本固件关闭了蓝牙。

USB 串口命令行可替代网页。串口命令：`token <值>`、`token clear`、`wifi <ssid> [密码]`（SSID 含空格时
加双引号）、`wifi clear`、`refresh`、`status`。`status` 不会打印任何密钥。

编译期选项位于 menuconfig 的 `Astra Monitor` 菜单：组织名、刷新间隔
（默认 120 秒）、时区（默认 `CST-8`）、最晚变暗时间、默认息屏时间，以及可选的备用 token。
不要把 token 提交到 `sdkconfig.defaults`。

## 数据与限制

每次刷新发两个 GraphQL 请求。第一个返回星标最多的 24 个仓库的计数与推送时间，
以及剩余配额（约 2.5 KB）；第二个返回最近推送的 6 个仓库中各自最近更新的 3 个
issue 和 3 个 PR（约 4 KB）。读缓冲上限 20 KB。

fine-grained token 读不到"谁点了星标"：GitHub 会拒绝 `stargazers` 连接，并把整个
仓库置为 null；组织范围的 `search` 对它也返回空结果。因此设备不请求这两项。星标
动态由两次同步之间的星数增量生成（某仓库"新增 3 颗星标"），issue/PR 动态只覆盖
最近推送过的仓库。
"今日"是自本地零点后第一次成功同步以来总星标数的变化；取消 star 会让它减少。

12 px 字体覆盖 ASCII、GB2312 一级汉字和常用中文标点。issue 或 PR 标题中的其他
字符，例如生僻字、日文假名和 emoji，会显示为 LVGL 占位方框。

## 维护

- `python3 tools/gen_app_assets.py`：根据 `assets/images/lynn/` 和脚本内的
  图标像素画重新生成 `main/ui_assets.c`。
- `python3 tools/gen_app_fonts.py --font <fusion-pixel-12px-proportional-zh_hans.ttf>`：
  修改文案后重新生成字体。
- `tools/ui_preview/run.sh`：在电脑上用真实的 `main/ui.c` 和 LVGL 渲染所有页面，
  输出到 `build/ui_preview/`。
- 宿主测试：`tests/test_gh_model.c`、`tests/test_gh_parse.c`、
  `tests/test_app_nav.c`、`tests/test_sound_synth.c`、`tests/test_portal_proto.c`，由
  `./tools/validate.sh --static` 运行。

基线硬件测试演示源码（`main/main.c`、`main/demo_*.c`、`main/ui_pixel*`）仍留在
仓库中供其宿主测试使用，但不参与本固件的构建。
