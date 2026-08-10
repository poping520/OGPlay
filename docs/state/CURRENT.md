# 当前状态

更新：2026-08-10 · M8 GLES2 client vertex/index staging 批次已完成

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

- Asphalt 6 exact 已越过 JNI_OnLoad、GLES discovery/FBO/state、AudioTrack、shader/uniform
  与 client vertex/index staging（含混合链接 GLES1 draw 转入）；当前停在
  `required guest pointer is null`；尚未声称首帧或主界面。

## 最近完成

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

1. 诊断 exact `required guest pointer is null`，再闭合 license/VFS/媒体并固化主界面
   Scenario 与三轮 gate。

## 阻塞

- 单场景自动测试闭环无阻塞。OBB fixture、MCP GPU trace 与多场景趋势属于已知后续范围，
  当前请求会明确失败，不伪造成功。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
