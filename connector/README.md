<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Lynn Mac connector

The connector takes a five-second device recording, transcribes it locally on the Mac, and triggers the selected Grok Bot Routine webhook. Grok delivers its reply and animations by calling the connector's MCP tools. A webhook HTTP 200 means accepted; its response body is never treated as a reply.

Requires Node 22+, Python 3.12 for the supplied Apple Silicon ASR, a public HTTPS tunnel, and a Grok Bot Routine with an official webhook plus authenticated Streamable HTTP MCP support. The device needs Internet access to the tunnel. The Mac must be awake and signed in for its user LaunchAgent to run. There are no Node package dependencies.

## Configure and install

1. Copy [config.example.json](config.example.json) to a private location outside Git and set permissions to `0600`. Generate independent device and tool tokens with `openssl rand -hex 32` twice. Set the selected Bot ID, name, exact public tunnel hostname, local ASR executable/arguments and state directory. Use an absolute state directory, or `state` relative to the installed configuration. The default port is `18787`. Never put secrets in terminal command arguments or tracked files.
2. Set `webhookURL` to the official Routine webhook URL and `webhookKey` to its Bearer key. These two settings can both be omitted during setup; authenticated voice uploads then return `503 not_ready`, while MCP remains available. The supplied example URL is a placeholder, not a Grok API endpoint.
3. Provision ASR as described below, then install the runtime and user LaunchAgent:

   ```bash
   python3 connector/install-macos.py --config /absolute/private/config.json --node /absolute/path/to/node
   ```

   Add `--no-load` to prepare files without starting the agent. The helper validates the configuration and installs to `~/Library/Application Support/AI Passport Connector`, retaining any existing ASR models and virtual environment. It copies the single action catalog from [docs/lynn-tools.json](../docs/lynn-tools.json), writes private `config.json` separately, and creates `~/Library/LaunchAgents/com.aipassport.lynn-connector.plist`. It uses the current user's home directory and UID and requires no `sudo`. Reinstall after changing code or configuration. ASR executable and argument paths must be absolute; `~` and shell variables in JSON are not expanded. Configuration changes require restarting the service.
4. Route only the intended HTTPS hostname through the tunnel to `http://127.0.0.1:18787`. Keep the `publicHost` configuration aligned with the tunnel hostname. The connector always binds to `127.0.0.1`; it rejects unknown Host and Origin values. Tunnel authentication must permit the independently authenticated device and Grok client to reach these routes.
5. In the selected Grok Bot's MCP settings, register `https://YOUR_HOST/mcp` using Bearer authentication with `toolToken`. Provision the device with `https://YOUR_HOST`, `botId` and `deviceToken`. Add these Bot instructions:

   > You are Lynn. Respond to the user's message by calling lynn_say or a lynn_action_* tool with text. Normal chat output does not reach the device. Use the action descriptions to choose appropriate animations. Do not repeat a tool call after it has reported queued unless another delivery is intended.

Inspect the service with `launchctl print gui/$(id -u)/com.aipassport.lynn-connector`. Stop it with `launchctl bootout gui/$(id -u)/com.aipassport.lynn-connector`; the plist and private state remain available for a later bootstrap. Logs contain operation types and generated request IDs, not credentials, audio, transcripts, webhook URLs or upstream error bodies. Logs are private files under the installation's `logs/`; rotate them as part of normal Mac maintenance. `GET /healthz` returns only `{"ok":true}` and confirms process health, not Grok or device connectivity.

## Local ASR

The supplied [asr/transcribe.py](asr/transcribe.py) wraps MLX Whisper on Apple Silicon. Create a Python 3.12 virtual environment at the installation's `asr/venv`, install [asr/requirements.lock](asr/requirements.lock), and run [asr/download_model.py](asr/download_model.py) with that interpreter once while online. It downloads `mlx-community/whisper-base-mlx` at the pinned revision `1e3e249fb8d01c655324bd6841b1deadffd6d04c`. Verify weights using [asr/model-sha256.txt](asr/model-sha256.txt) from `asr/models/whisper-base-mlx` (for example, `shasum -a 256 -c ../../model-sha256.txt`). Model loading and transcription then run offline; decoded PCM is passed directly to MLX, so ffmpeg is unnecessary. The model's upstream license and metadata are retained by the download. The dependency lock records the tested environment; model weights and Python packages are not committed.

Set `asr.command` to the absolute `asr/venv/bin/python` path and `asr.args` to `["/absolute/install/asr/transcribe.py", "{input}", "{output}"]`, with `output: "file"` and `timeoutMs: 60000`. The wrapper defaults to Chinese transcription, permits `--language` and `--model` overrides, and can emit traditional characters. Exact digital silence produces an empty transcript, which the connector rejects without contacting Grok. Real microphone accuracy depends on recording quality. MLX needs native Metal access; an isolated command sandbox may lack it.

An alternative executable such as whisper.cpp can use `{input}` (private temporary WAV), `{output}` (expected UTF-8 transcript file), and `{outputBase}` (the same path without `.txt`). Arguments are passed directly without a shell. Set `output: "stdout"` only if stdout contains transcript text alone. The default timeout is 120 seconds; the maximum is 300 seconds. Temporary audio/transcript files are deleted after success or failure. A process interruption or abrupt power loss can leave a temporary directory in the system temp folder; remove stale `ai-passport-asr-*` directories during maintenance. Voice jobs are in memory and are lost across a service restart; device events and their monotonic cursor persist.

Sources: [MLX Whisper](https://github.com/ml-explore/mlx-examples/tree/main/whisper), [mlx-whisper package](https://pypi.org/project/mlx-whisper/), [pinned model](https://huggingface.co/mlx-community/whisper-base-mlx/tree/1e3e249fb8d01c655324bd6841b1deadffd6d04c).

## Device protocol

All `/v1/*` requests require `Authorization: Bearer DEVICE_TOKEN` and the allowlisted `bot_id`. Another Bot ID is rejected. Credentials are independent of the MCP token.

| Request | Behavior |
| --- | --- |
| `POST /v1/voice?bot_id=ID` | Exactly 160000 PCM bytes: five seconds, 16000 Hz, mono, signed little-endian 16-bit. Content-Type must be `audio/pcm;rate=16000;channels=1;format=s16le` and Content-Length must be `160000`. Chunked uploads are rejected. Returns `202 {"request_id":"..."}` before ASR/webhook processing. |
| `POST /v1/voice?bot_id=ID` (push-to-talk) | Content-Type `audio/ima-adpcm;rate=8000;channels=1`, Content-Length `20004`: 20000 bytes of 8 kHz IMA ADPCM (low nibble first, zero padded) followed by a little-endian uint32 sample count (2400–40000, 0.3–5 s). The device streams it while the user holds OK. Decoded and upsampled to 16 kHz before ASR. Returns `202`. |
| `GET /v1/events?bot_id=ID&after=0` | Initializes at the current nonzero durable cursor and skips historical events. Use the returned cursor for later polls. |
| `GET /v1/events?bot_id=ID&after=N` | Returns `{"cursor":N,"events":[]}` while waiting, or at most one event and its cursor. Persist/update the cursor only after consuming a successful response. A lost response can be retried with the preceding cursor. An expired/empty queue can advance the cursor without an event. |

Events are `{"id":N,"type":"text","text":"..."}` or `{"id":N,"type":"action","action":"wave","text":"..."}`; action text is optional. Every text field is at most 191 UTF-8 bytes; longer tool replies are split without cutting Unicode code points. Responses stay below 4096 bytes. Text cards hold for at least five seconds or 250 milliseconds per code point, whichever is longer. Actions hold for the catalog's estimated duration plus 0.3 seconds and at least as long as any attached text, preventing subsequent deliveries from interrupting an animation or scrolling text. Device rendering can lag or fail; the connector does not claim a physical playback acknowledgement. Only one device consumer is supported for the configured Bot; a new `after=0` initialization skips pending events for that consumer.

Events expire after five minutes and the durable queue is bounded to 64 cards/actions. New tool calls fail with `event_queue_full` when they would exceed the bound; nothing is silently dropped. Cursor zero is reserved; uint32 exhaustion fails closed and requires a coordinated device/state migration. Preserve `events.json` across reinstalls and never reset/delete it while a device retains an old cursor.

There is one active voice job and at most three total voice jobs. Limits per minute are six voice uploads, 300 event polls, 120 MCP requests and 120 health checks. HTTP requests have a 15-second receive limit and a 5-second header limit, with at most 32 connections. MCP bodies are bounded to 32768 bytes; ASR output is bounded to 65536 bytes and 4000 characters. Webhook requests use a 20-second timeout and prohibit redirects. There are no automatic webhook retries because acceptance may already have triggered a Routine. Asynchronous ASR/webhook failures generate a safe device error card; logs and `lynn_status` include a generated request ID for correlation. In-flight voice work is cancelled on service shutdown.

The Routine webhook receives a Bearer-authenticated JSON POST with `text`, `request_id`, `bot_id` and `bot_name`; it must use `text` as the user message and can use the other fields as context. The webhook must return HTTP 200. Set the Routine up to invoke the registered tools; acceptance alone cannot deliver a bot reply.

## MCP and verification

The stateless JSON Streamable HTTP endpoint supports `initialize`, `ping`, `tools/list`, `tools/call` and notifications. It negotiates MCP `2025-03-26`, `2025-06-18` or `2025-11-25`; subsequent requests can supply `MCP-Protocol-Version`. POST clients must advertise both `application/json` and `text/event-stream` in Accept. GET and DELETE return 405; this server uses JSON responses and does not open SSE streams or sessions. Authentication uses the independently provisioned Bearer tool token; interactive OAuth discovery is not provided. The transport follows the [official MCP specification](https://modelcontextprotocol.io/specification/2025-06-18/basic/transports).

`tools/list` exposes all 41 actions as `lynn_action_<id>`, with their catalog descriptions and optional `text`, plus `lynn_say`, `lynn_list_actions`, `lynn_status` and the three memory/context tools described below: 47 tools in total. Tool replies report queued delivery rather than completed animation. Each text argument is bounded to 4000 characters. Action IDs and durations come directly from the existing firmware catalog instead of a second handwritten list.

Run `cd connector && npm test`. Tests cover all 41 tools, token separation, Host/Origin checks, Bot/cursor validation, body limits, asynchronous voice dispatch, webhook failures, fake ASR subprocesses/timeouts, UTF-8 cards, persistent cursors, expiry and pacing. Loopback integration tests require permission to listen on the local machine. They do not prove the production tunnel, Grok Routine or a physical device works; verify that full path with an actual five-second recording and a tool-delivered reply before claiming device acceptance.

## Communication references

The [official Grok Bot Routine Webhook](https://docs.x.ai/grok-bot/skills-routines-and-automations#webhooks) is the inbound integration used here. Community projects [rthomas24/grok-bot-mcp](https://github.com/rthomas24/grok-bot-mcp) and [Kargatharaakash/grok-bot-mcp](https://github.com/Kargatharaakash/grok-bot-mcp) provide useful examples of desktop automation and backend access, respectively. They are young projects and are not production-maturity evidence. This connector does not depend on their UI automation, private endpoints, or extraction of desktop account credentials.

## Continuous voice conversation and memory

Version 1.1 stores the user transcript and full tool-delivered assistant reply in private `state/conversation.json` (0700 directory, 0600 atomic file). It survives service restarts, reinstallations and device event-queue initialization; the display queue is not the conversation archive. Audio is still temporary. Existing native Grok chat history and old device conversations are not imported.

Each voice webhook includes stable `conversation_id`, the current `request_id`, raw `user_text`, recent `context`, a rolling excerpt `summary` and saved `memories`. The `text` field also includes this context and current utterance, so routines that only read `text` can continue a conversation. These are application fields, not Grok-native session selection. History is quoted data; it cannot override Bot rules. Only received tool replies are recorded as replies, not webhook acceptance or error cards.

Reply using `lynn_say` or any `lynn_action_*` with `text` and the webhook's `request_id`. Refresh MCP discovery after upgrading. An uncorrelated reply while a voice request is pending returns `request_id_required`; retry with the original ID. Unknown, expired or ambiguous IDs fail instead of attaching text to a different turn. Identical text for the same request is not re-enqueued. Manual tool use without an outstanding voice request still works but does not create an invented user turn. Extra reply parts must include the same request ID. A cached text-only tool can instead send `[request_id=ID]reply`; the connector strips the exact routing prefix before displaying or saving the reply.

The additional tools are `lynn_context` (recent conversation, excerpt summary and memories), `lynn_memory_search` (`query`, optional; empty lists saved entries), and `lynn_remember` (`key`, `text`, replacing the same key). Remember only user-stated facts/preferences. These tools supplement the Bot's native memory; they do not access Grok's internal memory store.

Storage is bounded to 48 full turns, 12 recent turns in the webhook, a 4000-character rolling excerpt and 32 memory entries of up to 1000 characters each. The webhook snapshot is capped at 20000 JSON characters. The excerpt is a bounded quotation of older turns, not an inferred or model-generated summary; older excerpts eventually roll out. The full history is therefore not unlimited. Outstanding replies expire after ten minutes. If 48 fresh unresolved turns exhaust storage, new dispatch fails rather than overwriting active context. State corruption or a mismatched Bot ID prevents startup; restore the private state instead of silently resetting it. Keep this conversation file private and include it in local backups if continuity is required.

Acceptance checks: tell Lynn a temporary fact, ask a follow-up without repeating it, restart the connector and repeat the follow-up; initialize the device event cursor and verify context still remains. Concurrent voice requests must keep separate IDs and replies. Unit/integration tests cover these boundaries; physical screen/microphone checks remain separate.
