# 当前状态

更新：2026-08-07 · M5 已打开

## 当前阶段

- M0、M1、M2、M3、M4 已完成并验收。
- M5 去硬编码正在推进；首个精确 legacy title Profile 已闭合 APK 身份、Bionic 依赖、
  native-call 目标/A32 调用帧与 Android link preflight，完整 JNIEnv/JavaVM guest ABI
  也已映射；当前尚未执行 `gl_surface_view` guest process。
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

## 进行中

- [WU-0259] GLES1 `glClearColor` 已绑定为逐位解码四个 guest `GLfloat` 并转发当前
  `AngleFrame`；相邻 `glClear` 仍未绑定。macOS/arm64 ANGLE 全量 CTest 402/402 与
  exact-APK smoke 已通过，真实目标越过该调用后首个新阻塞为未实现 GLES1
  `glClearDepthf`（thunk 11）。Windows/MSVC warnings-as-errors 全量 CTest 尚待执行。

## 最近完成

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

## 下一步（按优先级）

1. 完成 WU-0259 的 Windows/MSVC warnings-as-errors 与全量 CTest，闭合本 WU。
2. 按 smoke 暴露的下一边界绑定 GLES1 `glClearDepthf`。
3. 重跑真实 APK 的受帧数约束 smoke，直到获得首个 managed ANGLE frame。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
