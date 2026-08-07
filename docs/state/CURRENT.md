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
- [WU-0231] 固定 ANGLE extension header 的受检子集目录已覆盖目标 ELF 所需 3 个
  `GL_OES_matrix_palette` 入口，并与 145 个 GLES1 core ID 隔离。macOS/arm64
  warnings-as-errors 构建及全量 CTest 367/367 通过。
- [WU-0230] Android boundary 已在 `libGLESv1_CM.so` 发布隔离的 145 项 core Thumb
  trap；同名 GLES1/GLES2 不混淆，未绑定调用记账并明确失败。macOS/arm64
  warnings-as-errors 构建及全量 CTest 366/366 通过。
- [WU-0229] GLES1.1 core 145 项声明式目录已与固定 ANGLE `GLES/gl.h` 双向核对，
  57 个指针参数均有搬运元数据；GLES1/GLES2 生成物隔离 namespace。macOS/arm64
  warnings-as-errors 构建及全量 CTest 365/365 通过。
- [WU-0228] APK launch planning 已把 Profile native calls 按 JNI short/long 顺序预解析
  为 root ELF 强类型 guest 地址；真实目标 17 项全部命中。macOS/arm64 warnings-as-errors
  构建及全量 CTest 364/364 通过。
- [WU-0227] 受检 Java class/method/descriptor 现可通用生成 JNI short/long native
  导出名，含 Unicode UTF-16 code unit 转义；生产代码不含标题事实。macOS/arm64
  warnings-as-errors 构建及全量 CTest 362/362 通过。
- [WU-0226] 首个 legacy APK 的 startup/resume/frame/pause/shutdown 及 pointer/key
  native 调用序列已作为强类型纯数据进入标题 Profile；生产代码无游戏名或标题特判。
## 下一步（按优先级）

1. 让通用 `gl_surface_view` guest invoker 执行已装配调用帧并处理 JNI SVC trap。
2. 逐个绑定目标执行实际触达的 GLES1 fixed-pipeline handler，未触达项继续明确失败。
3. 用真实 APK 进行受帧数约束的启动/渲染 smoke test，并把首个可测试命令写入验收文档。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
