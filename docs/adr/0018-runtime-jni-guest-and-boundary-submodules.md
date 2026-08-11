# ADR-0018 · Runtime 拆出 jni_guest 与 boundary 子模块

日期：2026-08-11

## 背景

ADR-0013 将 runtime 拆为七个子模块时,`integration` 的定位是"无界面累计 runner/contract,
只负责装配,不提供低层能力"。M5..M8 期间,guest JNI ABI 边界(约 3200 行)与
Android native/GLES 边界(约 5400 行)全部落入 `integration`,使其增长到 32 个实现文件、
超过 1 万行,MODULE.md 不变量超过 300 行,已无法通读核对;"只装配"的契约与代码事实
持续背离。多个文件(`android_boundary_hle.cpp`、`android_boundary_gles.cpp`、
`android_guest_call_session.cpp`)超过 800 行上限。

## 决定

从 `integration` 拆出两个新的 runtime 子模块,公共头与实现目录保持镜像:

- `jni_guest`:guest 侧 JNI ABI 物化与绑定。包含 JNIEnv/JavaVM 表映射(`jni_guest_abi`)、
  SVC trap 分派(`jni_guest_dispatch`)、全部 slot binding family(core/static call/
  static field/string/array)与 root `JNI_OnLoad` 生命周期。依赖 `jni`、`execution`、
  loader、memory、cpu 及以下;不依赖 boundary 与 integration。
- `boundary`:Android native 边界。包含 `android_boundary_hle` 主分派、GLES2/GLES1
  边界组件、boundary symbol 目录、`GuestGlContext` 共享 GL 状态与 `A32CallFrame`。
  依赖 gles 模块、memory、cpu、loader 及以下;不依赖 `jni` 与 `jni_guest`。

`integration` 收敛为装配层:API 19 guest process、Android guest call session 及其
Java handler 绑定、link preflight、headless/NativeActivity runner 与累计 contract。

依赖方向更新为:

`integration -> jni_guest -> jni`,`integration -> jni_guest -> execution`,
`integration -> boundary -> (gles 模块)`;其余方向沿用 ADR-0013。
`jni_guest` 与 `boundary` 互不依赖。

## 迁移规则

- 沿用 ADR-0013 先例:纯机械迁移,不改 `ogplay::runtime` 命名空间、不改任何行为;
  经用户授权,每个子模块以单个机械 WU 一次迁完,提交保持可构建、可测试、可回滚。
- MODULE.md 不变量随文件迁移到对应新契约;`integration` 契约同步收敛,总量只减不增。
- `cmake/CheckDocumentationLayout.cmake` 的子模块清单同步追加 `jni_guest` 与 `boundary`。
- 会话拥有的 Java handler 状态(platform/movie/media)保留在 `integration`:迁往
  `framework` 会引入 framework 对 `jni_guest` 对象模型的向上依赖,违反 ADR-0013;
  待 session 级 Java object-model 统一(现有 backlog)后再评估。

## 后果

`integration` 从 32 个实现文件收敛到约 11 个,三份契约分别可通读;后续可用独立
CMake target 强制 `boundary` 不依赖 JNI、`jni_guest` 不依赖 GLES。include 路径一次性
变化,由同一 WU 内的构建与全量测试兜底。
