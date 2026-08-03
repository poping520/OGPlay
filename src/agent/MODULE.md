# 模块：agent

## 职责

把内核结构化状态暴露为无传输依赖的 Control Service；后续 JSON-RPC/TCP/UDS/MCP
适配器只负责协议编解码。

## 公共 API

- `ControlService::Request`：session/run/sym/hle/log 的传输无关分派。
- `JsonRpcAdapter::Handle`：逐行 JSON-RPC 2.0 编解码，可由 stdio/TCP/UDS 共用。
- M1 起补真实 guest session，M4/M6 增加 gpu/frame/fs/mem/cpu 分组。

## 不变量

- 返回结构化结果，不返回供正则刮取的自由文本。
- 查询与副作用操作分开；未知方法明确返回错误。
- 调试接口与 CI 断言读取同一份状态。
- `sym.resolve` 使用 core 的 provider；`hle.unimplemented/null_calls` 直接读取运行时账本。
- 协议错误使用 JSON-RPC 标准错误码；内核状态错误放在 server error 范围。

## 禁止

- 不直接依赖 TCP/平台 socket 或前端。
- 不以环境变量增加一次性调试开关。

## 测试

`tests/agent/` 的方法分派和错误契约测试。
