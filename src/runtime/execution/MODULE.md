# 子模块：runtime/execution

## 职责

执行 guest ELF init/fini 生命周期，运行单个 guest CPU 线程，并把 Bionic clone 请求提交为一个
guest 线程对应一个真实宿主线程。

## 依赖

单向依赖 `runtime/bionic`、`runtime/syscall`、`loader`、`cpu`、`memory` 与 `hal`；不得依赖
JNI、framework 或 integration。

## 不变量

- init array 正序、fini array 逆序，调用前完整验证。
- child 从 parent CPU 状态派生，r0、SP、TLS、TID 和退出清理必须精确。
- 执行循环只消费已声明的 Linux SVC，其他 trap 原样上报。

## 测试

对应 guest lifecycle、guest thread runner 和 clone runtime 测试。
