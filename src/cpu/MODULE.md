# 模块：cpu

## 职责

执行 guest A32/T32/A64 指令，管理每线程寄存器与异常；提供 JIT 和解释器双后端。

## 公共 API

- `A32State`：A32/T32 核心寄存器、CPSR、VFP/NEON 扩展寄存器及线程号。
- `Cpu::Run(ticks) -> RunResult`：以统一预算运行，返回停止原因和已消费 tick。
- `CpuSnapshot`：带显式版本的可复制 CPU 状态。
- `CpuFault`：将 memory fault 的地址、访问类型、原因和线程号保留到 CPU 边界。
- M1 后续实现解释器与 Dynarmic 两个后端。

## 不变量

- 每个 guest 线程拥有独立执行上下文。
- 内存失败产生 Fault，不得返回零；CPU 不直接调用 HLE。
- CPU 后端只通过 `MemoryBus` 访存；寄存器状态不得包含宿主指针。
- `Run` 的 tick 预算和消费量必须确定且可测试。

## 禁止

- 不得包含游戏分支或直接宿主 IO。
- 不得绕过 memory 接口长期保存宿主裸指针。

## 测试

`tests/cpu/` 的解释器/JIT 指令级对拍。
