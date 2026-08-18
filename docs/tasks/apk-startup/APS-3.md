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
