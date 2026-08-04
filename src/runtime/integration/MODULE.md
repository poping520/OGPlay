# 子模块：runtime/integration

## 职责

装配无界面 Bionic 与 JNI 累计契约，生成里程碑出口报告；本模块只协调已有能力，不实现新的
syscall、JNI、框架或文件系统语义。

## 依赖

位于 runtime 顶层，可依赖 framework、jni、bionic、syscall、execution、vfs 及其下层模块。
任何下层模块均不得反向依赖 integration。

## 不变量

- runner 只有在所有资源、引用、线程和生命周期闭环后才能报告成功。
- executor、时钟、VFS 和 profile 必须显式注入或由确定性 fixture 建立。
- 不包含平台 UI、真实 present 或游戏专属逻辑。

## 测试

对应 `headless_bionic_runner_tests.cpp` 与 `headless_jni_contract_tests.cpp`。
