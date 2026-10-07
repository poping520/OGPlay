# WU-PERF-13 · 原生并行与帧吞吐

目标：保持 2560×1600、画质和正常游戏时间，将关卡/对话实际成功呈现帧率提升至
80 FPS 或更高。以 WU-PERF-12 提交为基线，同 seed、场景、核数及无 diag 参数对照。

范围：先将匿名 JIT 采样归因到 guest 指令和通用执行机制，再优化实证热点；不添加
游戏专属生产分支，不通过重复呈现旧帧、跳过逻辑、降低分辨率或时间倍率计数。

依赖：共享 exclusive monitor、直接访存生命周期、统一 Clock、ANGLE 与 SDL3。
改变契约/架构时补 ADR 和相应 MODULE；局部实现复用定向回归。

验收：相关语义回归通过；Release 同场景实际成功 present 对照及画面/交互检查；
移除临时取证 hook，记录残余缺口。未实测达到目标不得声明完成。

状态：已完成本阶段初始关卡/对话的 80 FPS 性能目标（2026-10-07，4 核）。
基线提交 c73df73f；同 seed/分辨率/核数最终对照见 `.local/tales-fps80-20261006/report.md`。

当前实现：ARM64 aligned single-page exclusive 读侧 writer epoch 校验、原子 reservation
发布与 128 字节 processor 槽隔离；写侧锁、真实 CAS、peer 失效和 callback 路径保留。
整数 exclusive 的寄存器准备保持 live FPSR，fallback 发布/恢复；固定 vendor tree 不修改，
影子头统一 ABI 重编译。设计见 [ADR-0101](../../adr/runtime.md#adr-0101)。

机器验证：Release `ogplay`/`ogplay_tests` 受影响目标构建；27 项 Dynarmic / 767 断言通过，
覆盖 scalar 各宽度、同值写、exclusive ABA、direct/callback 混用、共享/独立多线程计数、
稀疏槽位/复用、权限 fault 及 FPSR 回调污染/软件浮点边界。验收仅定向；既有无关门禁失败
沿用 WU-PERF-12 记录。最终 initializer 顺序整理后，两份二进制 SHA256 均与已测版本一致。

实测：4 核无 diag，对话 93.32 FPS；教学连续 15 秒窗口 88.25/86.75/85.05 FPS，
50 次 swipe 的关卡移动窗口 86.08 FPS。各窗口源序号差等于成功 present 次数差，运行
`guest_fault=null`；截图确认原尺寸、对话/教学及实际移动。3 核对话 88.04/88.44/88.78
与新进程 89.77 FPS；教学稳态 76.44 FPS、移动 82.61 FPS，因此推荐 4 核。

最终基线复验：4 核 c73df73f 对话 54.70、教学稳态 50.33、同 50 次 swipe 移动 51.21 FPS；
优化分别为 93.32、86.75、86.08 FPS。旧核数对照 3 核 69.38/69.57，4 核 56.85/57.26；
优化后 4 核对话超过 3 核，负扩展已在该对话场景改善。不是截图单点 FPS 的直接差值。

收尾：临时 JIT dump hook 已移除，所有分析进程已关闭，用户沙盒未改。退出仍为既有
NativeOnDestroy 读0x1/超时；本次未修复。性能结论限上述初始场景与本机 Release，
未完成全量、完整关卡/title gate 或跨平台验收，不据此声明全游戏兼容。
