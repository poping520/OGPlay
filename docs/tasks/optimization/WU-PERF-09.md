# WU-PERF-09 · 原生执行与帧搬运热点

目标：降低原生 guest 的受检访存与线程竞争开销，用同存档、同场景的 Release
成功呈现帧数对照检验收益；用户性能目标为 30 FPS 以上，尽量达到 60 FPS。

依赖：既有 AddressSpace 映射发布、MemoryBus、Dynarmic ARM64 及真实 exclusive CAS。
设计：[ADR-0096](../../adr/runtime.md#adr-0096)。

范围：独立只读数据页表及 exclusive 回调的真实 CAS，保持写入、取指、observer、跨页
和原子操作的权限边界；限定外部 observer 的宿主线程、保留 JNI 重入的 runner 上下文；
避免高频 buffer bind 复制 uniform 元数据；原地 UI 合成与已认证透明叠层的扫描省略。
不通过游戏专属分支、降低画质或修改时间源提速。

验收：读页表发布/撤销/快照恢复；JIT 读取 R/RX 页、拒绝写保护及 execute-only
数据读取、跨页与卸载 fault；既有 exclusive、映射失效回归；实际游戏局部 FPS 对照。

状态：已完成本次实现及局部性能验证，达到 30 FPS 目标；60 FPS 未达到。

结果：macOS Release、2560×1600、同存档第一段对话，原版默认 1 核 10.60 FPS、
原版 2 核 14.39 FPS；优化后默认 1 核 23.33 FPS，2 核 33.06 FPS（同 MCP/diag）。
最终 2 核不传 `--diag` 的对话测量为 35.91 FPS。
截图对应的教学场景为 35.52 FPS。推荐原启动命令追加 `--cpu-cores 2`；4/8 核
探索未优于 2 核，游戏任务队列自旋会加重竞争。以上为各 15 秒成功呈现帧计数，
35.91/35.52 两项测量启用 MCP、未传 `--diag`，其余对话对照启用 MCP/diag；MCP 本身
仍启用内部诊断。不是无诊断 FPS 承诺或完整关卡/title gate 验收。

验证：仅构建 `ogplay`、`ogplay_tests`；42 项相关定向回归、1253 断言通过，覆盖
页表权限/撤销、exclusive CAS、observer 线程作用域、JNI 错误和 UI/GLES 行为。
额外两项 EGL 检查失败，回退本次 GL 状态复制优化后仍以相同断言失败，未在本任务扩修。

剩余：原生 CPU 蒙皮/任务队列竞争及帧读回仍占时；直接访存卸载 quiescence、普通写入
ABA 与 DVM-218 退出读 0x315f0001 仍未闭合；最后一次补测在 surfaceDestroyed 后
退出超时，15 秒后强制关闭，原因未定位。Windows/Linux 未实跑。未保留实验性的
exclusive monitor 改写及 JIT/guest 值采样代码。

基线、采样、截图、测试与构建证据位于 `.local/tales-perf-20261005/`，
完整结果见其中 `optimization-report.md`。
