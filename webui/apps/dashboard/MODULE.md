# Dashboard Web 前端

- 仅从同源 `/dash/rpc` 读取 schema 1；控制操作不走此只读接口。
- 单次串行轮询快照/事件/选中线程，按 125 ms 目标周期调度，不重叠请求；2.5 s 超时，断线 1 s 重试。
- stream_id 改变时清空旧历史、选择和游标，避免跨运行进程混合事实。
- 快照最多 600、事件最多 4096；selection 使用 guest_tid/context_token/frame/capability 共享键。
- 断线清空当前事实并标注历史；busy/unavailable 不用上次快照或零覆盖。
- 64 位整数大值保持十进制文本；不能无损发出的数字参数明确失败。
- 时间轴仅显示采样时刻与实际 frame，不推测 FPS、逐帧耗时或缺失事件 frame。
- Preact 转义所有来源文本，无 innerHTML；uPlot 只消费有界数字序列。
