# 当前状态

更新：2026-08-08 · M5 已打开

## 当前阶段

- M0、M1、M2、M3、M4 已完成并验收。
- M5 去硬编码正在推进；首个精确 legacy title Profile 已闭合 APK 身份、Bionic 依赖、
  native-call 目标/A32 调用帧与 Android link preflight，完整 JNIEnv/JavaVM guest ABI
  也已映射；指定 `gl_surface_view` guest process 已执行并提交首个 managed ANGLE frame。
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

- 无。目标 ELF 的 62 个 GL import 已全部具有显式行为 handler；exact-APK 在一帧边界
  正常停止。matrix-palette skinning 与 fixed renderer 完整语义仍按能力账本保持 partial。

## 最近完成

- [WU-0275] 最后 3 个 `GL_OES_matrix_palette` import 已进入受检、可重置的 palette index
  与 client pointer 状态；未完成的 skinning draw 明确失败。目标 62/62 GL import 均有
  行为 handler，exact-APK 一帧 smoke 继续以状态 0 完成。
- [WU-0274] 批量闭合 4 种 client pointer、enable/disable client state 与两种 draw；guest
  array/index 按真实 draw 范围预检上传，内部 GLES2 fixed shader 消费矩阵、颜色、light0、
  texture、fog 与 alpha state。exact-APK 首次成功提交 1 个 managed ANGLE frame。
- [WU-0273] 一次批量闭合 `glCompressedTexImage2D`、`glCopyTexImage2D`、
  `glTexImage2D` 与 `glTexSubImage2D`，guest 像素受检搬运并在 level 0 消费 automatic
  mipmap 状态。exact-APK 推进到 `glEnableClientState`。
- [WU-0272] 一次批量闭合 `glAlphaFunc`、`glClientActiveTexture`、`glColor4f/ub`、
  `glGetFloatv`、`glTexEnvfv/i`；最大 anisotropy 来自真实 ANGLE，其他 legacy 状态按
  context/texture unit 保存。exact-APK 推进到 `glCompressedTexImage2D`。
- [WU-0271] GLES1 `GL_GENERATE_MIPMAP` 已按 active unit 与 texture object 隔离保存，
  delete/reset 同步且不再错误转发 GLES2；exact-APK 推进到 `glGetFloatv`。自动生成消费
  留待 texture upload，能力保持 partial。
- [WU-0270] GLES1 `glGetString` 已把真实 ANGLE vendor/renderer/version/extensions 发布到
  专属分槽只读 guest 区域；exact-APK 推进到 `glTexParameterf` 参数兼容边界。
- [WU-0269] GLES1 `glGenTextures`/`glDeleteTextures` 已通过受检 little-endian guest
  数组接入真实 ANGLE texture name 生命周期；exact-APK 推进到 `glGetString`。
- [WU-0268] guest `CallStaticIntMethod` 已复用 descriptor-backed A32 variadic 解码并进入
  统一 invocation engine；exact-APK 命中既有 `resource.length` handler 后推进到
  `glGenTextures`。
- [WU-0267] `gles1_material_front_face` quirk 已依据真实调用序列扩展为独立保存
  `GL_FRONT`/`GL_BACK`，关闭时两者均明确失败，reset 保留策略。
- [WU-0266] 已把已验证 Profile quirk 映射为通用 guest-session 边界选项，默认路径保持
  fail closed；exact-APK 由 `GL_FRONT` 推进并暴露后续 `GL_BACK` 证据。
- [WU-0265] Profile quirk 声明与 CLI 注册表加载已闭合，关闭 quirk 的状态测试继续拒绝
  `GL_FRONT`；该 WU 完成时运行时行为尚未启用，随后已由 WU-0266/0267 闭合。
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
GLES1 handler 对照得出；当前已实现 62 个，尚余 0 个。它只表示该目标实际导入且尚未
实现的入口，不代表完整 GLES1 命名空间。

- 无。

## 下一步（按优先级）

1. 增加 exact-APK 多帧 smoke 与稳定黄金帧证据。
2. 按实际调用证据扩展 fixed renderer 的多纹理、完整 texture environment 与多光源语义。
3. 如目标启用 matrix palette，再实现 palette matrix stack 与 skinning shader。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
