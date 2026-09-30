# 模块：cpu

## 职责

执行 guest A32/T32/A64 指令，管理每线程寄存器与异常；提供 JIT 和解释器双后端。

## 公共 API

- `A32State`：A32/T32 核心寄存器、CPSR、VFP/NEON 扩展寄存器、线程号及
  可快照的 guest thread pointer；完整核心/扩展寄存器组支持等尺寸批量导入，供 JIT
  状态快照避免逐槽虚调用开销。
- `Cpu::Run(ticks) -> RunResult`：以统一预算运行，返回停止原因和已消费 tick。
- `Cpu::InvalidateCodeRange(GuestRange)`：发布同进程代码变更。Dynarmic 向共享 context
  中所有存活 CPU 排队范围失效并中断正在运行的 peer，由各自执行线程处理缓存；注册与
  退役串行化。解释器直接取指，无缓存可清；其他未实现后端明确失败。内部失效的零 tick
  中断自行续跑，不作为 guest 无进展错误；不修改 guest 寄存器或模拟 CPU 时间。
- Dynarmic 通过 MemoryBus 映射订阅对每个实例排队范围失效，包括独立 context；映射移除、
  权限撤销、地址复用及快照恢复不能继续执行旧块。退役先解绑订阅；CPU 创建失败也释放
  processor ID。普通内容写入仍须 guest 主动 cacheflush，不推断所有写入都是代码。
- `HostCallHook`：可选的 backend-neutral A32 SVC hook，直接借用 16 个 live core
  registers；`handled` 继续当前 JIT run，`unhandled` 保持 supervisor stop，`fault` 形成
  显式 host-call fault stop。CPU 不解释 SVC 的上层含义。
- `CpuSnapshot`：带显式版本的可复制 CPU 状态。
- `CpuFault`：将 memory fault 的地址、访问类型、原因和线程号保留到 CPU 边界。
- `InterpreterCpu`：确定性逐指令后端；当前覆盖 A32/T32 标量算术、条件、控制流及
  word/byte 单次 load/store 基础集。
- `DynarmicCpu`：ARMv7 A32/T32 动态翻译后端；通过 `MemoryBus` 回调及其受保护数据页表
  访存，并与解释器共享状态、tick、停止、fault 及 TPIDRURO 契约。code cache 为
  64 MiB（Dynarmic 上限 128 MiB，映射惰性提交）：16 MiB 会被真实标题稳定期打满，
  触发整缓存冲刷与逐帧重编译。
- `DynarmicExecutionContext`：为同一 guest 进程的 JIT CPU 分配唯一 processor ID 并共享
  exclusive monitor；STREX 通过 MemoryBus 的硬件原子 CompareExchange 提交，与普通 JIT
  直接写竞争时不可覆盖已经发生的不同值写入。不能以仅覆盖回调的互斥锁模拟原子提交。
  monitor 保留 exclusive peer 写入的 reservation 失效；普通直接写入后恢复原值的 ABA
  不由值比较检测，本接口不宣称完整 ARM reservation granule/write-history 模拟。
- `ExecutionBudget`：可选的进程共享并发名额和 token bucket，使用 Clock 单调纳秒计时。
  FIFO 入场、取消通知、RAII 释放与未消费 tick 退款；单片不超过 50000 tick，速率突发量
  为 `max(rate/20, 1)`（50 ms），限速时每片最多使用半桶额度，以容忍宿主计时粒度。
  统计提供 active/peak/waiting/consumed/draining 快照。
  Dynarmic context 在创建时固定预算；启用时 Run 可提前以 budget_exhausted 结束，累计
  watchdog 预算不变，所有 SVC 先返回 runner 释放名额再分派。此模式不启用 fast host hook。
  BeginDrain 解除限额并唤醒等待者，供上层生命周期清理；不修改线程身份或宿主亲和性。
- `GuestThreadGroup`：每个 guest thread ID 启动一个宿主线程和独立 CPU 实例，将
  TLS 基址装入 CPU thread pointer，保存退出状态并提供真实 join 生命周期。可注入失败回调，
  覆盖 CPU 构造、初始 SetState、执行与最终 GetState，保存原异常后在 record 锁外调用；
  回调次生异常不得替换首错，析构 join 不持有线程注册锁。
- `FutexTable`：以 32 位对齐 guest 地址为键，提供比较等待、精确 WAKE N、普通全局唤醒和
  失败清理所需的 sticky `InterruptAll`；中断会唤醒当前 waiter，并让之后的匹配等待立即
  返回 interrupted。M2 syscall 层负责把 interrupted 映射为 `-EINTR`，并把统一 Clock
  超时语义装配到该无超时核心。
- `FutexTable::Wait` 可接收显式中断谓词；`NotifyWaiters` 只唤醒谓词检查，不发放 WAKE
  token、不设置全局 sticky interrupt，用于上层定向信号。谓词不得反向等待 futex 锁。
- `FutexTable::TrySnapshot`：以短 `try_lock` 复制 waiter、expected、等待开始时间、wake
  token/count；锁忙时返回 `complete=false`，不阻塞诊断线程，也不推断 owner 或死锁环。
- 解释器保留为确定性参考/单步后端，后续按诊断需求扩展指令覆盖。

## 不变量

- 每个 guest 线程拥有独立执行上下文。
- 普通全局 futex 唤醒只释放调用时已经等待的线程，不改变 guest 值，也不让后续等待伪成功；
  失败中断一旦发布不得复位，当前及未来匹配等待必须明确返回 interrupted。
- 内存失败产生 Fault，不得返回零；CPU 只调用无上层语义的显式 HostCallHook。
- CPU 后端只通过 `MemoryBus` 及其显式页表能力访存；observer、执行页、非 RW 页和跨页
  访问不得进入直接快路，寄存器状态不得包含宿主指针。
- `Run` 的 tick 预算和消费量必须确定且可测试。
- 所有停止结果必须显式初始化指令、立即数和 fault 字段，跨编译器不得依赖聚合尾字段补零。
- Dynarmic 指令能力缺口返回 `unsupported_instruction`，不是 guest 非法指令；其 fallback
  保留原 PC，协处理器缺口保留当前 IT 及已执行效果，禁止当作 NOP。非法/不可预测编码
  返回 `undefined_instruction`，内部 DecodeError 返回 `backend_error`；不自动投递 SIGILL。
- SVC/BKPT 返回陷阱 PC，同时 CPU 状态 PC 指向下一条指令。
- A32/Thumb-2 `MRC p15,0,*,c13,c0,3` 只读取当前线程 TPIDRURO；其他 CP15 访问
  返回明确能力缺口。旧式非 MCR2 CP15 `c7,c10,5/4` 和 `c7,c5,4` 复用 DMB/DSB/ISB
  语义，ARM 条件与 Thumb IT 保持原路径。通用协处理器翻译采用受检构建扩展，只放行上述
  屏障及 TPIDRURO；其余在实际执行处停下，不进入后端 coprocessor assert，不产生访存或
  基址回写。VFP/NEON 仍由上游独立解码；不是任意 JIT 内部断言的恢复机制。

## 禁止

- 不得包含游戏分支或直接宿主 IO。
- 不得绕过 memory 页表生命周期或长期保存其外的宿主裸指针。

## 测试

`tests/cpu/` 的解释器/JIT 指令级对拍。

`DynarmicExecutionContext::TrySnapshot` 只 try-lock 复制最多 128 个活跃 processor 的最近完成 Run 缓存发布值；带发布 Clock 时间戳，未发布不等于零。Windows x64 通过构建目录的受检 Dynarmic 扩展读取 code capacity/used/full flush，不修改固定 submodule；其他后端返回 unavailable。
