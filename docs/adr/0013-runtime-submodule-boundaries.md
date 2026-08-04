# ADR-0013 · Runtime 子模块边界

日期：2026-08-04

## 背景

`include/ogplay/runtime/` 已积累 31 个公共头文件，`src/runtime/` 有 35 个实现文件，JNI、
框架 HLE、Bionic、syscall、guest 执行和累计装配共处一个目录，模块所有权与依赖方向不再清晰。

## 决定

Runtime 拆为七个子模块，公共头和实现目录保持镜像：

- `jni`：JNI/JavaVM ABI、对象模型、引用、异常、字符串、数组、类、字段和调用。
- `framework`：只依赖 JNI（Asset 额外依赖 VFS）的声明式 Java 框架 HLE。
- `bionic`：API profile、真实库自检、TLS 与选择性 libc 边界。
- `syscall`：ARM Linux syscall、kernel helper、VFS 适配和 guest 线程退出状态。
- `execution`：guest init/fini、线程执行循环和 clone 后的宿主线程运行。
- `vfs`：与 Android 路径和挂载来源有关、但不依赖 JNI/syscall 的文件系统核心。
- `integration`：无界面累计 runner/contract，只负责装配，不提供低层能力。

依赖方向固定为：

`integration -> framework -> jni`，`integration -> execution -> bionic`，
`integration -> execution -> syscall -> vfs`；`framework` 只允许 Asset 子域额外依赖 `vfs`。

`guest_thread_lifecycle` 归入 `syscall`，因为 syscall 声明直接拥有 exit/clear-child-tid 状态；
`guest_thread_runner` 与 `guest_clone_thread_runtime` 归入 `execution` 并单向依赖 syscall，避免循环。

## 迁移规则

- 每个 `src/runtime/<submodule>/` 必须有独立 `MODULE.md`。
- 公共头迁移到 `include/ogplay/runtime/<submodule>/`，实现目录与其镜像。
- 每个 WU 只迁移一个可独立验证的切片；旧 include 转发头只能临时存在并必须可搜索。
- 先完成目录迁移，再按相同边界拆分 CMake target；不得在同一 WU 混合行为变更。

## 后果

模块契约和物理目录一致，后续可以用构建 target 强制依赖方向。迁移期间 include 路径会逐步
变化，但每个提交保持可构建、可测试和可回滚。

## 实施状态

`WU-0146` 迁移 VFS，`WU-0147` 按用户授权以单个纯机械 WU 迁移其余公共头、实现、include
与 CMake 路径。runtime 根目录不再保留散落生产文件，文档布局测试对此持续门禁。
