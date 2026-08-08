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
- 当前 M5 增量 WU 的开发与验收目标为 Windows-x64 + ANGLE：warnings-as-errors、全量
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

- 无。下一精确边界为未实现 GLES1 `glMaterialfv`（thunk 89）。

## 最近完成

- [WU-0263] GLES1 modelview/projection/texture 三套矩阵栈及 7 个浮点 matrix handler 已
  批量闭合；guest matrix 经受检地址完整搬运，栈与非法值 fail closed。Windows-x64 +
  ANGLE 全量 CTest 403/403 与 exact-APK smoke 已通过，下一边界为 `glMaterialfv`。
- [WU-0262] 目标导入的 17 个 GLES1 无指针标量状态入口已批量闭合；共有状态进入
  ANGLE，GLES1-only hint/capability 与 transfer state 显式保存。Windows-x64 + ANGLE
  全量 CTest 402/402 与 exact-APK smoke 已通过，下一边界为 `glMatrixMode`。
- [WU-0261] GLES1 `glClear` 现将 guest `GLbitfield` mask 原样转发当前
  `AngleFrame::Clear`；macOS/arm64 ANGLE 全量 CTest 402/402 与 exact-APK smoke
  已通过，真实目标越过该调用后首个新阻塞为未实现 GLES1 `glEnable`（thunk 37）。
- [WU-0260] GLES1 `glClearDepthf` 现逐位解码 guest `GLfloat` 并转发当前
  `AngleFrame::ClearDepth`；macOS/arm64 ANGLE 全量 CTest 402/402 与 exact-APK
  smoke 已通过，真实目标越过该调用后首个新阻塞为未实现 GLES1 `glClear`
  （thunk 8）。
- [WU-0259] GLES1 `glClearColor` 已逐位解码四个 guest `GLfloat` 并转发当前
  `AngleFrame`；macOS/arm64 ANGLE 全量 CTest 402/402 与 exact-APK smoke 已通过，
  真实目标越过该调用后首个新阻塞为未实现 GLES1 `glClearDepthf`（thunk 11）。
- [WU-0258] GLES1 `glShadeModel` 现以显式 fixed-pipeline context state 保存受检
  `GL_FLAT` / `GL_SMOOTH`；真实目标已越过该调用，首个新阻塞为未实现 GLES1
  `glClearColor`。全量 CTest 402/402 通过。
- [WU-0257] guest `GetByteArrayRegion` 现从统一 primitive array store 受检复制 byte
  区间到 guest 内存，并解码 A32 第 5 栈参数；真实目标已越过该槽，首个新阻塞为未实现
  GLES1 `glShadeModel`。全量 CTest 401/401 通过。
- [WU-0256] guest `GetArrayLength` 现从 session 持有的统一 primitive array store
  返回真实长度；真实目标已越过该槽，首个新阻塞为未绑定 JNI `GetByteArrayRegion`。
  全量 CTest 400/400 通过。
- [WU-0255] Profile 引用的通用 direct-asset handler 与 APK `assets/` 已接入
  Android guest call session；真实目标已越过 `resource.load_full`，首个新阻塞为未绑定
  JNI `GetArrayLength`。全量 CTest 399/399 通过。
- [WU-0254] 通用直接资源 HLE 现按调用方 implementation id 从受检 APK VFS 提供
  full/range/length，并把结果发布为统一 JNI byte array。全量 CTest 399/399 通过。
- [WU-0253] guest `CallStaticObjectMethod` 现按 descriptor 解码 A32 variadic 参数并
  进入统一 invocation engine；真实目标已越过未绑定槽，首个新阻塞为 Profile Java
  method 尚无注册 handler。全量 CTest 397/397 通过。
- [WU-0252] guest `NewStringUTF` 现通过受检 guest C string、M3 Modified UTF-8 解码
  与统一 string store 发布 local reference；真实目标已越过该调用，首个新阻塞为未绑定
  JNI `CallStaticObjectMethod`。全量 CTest 396/396 通过。
- [WU-0251] GLES1 `glScissor` 现通过隔离 fixed-pipeline dispatch 转发当前 ANGLE
  frame，并复用受检超采样换算；真实目标已越过该调用，首个新阻塞为未绑定 JNI
  `NewStringUTF`。全量 CTest 395/395 通过。
- [WU-0250] GLES1 `glViewport` 现通过隔离 fixed-pipeline dispatch 转发当前 ANGLE
  frame，并复用受检超采样换算；真实目标已越过该调用，首个新阻塞为未实现
  `glScissor`。全量 CTest 395/395 通过。
- [WU-0249] activity native init 经 JADX 与 ELF 共同确认的 5 个 static Java method
  已进入纯 Profile 数据；真实目标已推进至 startup call 4，首个新阻塞为未实现 GLES1
  `glViewport`。全量 CTest 395/395 通过。
- [WU-0248] GLMediaPlayer native init 经 JADX 与 ELF 共同确认的 25 个 static Java
  method 已进入纯 Profile 数据；真实目标已推进至 startup call 3，首个新阻塞为未声明
  `sendAppToBackground()V`。全量 CTest 395/395 通过。

## 目标 ELF 尚未实现的 GL 入口

以下清单以 `docs/demo/games/libasphalt5.so` 的 62 个 GL import 与 WU-0263 后的显式
GLES1 handler 对照得出；当前已实现 30 个，尚余 32 个。它只表示该目标实际导入且尚未
实现的入口，不代表完整 GLES1 命名空间。exact-APK 当前最先命中的是 `glMaterialfv`
（thunk 89）。

- 固定管线状态/查询（14）：`glAlphaFunc`、`glClientActiveTexture`、`glColor4f`、
  `glColor4ub`、`glFogf`、`glFogfv`、`glGetFloatv`、`glLightModelfv`、`glLightf`、
  `glLightfv`、`glMaterialf`、`glMaterialfv`、`glTexEnvfv`、`glTexEnvi`。
- 纹理资源/查询（7）：`glCompressedTexImage2D`、`glCopyTexImage2D`、
  `glDeleteTextures`、`glGenTextures`、`glGetString`、`glTexImage2D`、
  `glTexSubImage2D`。
- Client array/draw（8）：`glColorPointer`、`glDisableClientState`、`glDrawArrays`、
  `glDrawElements`、`glEnableClientState`、`glNormalPointer`、`glTexCoordPointer`、
  `glVertexPointer`。
- Matrix-palette 扩展（3）：`glCurrentPaletteMatrixOES`、
  `glMatrixIndexPointerOES`、`glWeightPointerOES`。

## 下一步（按优先级）

1. 批量闭合 lighting/material/fog fixed-pipeline 状态，当前首个入口为 `glMaterialfv`。
2. 随后按批次闭合 client array/draw 与 matrix-palette extension。
3. 重跑真实 APK 的受帧数约束 smoke，直到获得首个 managed ANGLE frame。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
