# BND-44：ARM 用户态协处理器屏障与受控缺口出口

状态：完成（所列有界范围）。决策：[ADR-0082](../../adr/runtime.md#adr-0082)。

## 范围

- 将 CP15 旧式 DMB/DSB/ISB 编码接到 Dynarmic 原有屏障语义，保留 ARM 条件及 Thumb IT。
- 通用 CDP/LDC/STC/MCR/MRC/MCRR/MRRC（含 2 形式）只放行已支持的 TPIDRURO 查询和屏障。
  其他操作在实际执行位置返回 unsupported_instruction，不进入后端协处理器断言路径。
- 保留指令前的效果；失败指令不读写内存、不回写基址、不执行后续指令。保存原 PC/IT，
  诊断包含指令字、状态、线程及原有调用链；不把未实现能力当成 guest SIGILL。
- 保留上游非法编码检查；DecodeError 与普通未实现区分。不承诺拦截任意 JIT 内部断言。
- 不修改原 SO、ROM、游戏 Profile 或固定第三方 submodule。不扩充完整 CP15/系统态。

## 验证

- `windows-msvc` Release 的 `ogplay_tests`、`ogplay` 构建通过；沿用本机已有的
  `_CL_=/wd4996 /wd4834` 测试告警屏蔽。
- 定向 31 用例、476 断言通过：新旧 DMB/DSB/ISB、七类操作及 2 形式、ARM 条件、Thumb IT
  跳过/执行/失败后重试、跨 word 的 Thumb 指令诊断、TLS、UDF 分类、前置写生效/后置写
  不执行、LDC/STC 不访存或基址回写，以及原有 CPU/runner/执行预算回归。
- 按用户原 APK/OBB 命令（持久沙盒、无 Profile、无 CPU 配额、无帧上限）复现一次：
  `Should raise coproc exception here` 消失，下一首错 `pthread_kill failed`，随后 guest
  SIGABRT/code=134；CLI 输出完整调用报告并以 1 退出，未出现宿主断言退出。
- `/proc/stat`、时区、NDK 符号及调度调用警告仍存在；未验证完整游戏或其他宿主架构。
  新同步 fault-to-signal 不在本任务范围。没有用游戏继续启动替代屏障/故障边界测试。
- 证据：`.local/coprocessor-fix-{build,tests,startup,startup-check}.log`。
