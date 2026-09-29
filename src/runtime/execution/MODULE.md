# 子模块：runtime/execution

## 职责

执行 guest ELF init/fini 生命周期，运行单个 guest CPU 线程，并把 Bionic clone 请求提交为一个
guest 线程对应一个真实宿主线程。

## 依赖

单向依赖 `runtime/bionic`、`runtime/syscall`、`loader`、`cpu`、`memory` 与 `hal`；不得依赖
JNI、framework 或 integration。

## 不变量

- `GuestCloneThreadRuntime` 可接收窄诊断状态，在 child 宿主线程执行 guest runner 的整个
  驻留期登记 host_tid/guest_tid/PC，每个 budget slice 更新 PC/last progress，并以 RAII
  保留退出 tombstone；关闭时不登记。
- clone 执行抛出宿主异常时先保存首错，再请求进程线程组退出并中断 futex；完成故障线程
  的退出清理，通过显式 notifier 取消上层其他等待。`RethrowFailure` 与 join 保留原异常，
  不允许已退出宿主线程仍保持 guest running、直到 join 才暴露失败。
- 启用 signal runtime 时，主调用与 clone runner 每次 CPU 运行前投递信号，纯计算最多
  50000 tick 检查一次；目标 CPU 只由其所属宿主线程修改。clone 发布 child 前继承 mask，
  join 后退役信号状态，pending 与备用栈不继承。
- init array 正序、fini array 逆序，调用前完整验证。
- child 从 parent CPU 状态派生，r0、SP、TLS、TID 和退出清理必须精确。
- 执行循环消费已声明的 Linux SVC；其他 trap 只有显式 HLE handler 返回已处理才继续，
  否则原样上报。clone child 必须继承同一 handler。
- CPU `host_call_fault` 是 noexcept fast callback 的结构化退出；执行循环必须先交回显式
  HLE handler 取出 pending exception，不能把它折叠成普通未处理 CPU stop。
- 外部退出请求可发生在 child 首次执行前或任意 budget slice 之间；runner 必须在下一安全
  边界完成 lifecycle exit 与 clear-child-tid/futex 清理，不能再次以 running 前置条件失败。
- `InvokeA32GuestCall` 只接受非空目标、运行中线程、8 字节对齐栈和非零 tick 预算；
  r0-r3 与栈参数一次装配，Linux SVC 和显式 HLE trap 复用统一分派，且只允许在受检
  `SVC #1` 返回哨兵结束。长计算按 2000 万 tick 上限切片，并只在切片边界调用显式 observer；
  observer 接收当前调用实际累计 tick，SVC 安全边界同样报告该累计值；回调次数不得被解释
  为 tick。切片不得改变总 tick 预算。未处理 trap、提前线程退出和预算耗尽均明确失败；
  预算耗尽诊断必须包含 consumed tick、PC 与 LR，供 exact-title 定位有限但昂贵的 guest 路径。
  guest 在调用中请求退出时，报告必须保留调用 target/累计 tick、退出来源/码/requester、
  syscall PC/LR，以及统一 A32 stop、寄存器和可读时的指令窗口；不得折叠为固定短句。
  同一报告从 r11 按 API 19 GCC frame record 最多展开 16 帧，输出归一化 PC、原始 LR、FP
  及明确停止原因；不可读、非单调或无 frame pointer 只截断回溯，不得遮蔽退出事实。
  JNI native 帧可显式参与 watchdog 续期，但只有分发结果为 `handled_advanced`（真实 park、
  正字节数据 I/O、present/audio enqueue 或 JNI 重入）才续期；查询、EOF、wake/yield、内存
  管理及未分类的已处理边界均为 `handled_idle`，不得因经过边界清零。exit request 检查仍
  独立优先于下一轮执行。
- A32 非正常停止由统一 formatter 报告：execution state 与 stop/fault 枚举同时保留名字和
  数值，指令字/地址/核心寄存器使用固定宽度十六进制；stop、fault、thread、registers 与 code
  使用对齐的独立行，r0-r3 与 r12/SP/LR 分组。报告尽力读取 PC-8 起的有界指令窗口，读取
  失败不得遮蔽原始 stop；PC 低于 8 时保留原错，尽力附加 LR 前指令字节，不推测栈帧。上层可以添加调用边界，但不得重新拼装一份信息更少的 CPU
  fault 文本。

## 测试

对应 guest lifecycle、guest thread runner 和 clone runtime 测试。
