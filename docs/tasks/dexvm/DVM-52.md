# DVM-52 · DexVM 诊断基建

## 目标（一句话）

为解释器提供默认关闭、容量固定、可筛选的语义 trace，以及在 VM 执行锁安全点读取
全部 execution context Java 栈的 closed-schema 查询面，使后续 opcode、intrinsic、
monitor、native bridge 与 GC 开发能从同一组稳定事实定位问题。

## 交付

1. `InterpreterConfig::diagnostics` 显式配置 trace 容量、事件 mask 与指令采样间隔；
   容量为 0 时关闭，环在构造期一次分配，guest 热路径不分配、不格式化字符串、不保存
   host 指针。容量上限为 1,000,000，指令采样间隔必须非零。
2. 固定记录覆盖 instruction、method enter/exit、exception throw/catch、class init
   begin/end/fail、monitor enter/exit/wait/notify、native enter/exit 与 GC begin/end。
   `value` 只携带该事件的稳定整数 detail（对象/异常/类句柄或失败标记），不得解释成
   host 地址。
3. `Interpreter::Trace(filter, limit)` 在执行锁内复制最近记录，按 sequence 正序返回；
   filter 匹配事件名、class、method 或 descriptor，查询时才把稳定 class/method handle
   解析成文本。limit 受检为 1..10000；JSON schema 固定为 1。
4. `Interpreter::StackSnapshot()` 在执行锁安全点枚举全部 context，并通过
   `VmThreadRuntime` 的 context token 补齐 guest thread id/name/status；每帧输出
   class/method/descriptor/dex pc，pending exception 只输出 guest class descriptor。
   该接口是停界查询：若另一宿主线程正在执行 guest call，调用方等待其释放全 VM
   执行锁；本 WU 不伪造异步抢占或寄存器快照。
5. trace 与 stack 都有独立 JSON renderer；输出不包含裸 host 指针，默认关闭不改变
   解释器、title profile 或游戏行为。

## 验收

- 默认关闭时不产生记录，stack 仍能返回 idle root context。
- 小容量 ring 确认覆盖旧记录、sequence 单调、事件 mask 与文本 filter 生效；指令
  采样只影响 instruction 事件。
- 夹具覆盖 class init、异常 throw/catch、monitor enter/exit/notify 与 GC begin/end；
  trace/stack JSON 锁定 schema 与关键字段。
- 非法容量、采样间隔和查询 limit 明确失败。
- Windows/x64 `windows-msvc` 配置、构建与 843/843 CTest 通过。

## 边界

- 本 WU 交付 DexVM 核心查询基建；MCP/CLI 绑定、tagged register dump、heap/class 清单、
  自动故障落盘与可抢占 safepoint 属于后续消费层，不在解释器内引入前端依赖。
- trace 是有界诊断证据，不是确定性 replay 日志；环覆盖是预期行为。

状态：完成。
