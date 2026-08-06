# 当前状态

更新：2026-08-06 · M4 三平台出口验收完成

## 当前阶段

- M0、M1、M2、M3、M4 已完成并验收。
- Windows/MSVC、Linux/x64 与 macOS/arm64 均在同一主仓库 commit `f1b59bb` 上以 ANGLE
  开启和 warnings-as-errors 通过严格全量 CTest 302/302。记录见
  [M4-ACCEPTANCE.md](M4-ACCEPTANCE.md)。

## 已验收基线

| 里程碑 | 状态 | 验收记录 | Work Unit |
| --- | --- | --- | --- |
| M0 工程地基 | 完成 | [M0-ACCEPTANCE.md](M0-ACCEPTANCE.md) | `docs/tasks/m0/` |
| M1 内核与跨平台 | 完成 | [M1-ACCEPTANCE.md](M1-ACCEPTANCE.md) | `docs/tasks/m1/` |
| M2 Bionic 与 Syscall | 完成 | [M2-ACCEPTANCE.md](M2-ACCEPTANCE.md) | `docs/tasks/m2/` |
| M3 JNI 与 Java 框架 | 完成 | [M3-ACCEPTANCE.md](M3-ACCEPTANCE.md) | `docs/tasks/m3/` |
| M4 ANGLE 与 NativeActivity | 完成 | [M4-ACCEPTANCE.md](M4-ACCEPTANCE.md) | `docs/tasks/m4/` |

能力的机器可读现状以仓库根目录 `capabilities.toml` 为准；本文件不重复维护完整能力历史。

## 最近完成

- [WU-0195] Windows/MSVC 出口预演：真实 Bionic 与两个 APK，严格全量 CTest 297/297。
- [WU-0196] macOS/arm64 Metal 与 SwiftShader 出口；严格全量 CTest 297/297，关键测试
  连续 5 次稳定。
- [WU-0197] 原生 ANGLE 测试统一为 Windows D3D11、Linux Vulkan、macOS Metal 硬件映射。
- [WU-0198] Linux/x64 严格出口通过：修复 GCC missing-field-initializers /
  range-loop-construct，并以清单 ICD + Vulkan loader 闭合 SwiftShader 黄金帧；审查后
  增加作用域 driver 环境、可重定位 ICD、真实 renderer 与 software→hardware 隔离回归，
  修复后严格全量 CTest 302/302；编写 `M4-ACCEPTANCE.md`。
- macOS/arm64 审查修复复验：POST_BUILD 暂存 `libEGL`/`libGLESv2` dylib 后严格全量
  CTest 302/302；Metal/SwiftShader/隔离与两个真实 APK 各连续 5 次稳定。
- Windows/MSVC 审查修复复验：同 commit `f1b59bb` 严格全量 CTest 302/302；D3D11 黄金帧、
  SwiftShader 明确失败、backend 隔离与两个真实 APK 各连续 5 次稳定；冻结 M4 验收基线。

## 下一步（按优先级）

1. 按路线图打开 M5。
2. M4 范围外项目（窗口 surface、未绑定 GLES2、通用多库入口等）不得伪造成功，继续以
   能力账本为准。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
