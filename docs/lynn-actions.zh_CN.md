<p align="right">
  <strong>简体中文</strong> · <a href="lynn-actions.md">English</a>
</p>

# Lynn 动作

<!-- 由 tools/gen_lynn_stage.py 根据 tools/lynn_actions.py 生成;请修改目录文件,不要改本文件。 -->

Lynn 页（总览左边）显示大尺寸全身 Lynn：64 像素栅格的像素画整数放大 3 倍，以 24 fps 播放。每张画面像手绘像素动画一样保持若干拍，而摸头的手、水滴、纸飞机和表情气泡每拍都会移动。

她有 10 个待机活动和 31 个互动动作。活动之间她会呼吸、眨眼、左右张望；每隔 8~15 秒随机开始一个待机活动（进入、随机循环若干次、退出）。互动动作会打断她，播放一次后回到待机。

## 按键

| 输入 | 动作 |
| --- | --- |
| 确定单击或双击 | 随机一个互动 |
| 确定长按、上/下长按 | 摸头 |
| 上 / 下 | 上一页 / 下一页 |

## Agent 工具

每个动作都是 agent 工具。[`lynn-tools.json`](lynn-tools.json) 包含工具定义（`lynn_play_action`、`lynn_say`、`lynn_list_actions`、`lynn_status`）、JSON 输入结构和完整动作列表。在网络桥接完成之前，电脑上的 agent 通过 USB 串口调用它们，每行一条命令：

| 工具 | 串口命令 |
| --- | --- |
| `lynn_play_action` | `lynn play <动作> [台词]` |
| `lynn_say` | `lynn say <台词>` |
| `lynn_list_actions` | `lynn list` |
| `lynn_status` | `lynn status` |

播放动作或修改台词会把设备切到 Lynn 页并点亮屏幕。台词为 UTF-8，约 30 字以内；12 px 字体（GB2312 一级汉字与 ASCII）以外的字符显示为占位方框。

## 待机活动

| Id | 名称 | 默认台词 | 秒（一轮） |
| --- | --- | --- | --- |
| `read` | 看书 | 这本书好有意思。 | 9.5 |
| `type` | 敲电脑 | 写代码中,别打扰我哦。 | 9.9 |
| `tea` | 喝茶 | 喝口茶,歇一会儿。 | 8.8 |
| `music` | 听音乐 | 这首歌好好听~ | 7.4 |
| `stretch` | 伸懒腰 | 嗯——伸个懒腰。 | 3.8 |
| `doze` | 打瞌睡 | Zzz…… | 9.8 |
| `water` | 浇花 | 给小花浇水~ | 5.5 |
| `sweep` | 扫地 | 打扫一下,干干净净。 | 4.8 |
| `paper_plane` | 纸飞机 | 飞吧,纸飞机! | 3.4 |
| `doodle` | 画画 | 画一只小猫给你看。 | 5.5 |

## 互动动作

| Id | 名称 | 默认台词 | 秒 |
| --- | --- | --- | --- |
| `pat` | 摸头 | 诶嘿嘿……被摸头了。 | 3.4 |
| `wave` | 挥手 | 你好呀! | 2.2 |
| `cheer` | 欢呼 | 太棒了! | 1.4 |
| `party` | 撒花 | 恭喜恭喜! | 3.1 |
| `inspect` | 放大镜 | 嗯?让我仔细看看。 | 2.8 |
| `surprised` | 吓一跳 | 哇!吓我一跳。 | 1.4 |
| `confused` | 疑惑 | 咦?这是怎么回事? | 2.5 |
| `think` | 思考 | 让我想一想…… | 3.8 |
| `sleep` | 睡觉 | 晚安…… | 7.3 |
| `shy` | 害羞 | 别、别这样看我啦…… | 2.8 |
| `pout` | 生气 | 哼,不理你了! | 2.8 |
| `cry` | 哭哭 | 呜呜呜…… | 2.5 |
| `laugh` | 大笑 | 哈哈哈,笑死我了! | 3.4 |
| `bow` | 鞠躬 | 请多多指教。 | 2.3 |
| `heart` | 比心 | 送你一颗心~ | 2.5 |
| `thumbs_up` | 点赞 | 干得漂亮! | 1.8 |
| `peace` | 剪刀手 | 耶~ | 1.8 |
| `salute` | 敬礼 | 收到,保证完成任务! | 1.7 |
| `clap` | 鼓掌 | 啪啪啪! | 2.2 |
| `shrug` | 摊手 | 我也不知道诶。 | 1.6 |
| `facepalm` | 捂脸 | 唉,真拿你没办法。 | 1.7 |
| `nod` | 点头 | 嗯嗯,好的! | 1.6 |
| `shake_head` | 摇头 | 不行不行。 | 1.8 |
| `point` | 指向你 | 就是你! | 1.6 |
| `spin` | 转圈 | 转个圈圈~ | 1.5 |
| `dance` | 跳舞 | 一起来跳舞吧! | 2.8 |
| `scared` | 害怕 | 好、好可怕…… | 1.7 |
| `sneeze` | 打喷嚏 | 阿——嚏! | 2.1 |
| `snack` | 吃饭团 | 开动啦,好好吃! | 3.0 |
| `wink` | 眨眼 | 嘘,这是我们的秘密。 | 1.6 |
| `sigh` | 叹气 | 唉……好累啊。 | 1.9 |

## 新增动作

1. 在 `tools/lynn_art.py` 里描述新画面，用 `python3 tools/lynn_art.py --phase keys|inbetweens` 和 image-gen 批处理脚本生成关键帧与中间帧。
2. 用 `python3 tools/gen_lynn_stage.py --prep` 清理并人工检查，结果放在 `assets/images/lynn_stage/`。
3. 在 `tools/lynn_actions.py` 添加时间轴，运行 `python3 tools/gen_lynn_stage.py`。
4. 用 `tools/lynn_stage_preview/run.sh <id>` 预览，再运行宿主测试。
