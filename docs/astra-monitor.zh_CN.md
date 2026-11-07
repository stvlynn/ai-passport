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
| 上 / 下 长按 | 总览页：戳一下 Lynn |
| Lynn 页确定单击 / 双击 / 长按 | 录制五秒语音发给 Lynn |

页面顺序为 Lynn ⇄ 总览 ⇄ 动态 ⇄ 仓库 ⇄ 设备，开机停在总览。

每个键在所有页面上只有一种含义，翻页与页内选择不会冲突。每次长按都有可见反馈：
提示卡会说明同步已开始、无法同步的原因（没有 Wi-Fi、没有 token）、设置的新值，
或同步结果。

设备页菜单：网页配网、立即同步、提示音（默认关闭）、麦克风（拍手唤醒与 Lynn 声音互动）、屏幕亮度（100/70/40/20%）、
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

## Lynn 页的声音互动

Lynn 页亮屏且“麦克风”开关打开时，Lynn 会一直听，并立即做出反应。全部在设备上完成（`main/mic_sense.c`
从每 20 ms 的声音里提取响度、音高、频谱倾斜和短促声；`main/lynn_ear.c` 决定她的反应）。只有按住 OK
说话时才会录音上传。

| 声音 | Lynn |
| --- | --- |
| 对着设备吹气 | 风从屏幕上刮过；她眯眼、抱紧自己、被一点点推开。一直吹就翻着跟头被吹出舞台，过一会儿再走回来叹气 |
| 哼“啊——” | 她飘起来，越大声、音越高飘得越高。飘着时会有星星飞来，用音高对准高度就能接住 |
| 唱一段有歌词的歌 | 小小演唱会：她跟着摇摆，两侧亮起频谱柱；唱完为你鼓掌 |
| 拍一下手 | 她转头看向声音 |
| 拍两到五下 | 她按你的节奏拍回同样的次数 |
| “短-短-长”拍三下 | 暗号：撒花 |
| 均匀地拍手或有鼓点 | 跟着你的节拍跳舞，两侧出现频谱；停下后她鞠躬道谢 |
| “嘘——” | 她竖起手指，然后睡一会儿 |
| 小声说话 | 她凑近听，然后眨眼 |
| 持续的音乐 | 戴上耳机摇摆，出现频谱 |
| 长时间吵闹 | 她捂住脸，安静下来才松口气 |
| 长时间安静 | 她自己打起瞌睡；突然一声响会把她吓一跳 |

地面下方的细线表示她听到的音量。声音互动进行时屏幕不会变暗。生日一年只有一次，所以声音不会端出蛋糕：
由 agent 或串口命令 `lynn play cake` 端出，对着麦克风吹灭三根蜡烛。串口 `lynn ear` 打印她此刻听到的
内容（响度、音高、倾斜、反应），便于在真机上调门限。

## 硬件使用

| 硬件 | 用途 |
| --- | --- |
| ST7789 屏幕、LVGL | 四个页面、配网页、提示卡、精灵动画 |
| 三个 ADC 按键 | 单击、双击、长按 |
| ES8311 喇叭 | 16 kHz 实时合成的方波音效 |
| ES8311 麦克风 | 屏幕变暗或关闭时检测拍手两下；亮屏的 Lynn 页上的声音互动（在设备上处理，不上传） |
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
   `http://192.168.4.1`。先在“服务设置”中保存 token，再在“添加或更新 Wi-Fi”中
   选择 2.4 GHz 网络，填写密码后保存并连接。两张表单独立，添加 Wi-Fi 无需填写服务配置。
5. 设备会先试连新的 Wi-Fi。连上后热点自动关闭并开始第一次同步；约 20 秒内连不上
   则恢复原来的 Wi-Fi，可在网页上重新提交。

配网网页最多列出 5 个已保存的 Wi-Fi，提供连接和删除按钮。重新打开网页配网即可继续添加网络。
试连成功后新增或更新该网络，不删除其它配置；试连或存储写入失败时保留旧列表。
重启优先连接最近保存的网络，连接失败后按退避间隔轮换尝试已保存的网络。
保留 NVS 升级时会自动迁移旧版的单组 Wi-Fi 配置。
设备菜单中的“清除 Wi-Fi”和串口 `wifi clear` 会删除全部网络；列表已满时需先删除一个再添加。

连接已保存网络无需输入密码。更新同名网络时密码留空保留原值；新网络密码留空表示开放网络。
已有加密网络如需改为开放网络，请先删除再添加。网页不会回显已保存的密码或 token。
Connector URL 和 Bot ID 自动回填，服务字段留空保留原值；只保存服务设置不会重新连接 Wi-Fi。

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
  `tests/test_app_nav.c`、`tests/test_sound_synth.c`、`tests/test_portal_proto.c`、
  `tests/test_wifi_profiles.c`、`tests/test_app_net_profiles.c`、`tests/test_app_portal.c` 和
  `tests/test_app_settings_wifi.c`，由
  `./tools/validate.sh --static` 运行。

基线硬件测试演示源码（`main/main.c`、`main/demo_*.c`、`main/ui_pixel*`）仍留在
仓库中供其宿主测试使用，但不参与本固件的构建。

## Lynn 第一屏的 Grok Bot 交互

在网页配网中填写 HTTPS 连接器地址、设备 Bearer 密钥和 Lynn Bot ID。在亮屏的 Lynn 页按确定键，出现 Listening 后开始说话。Mac 本地转写五秒录音，通过 Grok Bot routine Webhook 发给 Lynn。Lynn 调用连接器的 MCP 文本及动作工具，回复在台词卡滚动显示，不播放语音。全部 41 个动作分别可调用。其他页面保留原有操作。离开 Lynn 页会取消录音并暂停接收事件；返回时建立新游标，不重播旧动作。

鉴权、工具注册、限制及验证见[连接器部署指南](../connector/README.zh_CN.md)。Tunnel 正常运行不代表 Bot 接入已完成。
