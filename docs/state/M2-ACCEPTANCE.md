# M2 Bionic 与 Syscall 验收

日期：2026-08-04

## 结论

M2 的 ELF32/ARM linker、API 19/22/23 真实 AOSP Bionic、选择性热点拦截、Android ARM
syscall 目录与统一 VFS 已经闭合。一个导出普通 C 入口的无界面 NDK `.so` 在三个宿主
平台和三个 Bionic profile 上完成 pthread、文件 IO 与 malloc/free，不依赖 Activity、JNI
或 GLES。最终提交在 Windows/MSVC、Linux/GCC、macOS/AppleClang 上均以
warnings-as-errors 构建成功并通过 CTest 159/159。

## 路线图出口条件

| 出口条件 | 结果 | 证据 |
| --- | --- | --- |
| 完整 ELF linker | 通过 | 多模块事务装载、ARM REL、符号版本、TLS、exidx、生命周期、`dlopen/dlsym/dlclose/dladdr` 契约测试 |
| API 19/22/23 真实 Bionic | 通过 | 三版本 libc/libdl 多模块装载、重定位和导出符号自检 |
| 选择性拦截 | 通过 | memcpy/memmove/memset/memcmp/strlen 生产 handler、行为测试与吞吐基准；pthread 保留真实 Bionic ABI |
| Android ARM syscall | 通过 | 120 项分组目录；未实现项统一返回 ENOSYS 并可观测，样本轨迹无未实现命中 |
| APK/OBB/外置统一 VFS | 通过 | 大小写不敏感索引、来源/权限、隔离 fd、事务挂载和跨平台路径测试 |
| 无界面 NDK 累计样本 | 通过 | 三版本入口返回 0，创建一个真实 child，完成 malloc/free 与 32 字节 VFS 写入读回 |
| 三平台出口 | 通过 | Windows/MSVC、Linux/GCC、macOS/AppleClang，CTest 159/159 |

## Work Unit 范围

- `WU-0041..0050`：Bionic profile/oracle、增量远端流程和 ELF 解析、装载、重定位与命名空间。
- `WU-0051..0063`：syscall/VFS 基线、Bionic 路由、TLS/版本/lifecycle 和动态 linker。
- `WU-0064..0078`：guest TLS、Bionic TLS block、线程生命周期、SVC、clone 和真实 child。
- `WU-0079..0098`：多模块总装、真实 Bionic 自检、120 项目录、统一 VFS、无头 NDK
  累计样本及实际执行轨迹闭环。
- `WU-0099..0103`：三平台出口、GCC/Clang 严格告警修复和固定 4 KiB Android guest 页。

完整任务单位于 `docs/tasks/m2/`。关键决定见 ADR-0002、ADR-0009、ADR-0010 和
ADR-0011。

## 页面与线程补充结论

- Android guest 页固定为 4 KiB；宿主后备按实际 4/16 KiB 页面提交。
- 共享同一 Apple Silicon 16 KiB 宿主页的相邻 guest 页仍具有独立映射与权限语义。
- pthread 代码继续由真实 Bionic 执行，只在 clone/futex/TLS syscall 边界映射宿主真线程。

## 明确不属于 M2

- JNI 类型、引用、异常、方法调用、线程附着与 Java 框架对象：M3。
- Activity 生命周期、ANGLE/SwiftShader、EGL/GLES、画面和输入累计集成：M4。
