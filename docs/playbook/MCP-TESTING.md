# AI 测试：MCP 工具

AI 直接驱动一个真实 `run-apk` 会话：读画面、注入触摸、步进帧、读结构化状态。
适合探索与定位。要把结论固化成可复跑用例，改用
[SCENARIO-RUNNER.md](SCENARIO-RUNNER.md)。长期规划见
[AI 自动化测试](../roadmap/10-ai-automation-testing.md)。

## 启动

```powershell
cmake --build --preset windows-msvc --config Release --target ogplay
build\windows-msvc\Release\ogplay.exe run-apk <apk> --system-dir <api19-lib-dir> --mcp
```

`--mcp` 固定用 `http://127.0.0.1:15971/mcp`；换端口用 `--mcp-port <1..65535>`，
两者互斥。等到终端出现 `OGPlay: MCP ready at ...` 再发请求；端口占用会明确失败。

两种模式：

| 模式 | 加什么 | 行为 | 可用工具 |
| --- | --- | --- | --- |
| 自由运行 | `--mcp` | 游戏按实时节奏跑，窗口可见 | `frame_capture`、`click`、`swipe` |
| 手动步进 | `--mcp` + `--mcp-manual-step` | 窗口隐藏，guest 只在收到 `step` 时前进 | 全部 7 个 |

`--mcp-manual-step` 必须配合 `--mcp`/`--mcp-port`，且 profile 必须是
`gl_surface_view` 或 `dex_activity`。`tools/list` 在两种模式下都列出 7 个工具，
但自由运行时调用会话类工具会明确报错——要确定性就用手动步进。

## 协议

`POST /mcp`，JSON-RPC 2.0，`Content-Type: application/json`，`Accept` 若带则须含
`application/json` 或 `*/*`。服务只绑定回环地址：其他路径 404、非 POST 405、
非回环 `Origin` 403。顺序为
`initialize`（需 `protocolVersion` + `capabilities` + `clientInfo`）→
`notifications/initialized` → `tools/call`。协议版本 `2025-11-25`，也接受
`2025-06-18` / `2025-03-26`。

## 工具

| 工具 | 参数 | 返回（`structuredContent`） |
| --- | --- | --- |
| `frame_capture` | `format` `jpeg`(默认,q85)\|`png`；`overlay` `coordinates` | 图片 + `sequence`/`width`/`height`/`format`/`overlay` |
| `click` | `x`、`y`（最新帧 guest 像素） | `requestSequence`、`frameSequence`、`x`、`y` |
| `swipe` | `startX`、`startY`、`endX`、`endY`、`steps` 1..120 | 同上 + 四个坐标与 `steps` |
| `session_state` | 无 | `lifecycle`、`frame`、`guestTicks`、`presentedFrame`、`movieRequest`、`processExit`、`guestFault`、`shutdownRequested` |
| `step` | `frames` 1..1000000 | `requestSequence`、`startingFrame`、`targetFrame`、`frames` |
| `lifecycle` | `action` `suspend`\|`resume` | `requestSequence`、`startingFrame`、`action` |
| `shutdown` | 无 | 同上，`action` 为 `shutdown` |

`frame_capture` 不推进执行也不消费输入；`lifecycle` 的 `suspend` 期间窗口事件与
步进都停住，`resume` 后继续。

## 手动步进的节奏

三条决定了脚本怎么写：

1. **输入是排队的**，每个 guest step 只派发一个手势阶段。`click` 是 down + up，
   至少要 2 次 step 才走完；`swipe` 是 down + `steps` 段位移 + up，需要
   `steps + 2`。step 给少了手势就停在半路。
2. **`step` 只累加额度，立即返回**，不等执行。要确认进展，读 `session_state` 的
   `frame` 与 `presentedFrame`，别靠 sleep。
3. **每轮循环只取一条会话命令**，所以连发 `step`/`lifecycle` 会按序生效，不会丢。

典型一轮：`frame_capture {"overlay":"coordinates"}` 看坐标 →
`click {"x":400,"y":240}` → `step {"frames":4}` → `session_state` 确认帧号推进 →
`frame_capture {}` 取干净截图比对。

guest 抛异常后会话不会退出：`lifecycle` 变 `failed`、`guestFault` 带原因、步进
额度清零，此时仍可读状态取证，再 `shutdown`。

## 坐标

坐标基于**最新一帧的 guest 像素**，与宿主窗口尺寸、黑边、缩放无关。
`overlay=coordinates` 只画在返回的副本上（每 100 px 主线与标签、每 25 px 边缘
刻度），不改变尺寸，也不影响实时帧。

## 判读纪律

- 帧号推进不等于 UI 动作生效。结论要落在 `session_state` 的结构化字段或截图
  golden 上，不能只凭目测。
- 手工探索不是兼容性结论。要进 gate 的必须是 Scenario 的三轮复跑。
- 收尾用 `shutdown`（或关窗），确认 guest、音频、surface 与 MCP listener 都清理。

## 会明确失败的情形

无首帧就截图或点击、坐标越界、`swipe` 端点相同或 `steps` 越界、输入/命令队列满、
多传参数、自由运行模式下调用会话类工具——全部返回 tool error，不会静默吞掉。
