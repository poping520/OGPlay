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
- [WU-0239] API 19 root guest 的 TLS/thread-info/preinit、4 MiB 栈、返回 trap 与空
  property area 已抽成事务式通用进程内存契约；固定布局冲突和非法身份失败时逆序回滚，
  libc 导出槽不被污染。Windows/MSVC warnings-as-errors 构建及全量 CTest 388/388 通过。
- [WU-0238] Guest JNI 首批 16 个 JNIEnv 引用/异常基础 slot 与 4 个 JavaVM slot
  已绑定真实 M3 状态；guest 输出指针受检，VM 只发布统一 guest ABI 地址，非空 attach
  arguments 明确失败。Windows/MSVC warnings-as-errors 构建及全量 CTest 385/385 通过。
- [WU-0237] Guest JNI `SVC #3` 已按 trap PC 精确解码并校验 interface receiver，
  发布寄存器/栈调用帧；slot 必须执行前绑定封口，未绑定项按名称和 LR 记账后失败。
  Windows/MSVC warnings-as-errors 构建及全量 CTest 382/382 通过。
- [WU-0236] 通用 A32 guest call executor 已按目标地址选择 A32/Thumb，装入 r0-r3 与
  8 字节对齐栈参数，复用 Linux SVC/显式 HLE 分派并只在受检返回 trap 结束；未处理
  trap、提前线程退出和预算耗尽明确失败。Windows/MSVC warnings-as-errors 构建及
  全量 CTest 379/379 通过。
- [WU-0235] Profile native calls 已按 phase 装配为 A32 JNI 调用帧：r0/r1 固定携带
  JNIEnv 与实例/jclass，显式参数进入 r2/r3 和 8 字节对齐栈；target、class reference、
  input 或 receiver 不完整时不发布部分帧。Windows/MSVC 构建及全量 CTest 375/375 通过。
- [WU-0234] 完整 233 槽 JNIEnv 与 8 槽 JavaVM 已物化为 32 位 guest 函数表、对象和
  独立 Thumb SVC trap；reserved 保持 null，数据页只读、代码页 RX，冲突完整回滚。
  Windows/MSVC 18.8 warnings-as-errors 构建及全量 CTest 372/372 通过。
- [WU-0233] APK preflight 已复用生产 Bionic/Android boundary 完成映射与重定位；真实
  目标报告 5 guest + 2 boundary、6503 relocations、17 native calls。macOS/arm64
  warnings-as-errors 构建及全量 CTest 369/369 通过。
- [WU-0232] `libGLESv1_CM.so` 已追加发布 3 项 matrix-palette extension trap，core
  地址不漂移且独立记账；目标 ELF 的 62 个 GL imports 均有生成目录 provider。
  macOS/arm64 warnings-as-errors 构建及全量 CTest 368/368 通过。
## 下一步（按优先级）

1. 声明 GLMediaPlayer startup 实际查询的方法，并继续绑定后续 JNI guest slot。
2. 逐个绑定目标执行实际触达的 GLES1 fixed-pipeline handler，未触达项继续明确失败。
3. 重跑真实 APK 的受帧数约束 smoke，直到获得首个 managed ANGLE frame。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
