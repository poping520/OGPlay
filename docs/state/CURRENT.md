# 当前状态

更新：2026-08-11 · 图形 Context/HLE/内存热路径优化已正式验收闭环

## 当前阶段

- M0、M1、M2、M3、M4 已完成并验收。
- M5 的 `WU-0199..0327` 共 129 项已冻结为三个批次；历史任务不移动、不重编号，
  正式 M5 验收尚未声明。
- M6 AI 自动化测试已从 `WU-0328` 开始；目标是让 AI 与 CI 通过同一
  Profile-backed runner 执行 exact APK 的有界场景、输入、readback、断言和证据收集。
- M8 兼容性冲刺从 `WU-0360` 开始；Asphalt 6 按静态盘点→JNI/Java→GLES2→
  线程/VFS→媒体→主界面三轮 gate 批次推进，不为单个缺失函数切 WU。
- Windows/MSVC、Linux/x64 与 macOS/arm64 的 M4 基线均在 commit `f1b59bb` 以 ANGLE、
  warnings-as-errors 和严格全量 CTest 302/302 通过，见 [M4-ACCEPTANCE.md](M4-ACCEPTANCE.md)。

## 已验收基线

| 里程碑 | 状态 | 验收记录 | Work Unit |
| --- | --- | --- | --- |
| M0 工程地基 | 完成 | [M0-ACCEPTANCE.md](M0-ACCEPTANCE.md) | `docs/tasks/m0/` |
| M1 内核与跨平台 | 完成 | [M1-ACCEPTANCE.md](M1-ACCEPTANCE.md) | `docs/tasks/m1/` |
| M2 Bionic 与 Syscall | 完成 | [M2-ACCEPTANCE.md](M2-ACCEPTANCE.md) | `docs/tasks/m2/` |
| M3 JNI 与 Java 框架 | 完成 | [M3-ACCEPTANCE.md](M3-ACCEPTANCE.md) | `docs/tasks/m3/` |
| M4 ANGLE 与 NativeActivity | 完成 | [M4-ACCEPTANCE.md](M4-ACCEPTANCE.md) | `docs/tasks/m4/` |
| M5 去硬编码机制 | 待验收 | 尚未建立 | [三批索引](../tasks/m5/README.md) |

能力的机器可读现状以仓库根目录 `capabilities.toml` 为准；本文件不重复维护完整能力历史。

## 进行中

- Asphalt 6 exact 已越过旧 mixed GLES buffer-state/client-pointer 故障；当前在 Profile
  native call 5 的 `CallStaticIntMethodV requires a valid class reference` 明确失败，尚未
  声称首帧或主界面。

## 最近完成

- [WU-M8-006] guest MonitorEnter/MonitorExit 接入真实可重入 monitor table；owner、recursion、
  waiter 与 condition wakeup 按 object identity 隔离，非 owner exit 明确失败，JavaVM detach
  释放 ownership，session shutdown 在 join 前 sticky interrupt 全部 waiter。Windows/MSVC
  focused 5/5 与 full CTest 522/522 通过；最终 aggregate 为 JNIEnv/JavaVM 212/4。
- [WU-M8-005] guest nonvirtual 30 槽复用现有 descriptor/A32 decoder 和 invocation engine；
  ABI 从 r3 取 method、从栈取首参数或 V/A pointer。ThrowNew 创建带 class/Modified UTF-8
  message 的真实 throwable，ExceptionDescribe 写结构化诊断且保留 pending identity。
  Windows/MSVC focused 3/3 与 full CTest 518/518 通过，当前 aggregate 为 JNIEnv/JavaVM
  210/4。
- [WU-OPT-CLOSURE-01] 正式闭合前 12 项优化验收：统一 active texture 与 GLES1 texture
  matrix unit，以 `(unit,target)` 隔离 2D/cube-map binding/metadata/delete，并让超采样下
  GLES1/GLES2 viewport/scissor query 返回 logical state；六类高频 setter 不再复制整个
  `SharedGlState`，2048 项 raw trace ring 改用独立 mutex。Windows/MSVC warnings-as-errors
  构建、focused 486/486 与 full CTest 509/509 通过；Asphalt 6 exact 仍越过旧 mixed
  GLES/client-pointer 故障，停在后续 `CallStaticIntMethodV` class reference 边界。
- [WU-0379] guest transfer 错误新增 `module!symbol`、寄存器、attribute provenance 与
  GLES1/GLES2 buffer binding 诊断。exact 将 null pointer 收敛到共同 `glBindBuffer`
  状态分裂，而非 guest 坏指针；full CTest 497/497。
- [WU-0378] Dungeon Hunter 第 75 帧命中的 guest 内置 PVRTC 解压批次经测量需约
  96.99 亿 tick；Profile/通用上限提高到仍受限的 100 亿，exact 120/240 帧均通过。
  `run-apk` 新增有界结构化启动、帧、长调用和 teardown 日志；full CTest 497/497。
- [WU-0377] 一次闭合 GLES2 client vertex/index staging、`glDrawArrays`、`glTexSubImage2D`
  与混合链接 GLES1 draw 转入；opaque EBO+client attribute 明确失败。exact 越过 staging，
  进入 `required guest pointer is null`；focused 2/2、full CTest 496/496。
- [WU-0376] 一次闭合 active attribute/uniform、info-log、八项 vector uniform、matrix4 与
  constant vertex attribute 共 14 项。exact 进入 client-array staging；focused 1/1、
  full CTest 496/496。
- [WU-0375] child 在首次执行前或 slice 间均响应外部 exit；session stop 先退出/中断再全量
  join。exact 从 SIGSEGV 恢复为明确 `glGetActiveAttrib` 失败；focused 2/2、full CTest
  496/496。
- [WU-0374] Profile 声明 AudioTrack 七项精确调用，通用 HLE 受检实现 PCM16 配置与
  write。exact 越过音频 bootstrap；focused 3/3、full CTest 495/495。
- [WU-0373] 补齐 blend color/equation、sample coverage 与 flush，覆盖混合链接
  sample-coverage。exact 进入 AudioTrack lookup；focused 2/2、full CTest 494/494。
- [WU-0372] 接入 FBO/renderbuffer 10 项及 mipmap。exact 进入 `glBlendEquation`；
  focused 1/1、full CTest 494/494。
- [WU-0371] 批量转发 shading-language 与 GLES2 capability/state 查询。exact 进入
  `glBindFramebuffer`；focused 1/1、full CTest 494/494。
- [WU-0359] Asphalt 5 exact `title_flow` 三轮 gate 通过；Main Menu golden SHA-256
  `9ee57323dae576c38d4d29984c067b5bceaa86f77724c8f3b174bcd1a81962b8`。

## M6 起点

Scenario→exact Profile session→action/step→assertion→evidence/Result→shutdown 已闭合；
Asphalt 5 标题流三轮通过。OBB fixture 与 MCP GPU trace 仍明确未实现。

## 下一步（按优先级）

1. 修复 Asphalt 6 Profile native call 5 的 static class reference 发布/解析，再继续
   license/VFS/媒体与主界面 Scenario 三轮 gate。

## 阻塞

- 单场景自动测试闭环无阻塞。OBB fixture、MCP GPU trace 与多场景趋势属于已知后续范围，
  当前请求会明确失败，不伪造成功。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
