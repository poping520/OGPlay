# 模块：agent

## 职责

把内核结构化状态暴露为无传输依赖的 Control Service；后续 JSON-RPC/TCP/UDS/MCP
适配器只负责协议编解码。

## 公共 API

- `ogplay::agent::ControlService::Request`：处理只读的 M0 方法。
- M1 起补 session/run/sym/hle/gpu/frame/fs/mem/cpu/log 分组。

## 不变量

- 返回结构化结果，不返回供正则刮取的自由文本。
- 查询与副作用操作分开；未知方法明确返回错误。
- 调试接口与 CI 断言读取同一份状态。

## 禁止

- 不直接依赖 TCP/平台 socket 或前端。
- 不以环境变量增加一次性调试开关。

## 测试

`tests/agent/` 的方法分派和错误契约测试。

