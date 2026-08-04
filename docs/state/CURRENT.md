# 当前状态

更新：2026-08-04 · M3 出口验收完成

## 当前阶段

- M0、M1、M2、M3 均已完成；当前准备进入 M4 图形栈。
- `WU-0149` 已完成 M3 三平台出口验收；下一个任务编号为 `WU-0150`。
- 本机开发只使用 Windows/MSVC 预设；Linux/macOS 使用持久目录增量验证，并在里程碑
  出口执行三平台总体验收。

## 已验收基线

| 里程碑 | 状态 | 验收记录 | Work Unit |
| --- | --- | --- | --- |
| M0 工程地基 | 完成 | [M0-ACCEPTANCE.md](M0-ACCEPTANCE.md) | `docs/tasks/m0/` |
| M1 内核与跨平台 | 完成 | [M1-ACCEPTANCE.md](M1-ACCEPTANCE.md) | `docs/tasks/m1/` |
| M2 Bionic 与 Syscall | 完成 | [M2-ACCEPTANCE.md](M2-ACCEPTANCE.md) | `docs/tasks/m2/` |
| M3 JNI 与 Java 框架 | 完成 | [M3-ACCEPTANCE.md](M3-ACCEPTANCE.md) | `docs/tasks/m3/` |

能力的机器可读现状以仓库根目录 `capabilities.toml` 为准；本文件不重复维护完整能力历史。

## 最近完成

- [WU-0105..0128] 完成 JNI/JavaVM 常用行为、双向累计契约与 DEX L1；低频槽继续 trap。
- [WU-0129..0142] 完成 Activity、资源、偏好、Locale、包信息和 native 工作线程累计闭环。
- [WU-0143] M3 功能开发闭合，累计出口 fixture 保持 partial。
- [ADR-0013/WU-0144] runtime 冻结 jni/framework/bionic/syscall/execution/vfs/integration
  七个子模块、无环依赖方向及渐进迁移规则。
- [WU-0145..0146] 建立子模块索引/文档门禁，并完成 VFS 镜像目录迁移。
- [WU-0147] 按特殊授权一次性移动剩余头文件与实现并更新全部路径，不修改代码逻辑。
- [WU-0148] 修复旧版 CMake 文档门禁策略和 AppleClang DEX 隐式转换问题。
- [WU-0149] 同一源码基线在 Windows/MSVC、Linux/GCC、macOS/AppleClang 完成严格构建，
  三端全量 CTest 均通过 232/232，M3 正式验收。

## 下一步（按优先级）

1. `WU-0150` 开始 M4，先冻结 ANGLE/EGL 后端接入边界与三平台选择契约。
2. 随后接入 ANGLE，并保持 EGL/GLES、显示适配和 Agent GPU 接口按小 WU 推进。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
