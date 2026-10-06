# WU-PERF-10 · 原生同步与呈现吞吐

目标：在原 2560×1600 分辨率、正常游戏时间和画质下，将实际成功呈现帧率提升到
60 FPS。以 WU-PERF-09 提交为基线，同存档、同场景、同诊断参数对照。

范围：按采样证据减少原生同步/JIT 热点及帧呈现等待；不添加游戏专属生产分支，
不通过重复呈现旧帧、跳过逻辑、缩小分辨率或时间倍率制造 FPS。

依赖：既有直接访存与 exclusive monitor 语义、ANGLE EGL/GLES、SDL3 和统一 Clock。
涉及接口或架构变化时追加 ADR 与对应模块契约；局部优化复用相关定向回归。

验收：相关机器可判定回归通过；Release 同场景实际 FPS 对照、画面与交互检查；
临时取证代码移除，记录剩余缺口。60 FPS 未实际达到不得宣布完成。

状态：本阶段已收尾，按用户要求暂停后续优化；全场景 60 FPS 目标未完成。
证据 `.local/tales-perf-20261006/`。

已实现：ARM64 页内 inline exclusive/保守 processor 高水位扫描；主 Looper 复用
已绑定 root Thread；Metal 私有 PBO 同步读回；MCP/Dashboard 增加真实成功呈现计数；
普通页表访存与链接块保留 live FPSR，wrapped fallback 保护 C++ 可能改写的浮点异常状态；
Run 保存/恢复宿主 FPSR，软件浮点、查询及改写边界保持 guest 累积异常。
设计：[ADR-0097](../../adr/runtime.md#adr-0097)、[ADR-0098](../../adr/runtime.md#adr-0098)。

当前对照：原提交加计数器的 2 核基线 36.71 FPS；inline exclusive/Looper/PBO 的
2 核组合 38.06 FPS、4 核 31.20 FPS。均为 2560×1600、同存档第一段对话、MCP
无 `--diag`、15 秒真实成功 present 计数；两核稳态窗口源序号差与成功次数差一致。
普通访存 FPSR 保留 44.05 FPS、邻接 NZCV 转发 44.52 FPS；
跨块保留后 2 核 57.90 FPS；3 核首轮 59.99 FPS，连续三个 15 秒窗口为
60.05/59.26/60.00 FPS。进入原截图对应教学场景后，3 核为 49.99 FPS。
因此对话接近 60 FPS，教学场景仍未达标。后续应优先对教学场景重新采样；
前期仅以源序号估算的探索不是正式 60 FPS 验收。

定向证据：`metrics-tests.log` 76 项/2016 断言、`pbo-tests.log` 72 项/2350 断言、
`fpsr-tests.log` 70 项/2136 断言通过，`persistent-fpsr-tests.log` 72 项/2166 断言通过（不同过滤范围，非累计测试数量）。新回归覆盖
高 ID/退役复用及 direct/callback exclusive peer、空消息泵不阻塞 VM 锁、PBO pack
状态/方向/原缓冲内容，以及访存回调主动污染 host FPSR 时 guest 异常状态仍正确。
对话到教学画面已检查，触摸手势已处理；持续移动的性能尚未验收。临时 JIT 转储
hook 已从构建 overlay 移除；最新验证后仅补充文档，未再改行为代码。
测试进程均已关闭，最后一轮因 MCP 不可用以 SIGTERM 收尾，退出码 1；其他轮次
仍有既有 fault/超时及强制结束。未涉及退出修复、全关卡或 Windows/Linux 验收。
