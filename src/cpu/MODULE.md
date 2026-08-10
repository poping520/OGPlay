# 模块：cpu

## 职责

执行 guest A32/T32/A64 指令，管理每线程寄存器与异常；提供 JIT 和解释器双后端。

## 公共 API

- `A32State`：A32/T32 核心寄存器、CPSR、VFP/NEON 扩展寄存器、线程号及
  可快照的 guest thread pointer；完整核心/扩展寄存器组支持等尺寸批量导入，供 JIT
  状态快照避免逐槽虚调用开销。
- `Cpu::Run(ticks) -> RunResult`：以统一预算运行，返回停止原因和已消费 tick。
- `CpuSnapshot`：带显式版本的可复制 CPU 状态。
- `CpuFault`：将 memory fault 的地址、访问类型、原因和线程号保留到 CPU 边界。
- `InterpreterCpu`：确定性逐指令后端；当前覆盖 A32/T32 标量算术、条件、控制流及
  word/byte 单次 load/store 基础集。
- `DynarmicCpu`：ARMv7 A32/T32 动态翻译后端；通过 `MemoryBus` 回调及其受保护数据页表
  访存，并与解释器共享状态、tick、停止、fault 及 TPIDRURO 契约。
- `DynarmicExecutionContext`：为同一 guest 进程的 JIT CPU 分配唯一 processor ID 并共享
  exclusive monitor；普通写与 exclusive compare/write 也共享提交锁，使 LDREX/STREX
  在真实宿主线程间保持原子语义。
- `GuestThreadGroup`：每个 guest thread ID 启动一个宿主线程和独立 CPU 实例，将
  TLS 基址装入 CPU thread pointer，保存退出状态并提供真实 join 生命周期。
- `FutexTable`：以 32 位对齐 guest 地址为键，提供比较等待、精确 WAKE N、普通全局唤醒和
  失败清理所需的 sticky `InterruptAll`；中断会唤醒当前 waiter，并让之后的匹配等待立即
  返回 interrupted。M2 syscall 层负责把 interrupted 映射为 `-EINTR`，并把统一 Clock
  超时语义装配到该无超时核心。
- 解释器保留为确定性参考/单步后端，后续按诊断需求扩展指令覆盖。

## 不变量

- 每个 guest 线程拥有独立执行上下文。
- 普通全局 futex 唤醒只释放调用时已经等待的线程，不改变 guest 值，也不让后续等待伪成功；
  失败中断一旦发布不得复位，当前及未来匹配等待必须明确返回 interrupted。
- 内存失败产生 Fault，不得返回零；CPU 不直接调用 HLE。
- CPU 后端只通过 `MemoryBus` 及其显式页表能力访存；observer、执行页、非 RW 页和跨页
  访问不得进入直接快路，寄存器状态不得包含宿主指针。
- `Run` 的 tick 预算和消费量必须确定且可测试。
- 所有停止结果必须显式初始化指令、立即数和 fault 字段，跨编译器不得依赖聚合尾字段补零。
- 未识别指令停在原 PC 并返回 `undefined_instruction`，禁止当作 NOP。
- SVC/BKPT 返回陷阱 PC，同时 CPU 状态 PC 指向下一条指令。
- A32/Thumb-2 `MRC p15,0,*,c13,c0,3` 只读取当前线程 TPIDRURO；其他 CP15 访问
  仍明确触发未定义指令。

## 禁止

- 不得包含游戏分支或直接宿主 IO。
- 不得绕过 memory 页表生命周期或长期保存其外的宿主裸指针。

## 测试

`tests/cpu/` 的解释器/JIT 指令级对拍。
