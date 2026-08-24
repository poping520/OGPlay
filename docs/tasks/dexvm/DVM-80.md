# DVM-80 · Intrinsic family TU 与 ownership 全量收敛

## 目标（一句话）

在不改变任何 guest-visible 行为的前提下，把 DexVM core 与 Android integration 的
class-per-cpp 全量归并为设计规定的 API family TU，并将非 Android Java family 的源码
ownership 清回 core。

## 依赖

- DVM-79：core family TU、per-VM runtime 与 Android-to-core ownership 迁移先例。
- [`12-api19-capability-stack.md`](../../design/dexvm/12-api19-capability-stack.md)：目标布局、
  ownership 与架构门禁。
- `src/runtime/dexvm/intrinsics/MODULE.md` 与
  `src/runtime/integration/dexvm_android/MODULE.md`。

## 范围

- core intrinsic 收敛为 catalog + lang/classloading/reflect/io/util/regex/zip/nio/net/xml/
  concurrent family TU。
- Android integration 收敛为 catalog/shared/UI support + 15 个 Android API family TU。
- `java.*`、`javax.net.*`、`javax.xml.*`、`org.xml.sax.*` 源码从 Android integration
  迁入 core；需要 Android 会话事实的既有行为只经窄 services 注入。
- 保留每个 class 独立 `Declare_*()`、catalog descriptor 相对顺序、handler、字段、常量、
  异常和 side state 行为。
- 新增 layout checker，锁定 family 文件白名单、namespace ownership、catalog-only 注册点、
  declaration 唯一性和 core 不依赖 `DexVmAndroidContext`。
- 本 WU 经用户明确授权忽略通常的单 WU 10 文件与单文件 800 行限制。

## 不做

- 不新增或补齐任何 Java/Android API。
- 不修改 Context hierarchy、NIO、GLES、AudioTrack、Clock 或 scheduler 语义。
- 不借归并重构 handler、side table、字段访问或 catalog 装配顺序。
- 不运行全量 CTest；全量回归留到本阶段最后一个 WU。

## 验收

- 两个 intrinsic 目录只剩设计白名单内的 `.cpp`，无 class-per-cpp 或 ownership 漂移。
- catalog 的 descriptor、super/interface、method、field、constant 集合与迁移前一致。
- Windows `windows-msvc` configure/build 通过。
- core catalog、Android catalog、GC、JNI、IO/VFS/ZIP、EGL/UI 与 architecture 定向回归通过。
- `capabilities.toml` 不推进能力状态；MODULE、任务索引和 CURRENT 同步。

## 结果

- core intrinsic 从 45 个 `.cpp` 收敛为 12 个，Android integration 从 152 个收敛为
  18 个，保留 class 级声明与既有 catalog 顺序。
- 22 个 `java.*`/`javax.net.*`/`javax.xml.*`/`org.xml.sax.*` 声明迁回 core；Android
  会话事实经 `CoreIntrinsicServices`/`AndroidCoreIntrinsicServices` 窄适配继续注入。
- `architecture.dexvm_intrinsic_layout` 锁定两个目录白名单、ownership、catalog 纯注册与
  core 依赖方向。
- `capabilities.toml` 无能力状态变化；全量 CTest 按计划留到阶段最后一个 WU。

状态：已完成。
