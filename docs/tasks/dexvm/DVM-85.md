# DVM-85 · 统一 SystemClock 与 Android 调度 backend

## 目标（一句话）

让 `SystemClock`、主/子 Looper、Handler、Timer、CountDownTimer 与 AsyncTask 共用
`DexVmAndroidContext::uptime_millis` 和一个确定性 deadline 队列。

## 依赖

- DVM-48/50：`VmThreadRuntime`、执行锁释放与统一 monitor Clock。
- DVM-80：`android_os.cpp`/`java_util.cpp` family TU 与 core/Android ownership。
- [`12-api19-capability-stack.md`](../../design/dexvm/12-api19-capability-stack.md) Phase D。

## 范围

- `SystemClock` 的 uptime/elapsed/nanos/sleep 只读或推进会话单调时钟。
- 队列按 `(deadline, sequence)` 排序；主 Looper 在 lifecycle 安全点泵送，
  `HandlerThread` 使用一 guest 线程对应一宿主线程的 `VmThreadRuntime`。
- Handler 支持 message/runnable 的即时、延迟、绝对时间投递、查询与取消。
- Timer/TimerTask、CountDownTimer、AsyncTask 迁入同一 scheduler；AsyncTask 的后台阶段
  在真实 guest 子线程运行，progress/post 回主 Looper。
- 调度引用进入 GC trace/session root，sweep 清理 side-table；shutdown 取消队列并唤醒全部
  Looper waiter。
- 按用户授权，本 WU 不受通常 10 文件与 800 行限制；family TU 仍保持既定豁免。

## 不做

- 不实现 Binder MessageQueue、AlarmManager、系统休眠差异或宿主墙钟调度。
- 不承诺 AsyncTask Executor 并行策略、完整 concurrent 包或完整 Timer purge 计数。
- 不运行全量 CTest；阶段全量回归留到最后一个 WU。

## 验收与结果

- 固定 Clock 下 delay 不提前、同 deadline FIFO、remove/cancel 与 shutdown 可复现。
- Timer/CountDownTimer 与 Handler 共用时钟；HandlerThread 回调在真实子线程 Looper 执行。
- AsyncTask 后台阶段与主线程 post 阶段分离，状态只允许单次执行。
- `windows-msvc` Debug `ogplay_tests` 构建及 DVM-85 4/4（83 assertions）通过；
  Thread/monitor/lifecycle 定向回归 26/26（132805 assertions）通过；全量未运行。
- 2026-09-04 follow-up：`Context.getMainLooper` 与 `ContextWrapper` 委托接入同一主
  Looper；Windows Release scheduler 定向测试 6/6（136 assertions）通过，PvZ Profile
  实跑越过该缺口并停于 `Intent.resolveTypeIfNeeded(ContentResolver)`。

状态：已完成。
