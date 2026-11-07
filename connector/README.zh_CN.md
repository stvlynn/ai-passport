<p align="right"><a href="README.md">English</a> · <strong>简体中文</strong></p>

# Lynn Mac 连接器

连接器接收设备录制的五秒音频，在 Mac 本地转写，并触发所选 Grok Bot 的 Routine webhook。Grok 必须调用连接器的 MCP 工具才能将回复文字和动画送到设备。Webhook 返回 HTTP 200 仅表示已接受请求；连接器不会把响应正文当作 Bot 回复。

需要 Node 22+、供 Apple Silicon ASR 使用的 Python 3.12、公共 HTTPS 隧道，以及提供官方 webhook 和 Bearer 认证 Streamable HTTP MCP 的 Grok Bot Routine。设备需要能访问隧道的互联网连接。用户 LaunchAgent 运行期间，Mac 必须保持唤醒并登录。Node 运行时不需要额外安装软件包。

## 配置和安装

1. 将 [config.example.json](config.example.json) 复制到 Git 目录以外的私有位置，并设置权限为 `0600`。运行两次 `openssl rand -hex 32`，分别生成独立的设备和工具令牌。配置所选 Bot ID、名称、准确的公共隧道主机名、本地 ASR 程序和参数、状态目录。状态目录使用绝对路径，或使用相对于安装后配置文件的 `state`。默认端口为 `18787`。不得将密钥放进终端命令参数或受版本控制的文件。
2. 将 `webhookURL` 设置为官方 Routine webhook 地址，将 `webhookKey` 设置为其 Bearer 密钥。配置期间可以同时省略这两项；此时已认证的语音上传返回 `503 not_ready`，MCP 仍然可用。示例 URL 仅为占位符，并非 Grok API 地址。
3. 按下文准备 ASR，然后安装运行时和用户 LaunchAgent：

   ```bash
   python3 connector/install-macos.py --config /absolute/private/config.json --node /absolute/path/to/node
   ```

   添加 `--no-load` 可仅准备文件而不启动服务。安装器先验证配置，再安装到 `~/Library/Application Support/AI Passport Connector`，保留已有 ASR 模型和虚拟环境。它从 [docs/lynn-tools.json](../docs/lynn-tools.json) 复制唯一动作目录，单独写入私有 `config.json`，并生成 `~/Library/LaunchAgents/com.aipassport.lynn-connector.plist`。安装使用当前用户的主目录和 UID，不需要 `sudo`。修改代码或配置后重新安装。ASR 程序和参数中的路径必须为绝对路径；JSON 内的 `~` 和 shell 变量不会展开。配置修改后必须重启服务。
4. 仅将目标 HTTPS 主机通过隧道路由到 `http://127.0.0.1:18787`。配置中的 `publicHost` 必须与隧道主机名一致。连接器始终只绑定 `127.0.0.1`，并拒绝未知 Host 和 Origin。隧道认证必须允许各自携带独立凭据的设备及 Grok 客户端访问这些接口。
5. 在所选 Grok Bot 的 MCP 设置中，注册 `https://YOUR_HOST/mcp`，使用 `toolToken` 作为 Bearer 认证令牌。设备配置使用 `https://YOUR_HOST`、`botId` 和 `deviceToken`。为 Bot 添加以下指令：

   > 你是 Lynn。必须调用 lynn_say 或带 text 的 lynn_action_* 工具回复用户。普通聊天输出不会送到设备。根据动作描述选择合适的动画。工具返回 queued 后，除非确实需要再次展示，否则不要重复调用。

使用 `launchctl print gui/$(id -u)/com.aipassport.lynn-connector` 查看服务。使用 `launchctl bootout gui/$(id -u)/com.aipassport.lynn-connector` 停止服务；plist 和私有状态会保留，方便之后重新 bootstrap。日志仅含操作类型和生成的请求 ID，不含凭据、音频、转写文本、webhook URL 或上游错误正文。日志是安装目录 `logs/` 下的私有文件；请按普通 Mac 维护方式进行轮换。`GET /healthz` 仅返回 `{"ok":true}`，表示进程健康，不证明 Grok 或设备连通。

## 本地 ASR

提供的 [asr/transcribe.py](asr/transcribe.py) 封装了 Apple Silicon 上的 MLX Whisper。在安装目录的 `asr/venv` 创建 Python 3.12 虚拟环境，安装 [asr/requirements.lock](asr/requirements.lock)，再在联网时用该解释器运行一次 [asr/download_model.py](asr/download_model.py)。它下载固定修订版 `1e3e249fb8d01c655324bd6841b1deadffd6d04c` 的 `mlx-community/whisper-base-mlx`。在 `asr/models/whisper-base-mlx` 下用 [asr/model-sha256.txt](asr/model-sha256.txt) 校验权重，例如运行 `shasum -a 256 -c ../../model-sha256.txt`。随后模型加载和转写均离线运行；解码后的 PCM 直接传给 MLX，因此不需要 ffmpeg。下载保留模型上游许可和元数据。依赖锁记录了测试环境；模型权重和 Python 软件包不提交到仓库。

将 `asr.command` 设置为 `asr/venv/bin/python` 的绝对路径，将 `asr.args` 设置为 `["/absolute/install/asr/transcribe.py", "{input}", "{output}"]`，同时设置 `output: "file"` 和 `timeoutMs: 60000`。脚本默认转写中文，允许通过 `--language` 和 `--model` 覆盖，并可能输出繁体字。完全数字静音产生空转写；连接器拒绝此结果，不联系 Grok。真实麦克风识别准确率取决于录音质量。MLX 需要原生 Metal 权限，隔离命令沙箱可能不提供该权限。

也可以使用 whisper.cpp 等其他程序，参数占位符为 `{input}`（私有临时 WAV）、`{output}`（预期 UTF-8 转写文件）和 `{outputBase}`（同一路径去掉 `.txt`）。参数直接传递，不经过 shell。仅当 stdout 只含转写文本时使用 `output: "stdout"`。默认超时为 120 秒，最大 300 秒。成功或失败后均删除临时音频和转写文件。进程中断或突然断电可能在系统临时目录留下残留；维护时可清理过期的 `ai-passport-asr-*` 目录。语音任务仅驻留内存，服务重启会丢失；设备事件及单调递增游标则持久保存。

来源：[MLX Whisper](https://github.com/ml-explore/mlx-examples/tree/main/whisper)、[mlx-whisper 软件包](https://pypi.org/project/mlx-whisper/)、[固定模型修订版](https://huggingface.co/mlx-community/whisper-base-mlx/tree/1e3e249fb8d01c655324bd6841b1deadffd6d04c)。

## 设备协议

所有 `/v1/*` 请求必须携带 `Authorization: Bearer DEVICE_TOKEN` 和允许列表中的 `bot_id`。其他 Bot ID 会被拒绝。设备凭据与 MCP 令牌相互独立。

| 请求 | 行为 |
| --- | --- |
| `POST /v1/voice?bot_id=ID` | 正文必须恰为 160000 字节 PCM：五秒、16000 Hz、单声道、小端有符号 16 位。Content-Type 必须为 `audio/pcm;rate=16000;channels=1;format=s16le`，Content-Length 必须为 `160000`。拒绝分块上传。ASR/webhook 处理前返回 `202 {"request_id":"..."}`。 |
| `POST /v1/voice?bot_id=ID`(按住说话) | Content-Type `audio/ima-adpcm;rate=8000;channels=1`,Content-Length `20004`:20000 字节 8 kHz IMA ADPCM(低半字节在前,不足补零)+ 小端 uint32 采样数(2400–40000,即 0.3–5 秒)。设备在用户按住 OK 时边录边传。解码并升采样到 16 kHz 后送 ASR。返回 `202`。 |
| `GET /v1/events?bot_id=ID&after=0` | 在当前非零持久游标处初始化，跳过历史事件。之后轮询使用返回的游标。 |
| `GET /v1/events?bot_id=ID&after=N` | 等待期间返回 `{"cursor":N,"events":[]}`，否则最多返回一个事件及其游标。仅在成功响应被消费后保存、更新游标。响应丢失时可使用之前的游标重试。过期或空队列可以不返回事件而直接推进游标。 |

事件格式为 `{"id":N,"type":"text","text":"..."}` 或 `{"id":N,"type":"action","action":"wave","text":"..."}`，动作文字可省略。每个文字字段最多 191 个 UTF-8 字节；较长工具回复会拆分，不截断 Unicode 码点。响应小于 4096 字节。文字卡片展示时间至少五秒，或每个码点 250 毫秒，以较长者为准。动作保留目录中预估时长加 0.3 秒，且至少展示附带文字所需的时长，防止后续事件中断动画或滚动文字。设备渲染可能延迟或失败；连接器不声称已收到物理播放确认。所配置 Bot 仅支持一个设备消费者；新的 `after=0` 初始化会跳过该消费者的待处理事件。

事件五分钟后过期，持久队列最多保存 64 张卡片或动作。超出上限的新工具调用返回 `event_queue_full`，不会静默丢弃。游标零为保留值；uint32 用尽时拒绝继续，需要协调迁移设备与状态。重新安装时保留 `events.json`，设备仍保存旧游标时不得重置或删除该文件。

同时只处理一个语音任务，语音任务总数最多三个。每分钟限额为六次语音上传、300 次事件轮询、120 次 MCP 请求和 120 次健康检查。HTTP 请求接收限时 15 秒，标头限时五秒，连接最多 32 个。MCP 正文最多 32768 字节；ASR 输出最多 65536 字节和 4000 个字符。Webhook 请求超时 20 秒，并禁止重定向。由于请求可能已触发 Routine，连接器不会自动重试 webhook。异步 ASR/webhook 失败会生成安全的设备错误卡片；日志和 `lynn_status` 包含生成的请求 ID 以便关联。关闭服务时取消正在进行的语音处理。

Routine webhook 接收 Bearer 认证的 JSON POST，字段为 `text`、`request_id`、`bot_id`、`bot_name`；它必须将 `text` 作为用户消息，可将其他字段作为上下文。Webhook 必须返回 HTTP 200。Routine 必须调用已注册的工具；仅接受 webhook 不会将 Bot 回复送到设备。

## MCP 和验证

无状态 JSON Streamable HTTP 接口支持 `initialize`、`ping`、`tools/list`、`tools/call` 和通知。它可协商 MCP `2025-03-26`、`2025-06-18` 或 `2025-11-25`；后续请求可提供 `MCP-Protocol-Version`。POST 客户端的 Accept 必须同时声明 `application/json` 和 `text/event-stream`。GET 和 DELETE 返回 405；服务使用 JSON 响应，不打开 SSE 流或会话。认证使用独立配置的 Bearer 工具令牌，不提供交互式 OAuth 发现。传输遵循 [MCP 官方规范](https://modelcontextprotocol.io/specification/2025-06-18/basic/transports)。

`tools/list` 将所有 41 个动作公开为 `lynn_action_<id>`，附带目录描述和可选 `text`，另有 `lynn_say`、`lynn_list_actions`、`lynn_status` 及下文三个记忆和上下文工具，共计 47 个工具。工具回复表示已排队，而非动画已完成。每个文字参数最多 4000 个字符。动作 ID 和时长直接来自既有固件目录，不另行手写第二份列表。

运行 `cd connector && npm test`。测试涵盖所有 41 个工具、令牌隔离、Host/Origin 检查、Bot/游标校验、正文大小限制、异步语音分发、webhook 失败、伪 ASR 子进程及超时、UTF-8 卡片、持久游标、过期和节奏控制。回环集成测试需要本机监听权限。测试不证明生产隧道、Grok Routine 或物理设备有效；必须使用真实五秒录音和经工具送达的回复验证完整链路后，才能声称设备验收通过。

## 通信参考

入站通信使用[官方 Grok Bot Routine Webhook](https://docs.x.ai/grok-bot/skills-routines-and-automations#webhooks)。社区项目 [rthomas24/grok-bot-mcp](https://github.com/rthomas24/grok-bot-mcp) 与 [Kargatharaakash/grok-bot-mcp](https://github.com/Kargatharaakash/grok-bot-mcp) 分别提供桌面自动化与后端接入参考。这些项目仍较新，不能作为生产成熟度的证据。本连接器不依赖它们的界面自动化、私有接口或桌面账户凭据提取。

## 连续语音对话与记忆

1.1 版将用户转写和工具回传的完整助手回复保存到私密 `state/conversation.json`（目录 0700，原子写入文件 0600）。重启、重新安装以及设备事件队列初始化均保留记录；屏幕队列不再充当会话档案。音频仍为临时文件。不会导入已有 Grok 原生聊天记录或此前设备对话。

每次语音 Webhook 附带固定 `conversation_id`、当前 `request_id`、原始 `user_text`、最近问答 `context`、滚动摘录 `summary` 与已保存的 `memories`。`text` 字段也包含这些上下文和当前话语，因此只读取 `text` 的旧 routine 仍能衔接对话。这些字段属于应用协议，不代表选择了 Grok 原生会话。历史是引用数据，不能覆盖 Bot 规则。仅记录工具实际回传的文本回复，不把 Webhook 接受或错误提示当成助手回答。

回复时通过 `lynn_say` 或任意 `lynn_action_*` 同时传入 `text` 与 Webhook 的 `request_id`。升级后刷新 MCP 工具发现。存在待回复语音时，不带关联 ID 的调用返回 `request_id_required`，需携带原 ID 重试。未知、过期或有歧义的请求会失败，避免归档到错误轮次。同一请求的相同文本不会重复入队。没有待处理语音时，手动调用仍可使用，但不会虚构用户轮次。分段回复继续使用同一请求 ID。缓存中的旧版纯文本工具可发送 `[request_id=ID]回复`，连接器会在显示与保存前去除该明确路由前缀。

新增工具为 `lynn_context`（读取最近问答、滚动摘录和记忆）、`lynn_memory_search`（可选 `query`，留空列出已保存条目）、`lynn_remember`（`key` 和 `text`，相同 key 替换）。只记住用户明确提供的事实与偏好。这些工具补充 Bot 自身记忆，不读取 Grok 内部记忆库。

保存最多 48 轮完整问答，Webhook 最近上下文最多 12 轮，滚动摘录最多 4000 字符，长期条目最多 32 条、每条 1000 字符。Webhook 快照上限为 20000 个 JSON 字符。摘录只是旧对话的有界引用，不是模型推断或生成的摘要；更早摘录会逐渐淘汰，因此并非无限历史。待回复请求十分钟过期。如果 48 个未过期、未完成请求占满存储，新请求会失败，而非覆盖活跃上下文。状态文件损坏或 Bot ID 不匹配会阻止启动，应恢复私密状态，不能悄悄重置。需要保留连续性时，请将此文件纳入本机备份并保持私密。

验收方式：告诉 Lynn 一个临时事实，不重复该事实追问；重启连接器后再次追问；初始化设备事件游标，确认上下文仍存在。并发语音必须按各自 ID 保存回复。单元和集成测试覆盖这些边界；实体屏幕、麦克风仍需单独验证。
