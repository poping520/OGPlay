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

## 最近完成

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
- [WU-0247] Profile lifecycle 现把 Java declarations 与 native-call receiver 装入
  Android guest 的同一 JNI class registry；真实目标已推进至 startup call 2，首个新阻塞
  为未声明 `isSoundLoaded(II)I`。全量 CTest 395/395 通过。
- [WU-0246] guest `GetStaticMethodID` 现按真实 class reference、受检 name/descriptor
  与统一 registry 的 static kind 精确查询；未声明不返回伪造 ID。全量 CTest 395/395 通过。
- [WU-0245] Profile Java method 现显式声明 instance/static kind，C++/Python/schema
  同步验证并按种类注册统一 JNI class registry。全量 CTest 394/394 通过。
- [WU-0244] `run-apk` 现可对 `gl_surface_view` 精确 Profile 组合 Android guest
  call session、声明式 phases、SDL input 与 managed ANGLE frame；真实目标已进入
  startup call 1，首个阻塞为未绑定 `GetStaticMethodID`。全量 CTest 394/394 通过。
- [WU-0243] `gl_surface_view` Profile 的 startup/resume/input/frame/pause/shutdown
  调用批次现与 managed surface 组成单一失败粘滞生命周期，且只依赖通用 frame executor。
  Windows/MSVC warnings-as-errors 构建及全量 CTest 394/394 通过。
- [WU-0242] Java/GLSurfaceView lifecycle 现可显式拥有 ANGLE pbuffer，并通过严格
  open/present/close 复用统一超采样 resolve 与 GPU 证据；guest EGL 不得替换或终止。
  Windows/MSVC warnings-as-errors 构建及全量 CTest 393/393 通过。
- [WU-0241] 通用 Android guest call session 已组合真实 Bionic namespace、API 19
  process、syscall/clone、guest JNI core、Android HLE 与 ELF init/fini，并只接受
  通用 A32 frame。Windows/MSVC warnings-as-errors 构建及全量 CTest 392/392 通过。
- [WU-0240] Profile native invocation 批次现先完整预检身份、地址、严格顺序、栈形状、
  进程内存与预算，再按声明顺序进入统一 A32 executor；结果/失败保留精确调用身份。
  Windows/MSVC warnings-as-errors 构建及全量 CTest 391/391 通过。
## 下一步（按优先级）

1. 绑定目标实际触达的 guest JNI `GetByteArrayRegion`，受检复制到 guest 内存。
2. 按真实调用顺序继续闭合 JNI 与 GLES1 fixed-pipeline handler。
3. 重跑真实 APK 的受帧数约束 smoke，直到获得首个 managed ANGLE frame。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
