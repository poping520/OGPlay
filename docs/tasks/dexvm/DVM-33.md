# DVM-33 · 解释器热路径 execution 传递（消除逐指令 thread-local 查找）

## 目标（一句话）

把活跃 execution state 的解析从"每条字节码指令做 1..3 次 thread-local
unordered_map 查找"，改为"`Call` 入口解析一次、沿调用链传引用"，行为逐位不变。

## 背景与问题证据

- `Impl::Execution()` 是 `thread_local unordered_map<const void*, ...>` 的
  `find`（`interpreter_context.cpp`）。
- 逐指令调用点：`Step()` 开头一次（`interp_exec.cpp`）、`Tick()` 内部一次
  （`interpreter_internal.h`，每指令必经）；object/invoke 家族指令再经
  `StepObjectOrInvoke()` 开头一次（`interp_object.cpp`）；`sget/sput`、
  `new-instance`、`invoke-static` 还会进 `EnsureInitialized()` 开头一次；
  每次解释方法调用经 `PushInterpretedFrame()` 一次。
- `Run()` 循环入口本已解析过一次 execution，却未向下传递——简单指令
  （move/const/goto）背着 2 次哈希查找执行。
- 正确性前提：`InterpreterExecutionScope` 强制"一次活跃调用内不得切换
  context"，因此整个 `Run` 期间该引用恒定；monitor 停泊
  （`ReleaseForBlocking`）期间线程留在自己的栈帧里，引用不跨线程逃逸。

## 方案（已实施）

`InterpreterExecutionState&` 作为首参沿调用链显式传递：

- `Run(execution, entry_depth)`、`Step(execution)`、
  `Tick(execution, amount = 1)`、
  `StepObjectOrInvoke(execution, frame, opcode, unit)`、
  `PushInterpretedFrame(execution, method, args, caller_advance)`、
  `EnsureInitialized(execution, java_class)`（含 superclass 递归与
  `<clinit>` 帧的 `Run`）。
- `Execution()` 的解析点收敛到每次 `Call`/`EnsureClassInitialized` 入口一次；
  冷路径（`ThrowJava`/`SetPending`/`CaptureStack`/`FailCode`、
  `NativeFrame` 标记）保持原状，不在本 WU 范围。
- 无任何语义改动：不新增状态、不改锁序、不改 tick/stop 判定顺序。

## 边界（不做）

- 不动 invoke 参数封送的 descriptor 逐字符解析与堆分配（另立 WU）。
- 不动 String intrinsic 的整串拷贝（另立 WU）。
- 不改 `Execution()` 本身的 thread-local 路由机制（context 切换语义不变）。

## 验收

- 行为零变化，由既有机器可判定测试锁定：dexasm 一致性夹具（算术边界、
  控制流、三种 dispatch、clinit、跨帧异常、tick/heap 预算、双 context 隔离）、
  vm_thread/vm_monitor 全部用例。
- Windows/MSVC `windows-msvc` 全量 CTest 705/705 通过（含 DVM-32 新增的
  绑定/冻结/重复 miss 回归）。

## 结果（已完成）

- 六个函数签名改为显式接收 `InterpreterExecutionState&`，全部调用点
  （`interpreter.cpp`/`interp_exec.cpp`/`interp_object.cpp`）同步更新；
  逐指令路径不再触碰 thread-local 路由，简单指令每条省 2 次哈希查找，
  `sget/sput` 与 invoke 家族省 3..4 次。
- `MODULE.md` 固化不变量："热路径在 Call 入口解析一次 execution 并沿
  调用链传递；`Execution()` 仅限入口与冷路径"。
- Windows/MSVC 全量 CTest 705/705（`architecture.documentation_layout`
  之前因 CURRENT.md 超限失败，本轮随快照压缩一并修复）。
