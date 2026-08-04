# 当前状态

更新：2026-08-04 · Runtime 子模块拆分

## 当前阶段

- M0、M1、M2 均已完成；M3 功能开发已完成，三平台出口前按用户要求整理 runtime 结构。
- `WU-0145` 已同步七个 runtime 子模块索引与文档门禁；下一个任务编号为 `WU-0146`。
- 本机开发只使用 Windows/MSVC 预设；Linux/macOS 使用持久目录增量验证，并在里程碑
  出口执行三平台总体验收。

## 已验收基线

| 里程碑 | 状态 | 验收记录 | Work Unit |
| --- | --- | --- | --- |
| M0 工程地基 | 完成 | [M0-ACCEPTANCE.md](M0-ACCEPTANCE.md) | `docs/tasks/m0/` |
| M1 内核与跨平台 | 完成 | [M1-ACCEPTANCE.md](M1-ACCEPTANCE.md) | `docs/tasks/m1/` |
| M2 Bionic 与 Syscall | 完成 | [M2-ACCEPTANCE.md](M2-ACCEPTANCE.md) | `docs/tasks/m2/` |
| M3 JNI 与 Java 框架 | 开发完成，待出口 | [M3-ACCEPTANCE.md](M3-ACCEPTANCE.md) | `docs/tasks/m3/` |

能力的机器可读现状以仓库根目录 `capabilities.toml` 为准；本文件不重复维护完整能力历史。

## 最近完成

- [WU-0105..0128] 完成 JNI/JavaVM 常用行为、双向累计契约与 DEX L1；低频槽继续 trap。
- [WU-0129..0142] 完成 Activity、资源、偏好、Locale、包信息和 native 工作线程累计闭环。
- [WU-0143] M3 功能开发闭合，累计出口 fixture 保持 partial。
- [ADR-0013/WU-0144] runtime 冻结 jni/framework/bionic/syscall/execution/vfs/integration
  七个子模块、无环依赖方向及渐进迁移规则。

## 下一步（按优先级）

1. `WU-0146` 迁移低耦合 VFS，验证公共头与实现目录镜像方案。
2. 后续按 bionic → jni → framework → syscall → execution → integration 渐进迁移。
3. 拆分完成后执行 M3 三平台出口并开始 M4。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
