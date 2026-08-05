# 子模块：runtime/integration

## 职责

装配 Bionic、JNI 与 Android native 边界，生成里程碑出口报告；本模块只协调已有能力，
不实现新的 syscall、JNI、框架或文件系统语义。

## 依赖

位于 runtime 顶层，可依赖 framework、jni、bionic、syscall、execution、vfs 及其下层模块。
任何下层模块均不得反向依赖 integration。

## 不变量

- runner 只有在所有资源、引用、线程和生命周期闭环后才能报告成功。
- `AndroidBoundaryHle` 从生成目录暴露完整 142 项 GLES2 Thumb trap 命名空间；只有显式
  handler 可以执行，未实现调用必须携带函数名失败。Looper/input 数据与 ANGLE readback
  跨线程传递必须受锁保护，未知地址或 SVC 不得吞掉。
- 超采样倍率必须在创建任何 ANGLE 资源前完整验证；viewport 缩放溢出明确失败，guest
  `eglQuerySurface` 不得泄漏内部渲染尺寸，GPU 查询不得把逻辑尺寸伪装成真实 target。
- `NativeActivitySession` 只接受 API 19 ARMv7 当前入口，执行真实 Bionic 初始化、
  `ANativeActivity_onCreate`、glue child 与完整销毁回调；阶段可由可选 observer 查询。
- guest child 异常必须唤醒同步生命周期 waiter，并在 root 继续执行、帧或输入边界转为带
  原始原因的 `NativeActivityRunError`；禁止 SDL 主线程无限等待已死亡渲染线程的首帧。
- `NativeActivityRunRequest::supersample_factor` 选择受检 1..4× 内部渲染倍率；guest 的
  EGL surface 和输出帧保持逻辑尺寸，ANGLE pbuffer、viewport 与 GPU render target 使用
  放大尺寸，swap 时通过 gles 确定性 resolve 还原。
- `NativeActivitySession` 实现 core GPU provider，快照只报告真实发生的 clear、默认 FBO、
  ANGLE 后端和最近 2048 条 EGL/GLES 调用；未查询到的扩展、限制和 guest FBO 不伪造。
- executor、时钟、VFS 和 profile 必须显式注入或由确定性 fixture 建立。
- 不包含平台 UI、真实 present 或游戏专属逻辑。

## 测试

对应 `headless_bionic_runner_tests.cpp`、`headless_jni_contract_tests.cpp` 与
`android_boundary_hle_tests.cpp`。
