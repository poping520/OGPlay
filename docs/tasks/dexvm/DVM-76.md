# DVM-76 · 致命错误自动附带 guest Java 调用栈

## 目标（一句话）

DexVM 在解释执行中抛出不可恢复的 `DexVmError` 时，在销毁帧前把稳定的 guest Java
调用链附加到错误文本，使缺方法等启动阻断可直接定位到 APK 类、方法签名和 DEX PC。

## 依赖

- DVM-52：已有 execution context 帧模型与安全的 Java 栈事实。

## 设计锚点

- `docs/design/dexvm/02-architecture.md` §7..§9。
- `docs/tasks/dexvm/DVM-52.md`：诊断查询面的栈字段口径。

## 变更

1. `Interpreter::Impl::Run` 捕获致命 `DexVmError` 后，先按最内层到最外层渲染当前
   interpreted frames，再执行既有 monitor 释放和帧清理。
2. 标题固定输出 execution context token，并在已注册的 `VmThreadRuntime` 中解析 guest
   thread id/name；未注册时明确标记，不猜宿主线程身份。
3. 首帧额外解码故障 opcode 名称/字节；method-ref 指令同时输出 DEX method index。
   每帧输出 class、完整 method descriptor 与 DEX code-unit PC，不输出宿主地址。
4. 栈按最内层优先最多输出 64 帧，报告总数/已显示数与省略的外层数；native/JNI
   重入导致同一错误跨多个 `Run` 边界时只附加一次栈标题。

## 验收（机器可判定）

- 两层 interpreted 调用在解析缺失 intrinsic 方法时，错误文本包含内层、外层及各自
  `dex_pc`，顺序为最内层优先。
- switch/threaded 两后端输出相同，错误后 execution frame depth 恢复为零。
- 标题包含 context、guest thread id/name；首帧包含 `invoke-static`、opcode 与 method index。
- 71 层栈只输出 `#0..#63`，并报告省略 7 个外层帧。
- Windows `windows-msvc` focused interpreter 与 architecture tests 通过。

## 结果（机器可判定，已达成）

- Windows Debug focused `dexvm fatal errors*` + `dexvm diagnostics*` 6/6
  （336 assertions）：覆盖 switch/threaded 的两层栈顺序、context/thread、fault
  opcode/method index、64 帧上限、单一标题和错误后空帧。
- Release PVZ 关闭 survey 的原命令复跑仍在
  `PackageManager.getApplicationInfo(String,int)` 明确失败，但已输出
  `context=1 thread=<unregistered>`、`invoke-virtual opcode=0x6e method_idx=643 dex_pc=18`
  与 6 层调用链：
  最内层 `BaseCore.loadConfiguration()V dex_pc=18`，最外层
  `PvZActivity.onCreate(Bundle)V dex_pc=107`。
- 完整 Windows CTest 949/951；DVM-76 与 architecture 均通过，两个失败为本次未触及的
  既有断言漂移：String intrinsic 数量期望 43/实际 44、liblog tag 期望 `PVZ`/实际为空。
