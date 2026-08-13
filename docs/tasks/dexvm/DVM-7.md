# DVM-7..12 · dexvm 阶段 1 内核（类链接/对象模型/解释器）

一批六个设计 WU 在同一会话交付（依赖强耦合，拆分只会产生不可编译的中间态）；
对应设计 06 §1 阶段 1 的完整出口。

## 交付内容

| WU | 内容 | 落点 |
| --- | --- | --- |
| 007 | 类链接：注册/层级/布局/vtable/iftable/常量池解析缓存/懒结构预检 | `class_linker.cpp`、`method_precheck.cpp` |
| 008 | JavaObjectModel + GC-A 预算 arena（字符串/基元数组委托存量 store） | `object_model.cpp` |
| 009 | 帧/tagged 寄存器/分派 + moves/const/goto 家族 | `interpreter.cpp`、`interp_exec.cpp` |
| 010 | 算术/逻辑/比较/条件分支家族（NaN 偏置、MIN/-1、移位掩码、饱和转换） | `interp_arith.cpp` |
| 011 | 数组/实例/静态字段/switch/payload 家族 | `interp_object.cpp`、`interp_exec.cpp` |
| 012 | invoke 三路由、异常展开、`<clinit>` 状态机、core intrinsic | `interp_object.cpp`、`interpreter.cpp`、`core_catalog.cpp`、`interpreter_builtins.cpp` |

## AOSP 参考（07 §2 模式 B）

指令语义对照 `vm/mterp/c/OP_*.cpp`（测试注释逐条记录）；vtable/初始化状态机
对照 `vm/oo/Class.cpp` `dvmInitClass`；catch 匹配对照 `vm/Exception.cpp`
`dvmFindCatchBlock`；预检子集对照 `vm/analysis/CodeVerify.cpp`；可赋值性对照
`vm/oo/TypeCheck.cpp`。只取语义，不取结构。

## 出口验收（机器可判定，已过）

- `tests/dexvm/interpreter_tests.cpp` 一致性套件 10 用例/194 断言全绿：
  div MIN/-1、除零 ArithmeticException（含帧内 catch）、cmpl/cmpg NaN 偏置、
  移位掩码、float→int 饱和、窄化截断、long 乘法回绕、packed/sparse switch、
  fill-array-data、AIOOBE/NPE/CCE 真实语义、virtual/super/interface 三种
  dispatch、字段读写、`<clinit>` 一次性 + 静态初始值、跨帧异常带消息与栈
  回溯、StackOverflowError 可捕获、tick 预算结构化失败、OOM 真实抛出。
- full CTest 549/549 无回归（macOS/arm64 dev preset）。

## 遗留（显式）

- monitor 为单线程重入计数（阶段 4 扩展 wait/notify + 存量 monitor table）。
- native 方法需 `NativeMethodBridge`（DVM-14 出向编组接 A32 执行器）。
