# WU-PERF-01 · Raw GPU trace ring

目标：把 GPU trace 记录从动态 `GpuTraceEntry`/map/deque 改为固定 2048 项 raw ring。

验收：

- [x] 正常 EGL/GLES 调用只记录 descriptor index 与 r0-r3，不构造字符串/map。
- [x] ring 满后确定性覆盖最旧项，查询仍按时间顺序返回最近匹配项。
- [x] trace 查询阶段才解析函数名、过滤并格式化参数。
- [x] 测试覆盖 2050 次调用后的 2048 项容量与渲染结果。
- [ ] PERF 阶段统一构建与测试。
