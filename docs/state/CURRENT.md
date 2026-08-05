# 当前状态

更新：2026-08-05 · M4 三平台 NativeActivity 测试后端

## 当前阶段

- M0、M1、M2、M3 均已完成；M4 图形栈正在开发。
- `WU-0192` 已解除真实 APK 测试的 D3D11 硬编码并按宿主选择硬件后端；下一编号为
  `WU-0193`，增加禁止环境缺失静默通过的严格出口模式。
- 当前开发机只使用 Windows/MSVC 预设；M4 的 Linux/macOS 验证在对应平台机器本地
  增量构建并执行，不从当前 Windows 主机通过 SSH 代跑。

## 已验收基线

| 里程碑 | 状态 | 验收记录 | Work Unit |
| --- | --- | --- | --- |
| M0 工程地基 | 完成 | [M0-ACCEPTANCE.md](M0-ACCEPTANCE.md) | `docs/tasks/m0/` |
| M1 内核与跨平台 | 完成 | [M1-ACCEPTANCE.md](M1-ACCEPTANCE.md) | `docs/tasks/m1/` |
| M2 Bionic 与 Syscall | 完成 | [M2-ACCEPTANCE.md](M2-ACCEPTANCE.md) | `docs/tasks/m2/` |
| M3 JNI 与 Java 框架 | 完成 | [M3-ACCEPTANCE.md](M3-ACCEPTANCE.md) | `docs/tasks/m3/` |

能力的机器可读现状以仓库根目录 `capabilities.toml` 为准；本文件不重复维护完整能力历史。

## 最近完成

- [WU-0105..0149] M3 JNI/Java 框架、DEX L1 与 runtime 七子模块迁移完成，并通过
  Windows、Linux、macOS 三平台验收；详见 `M3-ACCEPTANCE.md`。
- [WU-0150..0159] 冻结 ANGLE backend、预编译 SDK 与独立源码维护流程，并在 Windows
  D3D11 上闭合真实 EGL pbuffer 生命周期。
- [WU-0160..0164] 建立 GLES2 142-entry IDL/catalog、guest 搬运、显式分派和受限调用准备。
- [WU-0165..0169] 完成 GLES 状态搬运、真实 ANGLE clear/readback、可注入 HLE 命名空间、
  supervisor trap 传播及 VFS pipe/syscall 42 原子发布；完整验收保留在各 WU 任务单。
- [WU-0170..0174] 闭合 SDL RGBA8 present、APK ZIP32 读取、NativeActivity HLE/session 与
  `run-apk`；真实 API 19 最小 APK 可显示、响应输入并完整销毁，详见各 WU 任务单。
- [WU-0175..0178] 闭合 GPU Agent 查询、NativeActivity 真实指标、等比黑边呈现和同布局
  输入映射；失败、限额与黑边手势均有机器测试，完整记录见各 WU 任务单。
- [WU-0179..0182] 闭合 scissor、左上原点 readback、1..4× 确定性超采样 resolve 及 CLI
  配置；逻辑 surface/输入尺寸不泄漏内部放大尺寸，真实 API 19 APK 以 2× 呈现并正常退出。
- [WU-0183] ANGLE 预编译子模块升级为同 commit 的共享头、Windows/Linux x64 与 macOS
  x64/arm64 包；逐文件字节在 Git checkout 后仍匹配清单，CMake 同时校验平台、GN 参数、
  共享头和哈希。黄金帧按宿主硬件 backend 运行；当前 Windows 包未编入 SwiftShader，
  测试确认明确失败且不回退硬件；Windows/MSVC + ANGLE 全量 CTest 289/289 通过。
- [WU-0184] 综合 API 19 ARMv7 样例以 NDK r21e 完成离线构建，12 个 EGL 与 42 个 GLES2
  调用和实际 ELF 导入均受检；Android HLE 从生成目录发布全部 142 项 GLES2 符号，样例
  越过装载期缺符号，未绑定调用仍明确失败；Windows/MSVC + ANGLE、真实 API 19
  APK/Bionic 环境全量 CTest 291/291 通过，未宣称综合样例已经出帧。
- [WU-0185..0189] 闭合 child 失败传播、shader/program、buffer/texture、vertex/uniform 共
  31 项真实 handler，并把 18 项资源/顶点分派迁入独立 `AndroidBoundaryGles`；全过程
  未实现调用保持精确失败，Windows/MSVC + ANGLE 回归持续通过。
- [WU-0190] 从综合样例源码核对并真实执行全部 42 项 GLES2 调用；补齐 query/state、
  EBO draw 和局部 readback，ANGLE 查询字符串进入只读 guest 页；真实 API 19 APK 首帧
  健康标记为绿色，draw/compile/link 指标与完整 Stop 闭合，全量 CTest 295/295 通过。
- [WU-0191] 综合 API 19 APK 的首帧、按键换色和指针标记移动以连续三帧关键像素锁定；
  draw/clear 指标随帧推进且 shader/program 不重复初始化，全量 CTest 295/295 通过。
- [WU-0192] 两个真实 NativeActivity APK 测试按 Windows D3D11、Linux Vulkan、macOS
  Metal 选择宿主硬件后端，不再把 D3D11 带入其他平台；Windows 全量 CTest 295/295 通过。

## 下一步（按优先级）

1. 增加严格出口模式和本地增量验收入口，缺少 APK/Bionic/ANGLE 或应有的软件后端时
   必须失败；Linux/macOS 在对应平台机器本地执行。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
