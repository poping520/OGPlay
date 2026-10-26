# 可观测性、线程握手与退出

返回 [ADR 索引](README.md)。本文件只记录现行决定；旧文由 Git 保存。

- [ADR-0006 · 结构化可观测性是一等接口](#adr-0006)
- [ADR-0025 · 有界协作停止、单向退役与首错保留](#adr-0025)
- [ADR-0026 · 进展预算与有界停滞诊断](#adr-0026)

<a id="adr-0006"></a>

## ADR-0006 · 结构化可观测性是一等接口

- 状态：Accepted
- 日期：2026-08-03

### 背景

裸文本、静默桩和人工地址解析难以用于自动调试与验收。

### 决定

日志保存固定 message 与结构化 fields，以 frame 作为主时间轴；运行时命中与能力状态分开。
未实现调用必须记账并明确失败，warning 单独统计，不冒充实现或成功。
Agent Control、Dashboard 与诊断消费同源结构化事实；下层只发布窄接口，不反向依赖界面。
src 中禁止裸 printf、std::cout、std::cerr；显式 Java/CLI 控制台协议按所属模块处理。

### 后果

自动检查使用可判定状态与指标；不能以日志 grep 或未报错替代兼容性证据。

<a id="adr-0025"></a>

## ADR-0025 · 有界协作停止、单向退役与首错保留

- 状态：Accepted
- 日期：2026-08-28

### 背景

退出回调依赖存活 worker 和音频；提前取消会破坏清理，运行 watchdog 也不能充当退出预算。

### 决定

区分 Activity.finish、健康宿主关闭、guest exit 与运行 fault。
finish 执行实例 onDestroy；健康宿主关闭在有界根 scope 内尝试前台失焦、pause、Surface/stop、
前台 onDestroy，不作后台全栈 finish；期间保留图形、线程准入与音频消费，只解除宿主帧节奏等待。
协作完成或失败后先停止音频消费者，再 BeginTeardown、关闭 scheduler/准入、取消并 join worker。
BeginTeardown 幂等且不可逆，封闭 Java/native EGL/GLES、唤醒阻塞，安全点观察独立取消。
退役后图形不进入 ANGLE，swap 返回 false 并锁存 BAD_NATIVE_WINDOW；不得复活旧线程或资源。
退出同步清理仅由创建 owner/live root 准入，重入共享不可续期 tick 与 Clock 累计墙钟预算。
Java 指令和阻塞也检查 deadline；到期使用不可被 guest catch 吞掉的控制展开，不继续 guest 回调。
进程 Stop 不执行模块卸载式 DSO fini；底层显式 permitted finalization 仍须有界并报告失败。
原生 exit/exit_group 保留来源与代码；Java/native 退出后不再重入 guest 生命周期/JNI 清理。
未处理 CPU stop 标记 native 状态不安全，拒绝后续 guest 调用和 fini；普通异常不自动归入此类。
失败仍 join、flush、detach 和回收宿主资源，保留运行首错，清理错误独立报告，Stop 幂等。

### 后果

此为进程内有界兼容策略，不承诺完整 Android 退出或抢占第三方宿主库内部等待；超时不得报成功。

<a id="adr-0026"></a>

## ADR-0026 · 进展预算与有界停滞诊断

- 状态：Accepted
- 日期：2026-08-28

### 背景

仅经过 syscall 不能证明进展，停滞后再等待全局锁或进程内展开宿主栈会制造新死锁。

### 决定

native 分发统一区分 not_handled、handled_idle、handled_advanced；仅显式 renewable 帧按
真实进展续 watchdog，普通线程不续期，退出取消与清理预算独立且优先。
正字节 IO、真实驻留等待、成功 swap/audio enqueue 与 JNI 重入按集中清单分类；查询、EOF、
wake/yield、内存管理和未知已处理入口默认 idle。JNI 重入可能仍使琐碎调用循环续期。
首个 Surface traversal 前对已有 worker 集合作有界静默握手；新线程不追入，超限告警后继续。
frontend 拥有 DiagnosticState/coordinator，并向下显式注入窄事实源；固定环、atomic 或
try-lock 快照采集，繁忙 section 标 unavailable，不阻塞其余输出。
协调器不依赖 SDL 主循环，由内部请求、退出超时或 OS 触发唤醒，析构 stop→join，禁止 detach。
快照标 schema、采样时间、generation 和 section 状态，各 section 不宣称全局原子时刻。
futex 无 owner edge 时不能宣称确认死锁；完整宿主栈在进程外采集，按 host_tid 对齐。

### 后果

诊断可以部分失败但必须如实标记；报警取证不改变停止协议，无符号帧保留 module+offset。
