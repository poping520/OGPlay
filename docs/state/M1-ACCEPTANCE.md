# M1 内核与跨平台验收

日期：2026-08-04

## 结论

M1 的跨平台 HAL、4 GiB guest 地址空间、A32/T32 CPU 后端、真实宿主线程模型和裸 guest
累计样本已经闭合。Windows/MSVC、Linux/GCC、macOS/AppleClang 均以
warnings-as-errors 构建成功并通过 CTest 73/73，可以进入 M2；本阶段不要求 APK、ELF、
Bionic、JNI、EGL/GLES 或画面。

## 路线图出口条件

| 出口条件 | 结果 | 证据 |
| --- | --- | --- |
| SDL3 窗口与输入 HAL | 通过 | 生命周期及键盘、鼠标、手柄、退出事件契约测试 |
| 4 GiB guest 地址空间 | 通过 | 强类型地址、低地址 guard、页权限、fault、快照与 CheckedMemoryBus |
| A32/T32 CPU | 通过 | 解释器基础指令族与 Dynarmic 后端遵守同一状态、内存、tick 和停止契约 |
| 一 guest 线程对应一宿主线程 | 通过 | HostThread、ThreadGroup、TLS 生命周期和并发压力测试 |
| futex 核心 | 通过 | guest 地址键控的 WAIT/WAKE N、错误路径和多线程同步测试 |
| 裸 guest 累计样本 | 通过 | A32/Thumb mailbox 样本在解释器与 Dynarmic 上结果一致并可快照复跑 |
| 三平台出口 | 通过 | Windows/MSVC、Linux/GCC、macOS/AppleClang，CTest 73/73 |

## Work Unit 范围

- `WU-0015..0019`：第三方 submodule、Clock、平台边界与 SDL3 窗口输入。
- `WU-0020..0024`：guest 地址、虚拟内存、AddressSpace、CheckedMemoryBus 与快照。
- `WU-0025..0032`：CPU 状态、解释器、裸样本、Dynarmic 与轻量 Boost 依赖。
- `WU-0033..0036`：宿主真线程、guest 线程组、futex 与 gfx/audio/fs HAL 契约。
- `WU-0037..0040`：GCC/Clang 可移植性修复和三平台出口验收。

完整任务单位于 `docs/tasks/m1/`。关键决定见 ADR-0003、ADR-0004、ADR-0007 和
ADR-0008。

## 明确不属于 M1

- ELF linker、真实 AOSP Bionic、Android syscall 和统一 VFS：M2。
- JNI、Java 框架对象模型与 DEX 决策：M3。
- ANGLE/SwiftShader、EGL/GLES、NativeActivity 画面与输入累计集成：M4。
