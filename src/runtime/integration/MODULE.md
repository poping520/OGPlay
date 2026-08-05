# 子模块：runtime/integration

## 职责

装配 Bionic、JNI 与 Android native 边界，生成里程碑出口报告；本模块只协调已有能力，
不实现新的 syscall、JNI、框架或文件系统语义。

## 依赖

位于 runtime 顶层，可依赖 framework、jni、bionic、syscall、execution、vfs 及其下层模块。
任何下层模块均不得反向依赖 integration。

## 不变量

- runner 只有在所有资源、引用、线程和生命周期闭环后才能报告成功。
- `AndroidBoundaryHle` 只暴露已登记的 Thumb trap；Looper/input 数据与 ANGLE readback
  跨线程传递必须受锁保护，未知地址或 SVC 不得吞掉。
- `NativeActivitySession` 只接受 API 19 ARMv7 当前入口，执行真实 Bionic 初始化、
  `ANativeActivity_onCreate`、glue child 与完整销毁回调；阶段可由可选 observer 查询。
- `NativeActivitySession` 实现 core GPU provider，快照只报告真实发生的 clear、默认 FBO、
  ANGLE 后端和最近 2048 条 EGL/GLES 调用；未查询到的扩展、限制和 guest FBO 不伪造。
- executor、时钟、VFS 和 profile 必须显式注入或由确定性 fixture 建立。
- 不包含平台 UI、真实 present 或游戏专属逻辑。

## 测试

对应 `headless_bionic_runner_tests.cpp`、`headless_jni_contract_tests.cpp` 与
`android_boundary_hle_tests.cpp`。
