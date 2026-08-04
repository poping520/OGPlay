# 当前状态

更新：2026-08-04 · Runtime 子模块拆分

## 当前阶段

- M0、M1、M2 均已完成；M3 功能开发已完成，三平台出口前按用户要求整理 runtime 结构。
- `WU-0147` 已完成全部 runtime 纯机械迁移；下一个任务编号为 `WU-0148`。
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
- [WU-0145..0146] 建立子模块索引/文档门禁，并完成 VFS 镜像目录迁移。
- [WU-0147] 按特殊授权一次性移动剩余头文件与实现并更新全部路径，不修改代码逻辑。

## 下一步（按优先级）

1. `WU-0148` 执行 M3 三平台出口验收并完成 `M3-ACCEPTANCE.md`。
2. 出口通过后开始 M4。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
