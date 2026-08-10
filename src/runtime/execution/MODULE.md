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
- 执行循环消费已声明的 Linux SVC；其他 trap 只有显式 HLE handler 返回已处理才继续，
  否则原样上报。clone child 必须继承同一 handler。
- `InvokeA32GuestCall` 只接受非空目标、运行中线程、8 字节对齐栈和非零 tick 预算；
  r0-r3 与栈参数一次装配，Linux SVC 和显式 HLE trap 复用统一分派，且只允许在受检
  `SVC #1` 返回哨兵结束。长计算按 2000 万 tick 上限切片，并只在切片边界调用显式 observer；
  切片不得改变总 tick 预算。未处理 trap、提前线程退出和预算耗尽均明确失败；预算耗尽
  诊断必须包含 consumed tick、PC 与 LR，供 exact-title 定位有限但昂贵的 guest 路径。

## 测试

对应 guest lifecycle、guest thread runner 和 clone runtime 测试。
