# 当前状态

更新：2026-08-08 · M5 已打开

## 当前阶段

- M0、M1、M2、M3、M4 已完成并验收。
- M5 去硬编码正在推进；首个精确 legacy title Profile 已闭合 APK 身份、Bionic 依赖、
  native-call 目标/A32 调用帧与 Android link preflight，完整 JNIEnv/JavaVM guest ABI
  也已映射；当前尚未执行 `gl_surface_view` guest process。
- Windows/MSVC、Linux/x64 与 macOS/arm64 均在同一主仓库 commit `f1b59bb` 上以 ANGLE
  开启和 warnings-as-errors 通过严格全量 CTest 302/302。记录见
  [M4-ACCEPTANCE.md](M4-ACCEPTANCE.md)。
- 当前 M5 增量 WU 的开发与验收目标为 macOS-arm64 + ANGLE：warnings-as-errors、全量
  CTest 与 exact-APK bounded smoke；其他平台留到显式跨平台检查点，不再阻塞每个
  增量 WU。

## 已验收基线

| 里程碑 | 状态 | 验收记录 | Work Unit |
| --- | --- | --- | --- |
| M0 工程地基 | 完成 | [M0-ACCEPTANCE.md](M0-ACCEPTANCE.md) | `docs/tasks/m0/` |
| M1 内核与跨平台 | 完成 | [M1-ACCEPTANCE.md](M1-ACCEPTANCE.md) | `docs/tasks/m1/` |
| M2 Bionic 与 Syscall | 完成 | [M2-ACCEPTANCE.md](M2-ACCEPTANCE.md) | `docs/tasks/m2/` |
| M3 JNI 与 Java 框架 | 完成 | [M3-ACCEPTANCE.md](M3-ACCEPTANCE.md) | `docs/tasks/m3/` |
| M4 ANGLE 与 NativeActivity | 完成 | [M4-ACCEPTANCE.md](M4-ACCEPTANCE.md) | `docs/tasks/m4/` |

能力的机器可读现状以仓库根目录 `capabilities.toml` 为准；本文件不重复维护完整能力历史。

## 进行中

- 无。下一精确边界为目标向 `glMaterialfv` 传入规范不接受的 `GL_FRONT`；需以 Profile
  quirk 精确归一为 `GL_FRONT_AND_BACK`，标准路径继续 fail closed。

## 最近完成

- [WU-0264] 目标导入的 7 个 GLES1 lighting/material/fog 入口已进入独立、可重置的
  fixed-pipeline 状态，pointer 参数完整受检搬运，枚举与范围错误明确失败。macOS-arm64 +
  ANGLE 行为测试通过；exact-APK 已进入 `glMaterialfv` handler 并暴露 `GL_FRONT`
  规范兼容边界。
- [WU-0263] GLES1 modelview/projection/texture 三套矩阵栈及 7 个浮点 matrix handler 已
  批量闭合；guest matrix 经受检地址完整搬运，栈与非法值 fail closed。Windows-x64 +
  ANGLE 全量 CTest 403/403 与 exact-APK smoke 已通过，下一边界为 `glMaterialfv`。
- [WU-0262] 目标导入的 17 个 GLES1 无指针标量状态入口已批量闭合；共有状态进入
  ANGLE，GLES1-only hint/capability 与 transfer state 显式保存。Windows-x64 + ANGLE
  全量 CTest 402/402 与 exact-APK smoke 已通过，下一边界为 `glMatrixMode`。
- [WU-0261] GLES1 `glClear` 现将 guest `GLbitfield` mask 原样转发当前
  `AngleFrame::Clear`；macOS/arm64 ANGLE 全量 CTest 402/402 与 exact-APK smoke
  已通过，真实目标越过该调用后首个新阻塞为未实现 GLES1 `glEnable`（thunk 37）。

## 目标 ELF 尚未实现的 GL 入口

以下清单以 `docs/demo/games/libasphalt5.so` 的 62 个 GL import 与 WU-0264 后的显式
GLES1 handler 对照得出；当前已实现 37 个，尚余 25 个。它只表示该目标实际导入且尚未
实现的入口，不代表完整 GLES1 命名空间。

- 固定管线状态/查询（7）：`glAlphaFunc`、`glClientActiveTexture`、`glColor4f`、
  `glColor4ub`、`glGetFloatv`、`glTexEnvfv`、`glTexEnvi`。
- 纹理资源/查询（7）：`glCompressedTexImage2D`、`glCopyTexImage2D`、
  `glDeleteTextures`、`glGenTextures`、`glGetString`、`glTexImage2D`、
  `glTexSubImage2D`。
- Client array/draw（8）：`glColorPointer`、`glDisableClientState`、`glDrawArrays`、
  `glDrawElements`、`glEnableClientState`、`glNormalPointer`、`glTexCoordPointer`、
  `glVertexPointer`。
- Matrix-palette 扩展（3）：`glCurrentPaletteMatrixOES`、
  `glMatrixIndexPointerOES`、`glWeightPointerOES`。

## 下一步（按优先级）

1. 登记并接入只对精确 Profile 生效的 material `GL_FRONT` 归一 quirk。
2. 随后批量闭合其余 fixed-pipeline 状态、client array/draw 与 matrix-palette extension。
3. 重跑真实 APK 的受帧数约束 smoke，直到获得首个 managed ANGLE frame。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
