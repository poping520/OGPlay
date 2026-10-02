# BND-48：进程执行一致性与线程失败收敛

状态：实现及 Windows 定向验证完成；游戏回放结果见 CURRENT。
依赖：MemoryBus、Dynarmic 范围失效、GuestThreadGroup 与 clone lifecycle。
设计：[ADR-0088](../../adr/runtime.md#adr-0088)。

范围：

- 内存映射变更经显式订阅发布范围失效，覆盖权限撤销、匿名地址复用及快照恢复。
  回调不持有账本锁，解绑等待正在执行的回调；普通写入仍复用 cacheflush。
- 整个 clone 宿主线程的启动/执行/最终状态失败统一发布首错、退出并中断等待；
  创建失败释放 CPU processor ID，通知或清理的次生异常不覆盖首错。
  fast `host_call_fault` 经 HLE 还原原异常；process/session 可在不进入 guest 时观察首错，
  dex activity 调度前优先检查，主 Looper 身份查询不重复 native prepare。
  BND-49 继续将 clone 非正常 CPU stop 转为统一 A32 诊断与进程首错，取消/预算/正常退出保留原语义。
- ARM kuser v5 提供 DMB 与 32/64 位原子比较交换，准确返回 carry、重试丢失的
  reservation，并保存 ABI 寄存器。实现使用现有 CAS，不另加临时互斥锁。

验证：

- 审计时 4 用例/37 断言中 9 个断言按预期失败；修复后 Windows Release ogplay 和
  ogplay_tests 构建成功，精确筛选的 21 用例/324 断言通过。
- 覆盖真实 mmap/mprotect SVC、活跃 peer、独立 JIT context、快照恢复及订阅退役；
  CPU factory 异常/null、初始 SetState、最终 GetState 与 notifier 次生异常；
  final GetState 在 guest 已退出后失败仍终止其他存活线程；
  32/64 位失配、carry、寄存器保存和第一次独占提交失败后的成功重试。
- 过宽的 `*snapshot*` 筛选曾误选无关 Observable 测试，因既有
  `NativeActivity.loadNativeCode` 未绑定失败；上述正式结果使用精确筛选。
- 证据：`.local/concurrency-audit/{tests.log,fix-build-final-state.log,fix-tests-scoped.log}`。

未覆盖：并发直接数据访问与卸载的完整 quiescence、普通直接写入 ABA 历史、
renderer teardown 顺序风险及完整游戏兼容；不将本次范围测试当作这些问题的验收。

原 APK/OBB、无并发限制、隔离沙盒回放：启动并关闭剧情弹框后进入地图，
f=3324 点击 Accept；加载中有有限长帧，随后进入第一关并继续出帧，
f=3508 画面及采样确认战斗场景。f=4740 报告既有独立首错
`string constructor receiver is not an unbound string instance` 并退出，未强制结束。
证据：`.local/concurrency-audit/{replay-accept.jsonl,replay-gameplay.png,replay.stderr.log}`。
本次未完成关卡及正常主动关闭验收。
