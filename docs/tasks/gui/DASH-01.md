# DASH-01 · Dashboard 只读快照层

状态：agent 快照层完成，Windows 定向验证通过。
设计：[Dashboard](../../design/gui/dashboard.md)。

## 已完成

- `DashboardService` 与 ControlService/JSON-RPC 提供 `dash.overview/snapshot/events/thread`，
  schema 1，严格校验 envelope、字段、选择器、整数和范围。
- 聚合既有 DiagnosticState（含 futex、Java 栈、monitor、pacer）、McpSessionControl、
  Logger、CapabilityLedger，以及注入的 GPU/VFS IO/AudioTrack 强类型快照。
- section 明确 complete/partial/unavailable、采集时间、generation 和失败原因；忙碌与
  未连接返回 null。新增日志、账本、会话 try-lock 入口，不推进 guest、不消费命令、不写盘。
- 固定容量事件环默认 4096，支持独立游标、过滤、翻页、覆盖 gap/dropped、来源环丢失计数；
  当前支持 syscall/native/dexvm/lifecycle，保留 source_sequence，不推测缺失 frame/时间。
- thread 通过 guest_tid/context_token 关联执行、Java 栈、native/syscall 和 monitor/futex；
  pacer/lifecycle 为共享状态，不推测 futex owner。
- 集合、日志、栈、事件页及响应体限量；请求竞争立即失败，不等待 VM 执行锁。

## 验证

- `cmake --build --preset windows-msvc --config Release --target ogplay_tests` 通过。
- `ogplay_tests --source-file=*dashboard_tests.cpp,*control_service_tests.cpp,*stall_diagnostics_tests.cpp,*mcp_protocol_tests.cpp,*logger_tests.cpp,*capability_ledger_tests.cpp`
  通过：56 用例、1039 断言。
- 覆盖闭合 schema、来源缺失/忙碌/异常、只读性、线程关联、栈/日志限量、UTF-8、64 位整数、
  事件去重/翻页/过滤/覆盖，以及竞争请求在 100 ms 内返回。

## 后续边界

- 本阶段是传输无关 agent 接口；默认 ControlService 只装配 logger/ledger。
  运行进程来源生命周期装配、HTTP RPC/静态路由、Web 页面与真实会话验证见 [DASH-03](DASH-03.md)。
- GPU/VFS/AudioTrack 回调只能用 atomic/有界 try-lock，不能直接接可能等待的普通快照方法。
  GPU 目前为统计、VFS 为 IO 计数、Audio 为 AudioTrack，均标 partial；新计数/快照归 DASH-02。
- GC/GLES error/capability miss/audio underrun/VFS flush 的统一事件发布尚未接入；
  查询它们不会伪造事件，响应列出 supported_kinds。
- 所有来源必须比 DashboardService 存活更久；销毁/重建服务后客户端从游标 0 重连。
