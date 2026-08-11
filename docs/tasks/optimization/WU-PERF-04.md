# WU-PERF-04 · run-apk slice observer 事件泵节流

目标：消除长 guest call 期间无条件 `PumpEvents` 造成的宿主事件循环开销，
让 CPU 时间回到 guest 执行本身。

背景（macOS Release、Dungeon Hunter 加载阶段 10 秒采样，8228 个主线程样本）：

- `SDL_PumpEventsInternal`/`Cocoa_PumpEvents` 占 5508 个样本（约 67%）；
- `DynarmicCpu::Run`（含 JIT 与内存回调）仅约 987 个样本（约 12%）。

根因：`InvokeA32GuestCall` 在每个 20M-tick slice 边界*和每次 supervisor call 之后*
都调用 slice observer，`run-apk` 的 observer 无条件泵宿主事件；系统调用密集阶段
事件泵频率远超响应性所需，每次泵都走完整 Cocoa runloop。

验收：

- [x] `frontend::HostEventPumpGate` 以宿主 Clock ticks 节流：首次放行，之后每
  interval 至多一次；单元测试覆盖首次放行、间隔内拒绝、间隔后放行与零间隔退化。
- [x] `run-apk` slice observer 仅在 gate 放行时泵事件，节奏由独立 `RealtimeClock`
  驱动（250 Hz 上限）；帧循环 `PollEvents` 不受影响。
- [x] observer 仍不推进 guest Clock、不改预算（既有契约不变）。
- [x] full CTest 526/526；Dungeon Hunter 120 帧 Release 实测（同机同夹具）：
  54.26s → 10.05s / 9.75s（两次复测，约 -81%）；加载期采样中事件泵从约 67% 降至
  约 3%，`DynarmicCpu::Run` 占比从约 12% 升至约 78%。

优化后遗留热点（供后续 WU 决策，均在 guest 执行内部）：

- 回调路径每次标量读都锁/放一次 `AddressSpace` 内部 `std::mutex`
  （加载期约占 `DynarmicCpu::Run` 的 13%）；直连页表未覆盖的页才走此路径。
- SVC/HLE 处理约 13%，其中 bionic memory intercept 的 Validate/Read 同样逐次取锁。
