# 模块：agent

## 职责

把内核结构化状态暴露为无传输依赖的 Control Service；后续 JSON-RPC/TCP/UDS/MCP
适配器只负责协议编解码。

## 公共 API

- `ControlService::Request`：session/run/sym/hle/log/gpu 的传输无关分派。
- `DashboardService::Request` / `ControlService::RequestDashboard`：只读 `dash.overview`、
  `dash.snapshot {sections?}`、`dash.events {since_sequence, limit?, kinds?}`、
  `dash.thread {guest_tid}`；JSON-RPC 支持同名分派，envelope/params 闭合校验。
  `DashboardSources` 注入结构化来源，必须比服务存活更久；回调必须 atomic/有界 try-lock，
  不允许执行 guest、写盘或调用会等待锁的普通 GPU/AudioTrack 快照。
  默认 ControlService 只连接 logger/ledger；运行进程装配与 HTTP 路由由 DASH-03 接入。
- `JsonRpcAdapter::Handle`：逐行 JSON-RPC 2.0 编解码，可由 stdio/TCP/UDS 共用。
  支持同步 `RequestHandler` 注入供独立 GUI 复用协议封装；JSON view 只在调用期间有效，
  注入模式拒绝额外 envelope 字段，不依赖前端或创建运行时 session。
- `FrameSnapshotStore`：以移动所有权保留最近一次已呈现 RGBA8 guest frame；发布新帧时
  返回旧缓冲供前端回收，读取时只按请求复制，不在每帧编码或复制截图。
- `McpProtocolAdapter::Handle`：实现 MCP initialize/ping/tools/list/tools/call 最小协议面；
  `frame_capture` 只读工具缺省以 quality 85 编码为 MCP `image/jpeg`，也接受显式
  `format: "png"`；可选 `overlay: "coordinates"` 在截图副本上绘制 guest 像素网格，并返回
  精确格式、网格状态、序号和尺寸；`click` 排队一次 tap；`swipe` 以最近帧的 guest 整数
  像素端点和 1..120 个 motion 步数排队一次确定性主指针滑动。
- `DrawCoordinateOverlay`：在严格匹配尺寸的 RGBA8 缓冲上绘制每 100 px 主线、每 25 px
  边缘刻度和顶部/左侧坐标标签，供截图编码前的临时副本使用。
- `McpInputQueue`：跨 MCP worker 与 guest 主线程传递最多 64 个 pointer gesture；click 在连续
  两次 take 中输出 down/up，swipe 按请求的 1..120 个确定性步数输出 down/move/up，整个手势
  保持同一请求与起始帧序号，网络线程不直接调用 guest。
- `McpSessionControl`：跨 MCP worker 与 guest 主线程交换原子 lifecycle/frame/ticks/
  presented-frame/movie/process-exit/guest-fault/shutdown 快照，并以最多 64 项 FIFO 传递
  step/suspend/resume/shutdown 命令；`step` 只接受 1..1,000,000 帧。
- MCP `session_state` 只读同一份原子状态；`step`、`lifecycle`、`shutdown` 只确认命令排队，
  返回请求序号和起始 frame，不把排队伪作 guest 已执行。
- MCP `diag.snapshot` 调用注入的有界快照 handler 并返回 JSON 路径；未启用或超时明确失败，
  它不是主循环停滞时唯一触发路径。
- 协议编解码只通过 core `JsonDocument`/`JsonWriter`；MCP initialize 与工具 schema 按
  JSON-RPC 对象作用域验证，不扫描嵌套文本。
- `gpu.stats/render_targets/capabilities/trace`：从可选 `GpuStateProvider` 序列化强类型
  快照；capability limit 保留 provider 的有符号 64 位值，未连接 provider 明确失败，
  trace 限额为 1..1000。
- M6 增加 frame/fs/mem/cpu 分组。

## 不变量

- 返回结构化结果，不返回供正则刮取的自由文本。
- 查询与副作用操作分开；未知方法明确返回错误。
- MCP 截图不得推进 guest、消费输入或伪造无帧成功；无最近帧时返回显式 tool error。
- 坐标网格不得改变截图尺寸或写回 `FrameSnapshotStore`；省略 `overlay` 必须保持干净截图，
  未知 overlay 和未知字段必须明确失败。
- MCP pointer gesture 的每个 down/move/up 阶段必须由独立 take 取得；整数插值包含精确终点，
  单个 swipe 不得超过 120 个 motion 阶段，待处理手势不得超过 64 个。
- MCP click 必须在最近帧边界内、参数完整且队列可用时才确认排队；队列满、无帧、负数、
  越界、未知字段和未接输入均返回显式 tool error。
- MCP swipe 的起点和终点必须不同且都在最近帧边界内；五个参数完整、步数受限且队列可用时
  才确认排队，响应必须返回请求序号、起始帧序号、端点和步数。
- MCP session 工具的 input/output schema 必须 closed；未连接控制面、命令队列满、额外字段、
  非法 lifecycle 或越界 step 明确失败，网络线程禁止直接进入 guest lifecycle。
- MCP 图像最大 64 MiB RGBA8，尺寸、字节数、JPEG/PNG/Base64 输出必须在发布前完整受检；
  两种编码均使用仓库固定 commit 的官方 `stb_image_write`，禁止退回 stored-block PNG。
- 调试接口与 CI 断言读取同一份状态。
- Dashboard schema 1 的 section 为 session/diagnostics/gpu/vfs/audio/capabilities/log，携带
  status/captured_at_steady_ns/generation；未连接、忙碌、异常为 unavailable + null，部分来源
  与截断为 partial。无来源 generation 沿用诊断约定 0，不冒充 frame。overview 保留 session
  顶栏数据，其余只给状态；thread 用 guest_tid/context_token 关联，pacer/lifecycle 为共享状态。
- Dashboard 事件环默认 4096（上限 4096），每次最多 1000；sequence 是服务观察顺序，
  source_sequence 保留来源序号。支持 syscall/native/dexvm/lifecycle；缺 frame/steady_ns
  返回 null，其余事件类别不伪造。next_sequence 用于独立客户端续读；未来游标拒绝，覆盖
  返回 gap/dropped，来源环丢失另计并保留 source section 状态。服务重建须从游标 0 开始。
- Dashboard 响应最大 1 MiB；常规集合最多 128、事件最多 256、Java 栈最多 32 层；
  日志尾部最多 128 条/每条 32 fields/文本 512 bytes。超预算显式 -32003，竞争请求立即
  -32002，禁止等待 VM 执行锁；不承诺跨 section 原子快照。
- `sym.resolve` 使用 core 的 provider；`hle.unimplemented/null_calls` 直接读取运行时账本。
- `gpu.*` 不接受 provider 生成的 JSON；过滤与限额经结构化参数传入同一快照接口。
- 协议错误使用 JSON-RPC 标准错误码；内核状态错误放在 server error 范围。
- JSON 语法错误与无效 request 分别映射 -32700/-32600；重复键、超限和错误类型明确失败。

## 禁止

- 不直接依赖 TCP/平台 socket 或前端。
- 不以环境变量增加一次性调试开关。

## 测试

`tests/agent/` 的方法分派和错误契约测试。
