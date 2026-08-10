# 模块：agent

## 职责

把内核结构化状态暴露为无传输依赖的 Control Service；后续 JSON-RPC/TCP/UDS/MCP
适配器只负责协议编解码。

## 公共 API

- `ControlService::Request`：session/run/sym/hle/log/gpu 的传输无关分派。
- `JsonRpcAdapter::Handle`：逐行 JSON-RPC 2.0 编解码，可由 stdio/TCP/UDS 共用。
- `FrameSnapshotStore`：以移动所有权保留最近一次已呈现 RGBA8 guest frame；发布新帧时
  返回旧缓冲供前端回收，读取时只按请求复制，不在每帧编码或复制截图。
- `McpProtocolAdapter::Handle`：实现 MCP initialize/ping/tools/list/tools/call 最小协议面；
  `frame_capture` 只读工具缺省以 quality 85 编码为 MCP `image/jpeg`，也接受显式
  `format: "png"`，并返回精确格式、序号和尺寸；`click` 以最近帧的 guest 整数像素坐标
  排队一次主指针 tap。
- `McpInputQueue`：跨 MCP worker 与 guest 主线程传递最多 64 个 click；每个 click 以同一请求
  序号在连续两次 take 中输出 down/up，网络线程不直接调用 guest。
- 协议编解码只通过 core `JsonDocument`/`JsonWriter`；MCP initialize 与工具 schema 按
  JSON-RPC 对象作用域验证，不扫描嵌套文本。
- `gpu.stats/render_targets/capabilities/trace`：从可选 `GpuStateProvider` 序列化强类型
  快照；未连接 provider 明确失败，trace 限额为 1..1000。
- M6 增加 frame/fs/mem/cpu 分组。

## 不变量

- 返回结构化结果，不返回供正则刮取的自由文本。
- 查询与副作用操作分开；未知方法明确返回错误。
- MCP 截图不得推进 guest、消费输入或伪造无帧成功；无最近帧时返回显式 tool error。
- MCP click 必须在最近帧边界内、参数完整且队列可用时才确认排队；down/up 不得在同一次
  take 合并，队列满、无帧、负数、越界、未知字段和未接输入均返回显式 tool error。
- MCP 图像最大 64 MiB RGBA8，尺寸、字节数、JPEG/PNG/Base64 输出必须在发布前完整受检；
  两种编码均使用仓库固定 commit 的官方 `stb_image_write`，禁止退回 stored-block PNG。
- 调试接口与 CI 断言读取同一份状态。
- `sym.resolve` 使用 core 的 provider；`hle.unimplemented/null_calls` 直接读取运行时账本。
- `gpu.*` 不接受 provider 生成的 JSON；过滤与限额经结构化参数传入同一快照接口。
- 协议错误使用 JSON-RPC 标准错误码；内核状态错误放在 server error 范围。
- JSON 语法错误与无效 request 分别映射 -32700/-32600；重复键、超限和错误类型明确失败。

## 禁止

- 不直接依赖 TCP/平台 socket 或前端。
- 不以环境变量增加一次性调试开关。

## 测试

`tests/agent/` 的方法分派和错误契约测试。
