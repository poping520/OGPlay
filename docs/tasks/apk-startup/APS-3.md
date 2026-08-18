# APS-3 · Rootless Android guest process shell

## 目标（一句话）

把 `AndroidGuestCallSession` 中“创建 Android native 进程”与“加载应用 root ELF”拆开，
允许先得到不含任何 APK application `.so` 的可执行 process shell。

## 依赖

- APS-2
- `docs/design/apk-startup/03-target-architecture.md`

## 设计锚点

- 03 §3–5
- 02 §3.2

## 语义出处

本 WU 主要是 OGPlay 内部架构重构；不需要照搬 AOSP。若调整 Bionic process init
顺序，记录对应现有 MODULE/runtime contract，而不是凭空改变。

## 变更

- 抽出 `AndroidGuestProcess` 或等价 rootless create API；
- guest address space/Bionic/syscall/boundary/JNI 可在无 app module 时就绪；
- ELF namespace 生命周期从“一次 Start 输入”提升为 process lifetime；
- 保留 legacy adapter 让旧 root-module 路径暂时可跑；
- 不实现 dynamic app load（留给 APS-4）。

## 验收（机器可判定）

- 单测可创建/停止 rootless process shell；
- 创建后 APK application module count = 0；
- JavaVM/JNI thread attach 基础操作可用；
- legacy session tests 通过 adapter 无回归；
- clean stop 无 module/fd/thread 泄漏断言回归。

## 结果（机器可判定，已达成）

- `AndroidGuestProcess::Start` 只接收已选 API 的 system module closure，以 `libc.so`
  建立 process-lifetime ELF namespace；请求面没有 application root/module 字段，启动后
  `ApplicationModuleCount() == 0`。
- process shell 独立拥有 address space、Bionic process memory、syscall/boundary、JNI
  guest ABI、JavaVM、root CPU/thread lifecycle 与 system ELF init/fini；根 JNI 线程在
  create 时 attach，在 `Stop()` 时 detach，重复 Stop 幂等。
- `AndroidGuestCallSession` 已降为 legacy adapter：旧 root-module request 经私有
  `StartLegacy` 进入同一个 process owner，frontend 调用面保持不变；APS-3 不提供任何
  dynamic application ELF API，该能力仍归 APS-4。
- 确定性最小 `libc.so` fixture 验证 rootless create/stop、0 个 application module、
  process-lifetime namespace、JavaVM/JNI attach 清理，以及 legacy adapter 实际启动/停止。
- Windows MSVC：`cmake --preset windows-msvc`、Release `ogplay_tests` build 与完整
  `ctest --preset windows-msvc` 通过，790/790。
