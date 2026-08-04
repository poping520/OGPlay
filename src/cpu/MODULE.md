# 模块：cpu

## 职责

执行 guest A32/T32/A64 指令，管理每线程寄存器与异常；提供 JIT 和解释器双后端。

## 公共 API

- `A32State`：A32/T32 核心寄存器、CPSR、VFP/NEON 扩展寄存器、线程号及
  可快照的 guest thread pointer。
- `Cpu::Run(ticks) -> RunResult`：以统一预算运行，返回停止原因和已消费 tick。
- `CpuSnapshot`：带显式版本的可复制 CPU 状态。
- `CpuFault`：将 memory fault 的地址、访问类型、原因和线程号保留到 CPU 边界。
- `InterpreterCpu`：确定性逐指令后端；当前覆盖 A32/T32 标量算术、条件、控制流及
  word/byte 单次 load/store 基础集。
- `DynarmicCpu`：ARMv7 A32/T32 动态翻译后端；通过 `MemoryBus` 回调访存，不暴露
  宿主指针，并与解释器共享状态、tick、停止及 fault 契约。
- `GuestThreadGroup`：每个 guest thread ID 启动一个宿主线程和独立 CPU 实例，将
  TLS 基址装入 CPU thread pointer，保存退出状态并提供真实 join 生命周期。
- `FutexTable`：以 32 位对齐 guest 地址为键，提供比较等待与精确 WAKE N；M2 syscall
  层负责把统一 Clock 超时语义装配到该无超时核心。
- 解释器保留为确定性参考/单步后端，后续按诊断需求扩展指令覆盖。

## 不变量

- 每个 guest 线程拥有独立执行上下文。
- 内存失败产生 Fault，不得返回零；CPU 不直接调用 HLE。
- CPU 后端只通过 `MemoryBus` 访存；寄存器状态不得包含宿主指针。
- `Run` 的 tick 预算和消费量必须确定且可测试。
- 所有停止结果必须显式初始化指令、立即数和 fault 字段，跨编译器不得依赖聚合尾字段补零。
- 未识别指令停在原 PC 并返回 `undefined_instruction`，禁止当作 NOP。
- SVC/BKPT 返回陷阱 PC，同时 CPU 状态 PC 指向下一条指令。

## 禁止

- 不得包含游戏分支或直接宿主 IO。
- 不得绕过 memory 接口长期保存宿主裸指针。

## 测试

`tests/cpu/` 的解释器/JIT 指令级对拍。
