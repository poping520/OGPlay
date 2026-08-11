# WU-PERF-05 · 帧循环空转休眠与 Dynarmic code cache 扩容

目标：消除稳定期每帧固定 1ms 休眠税，并停止 Dynarmic 因 16 MiB code cache 打满
导致的整缓存冲刷与逐帧重编译。

背景（macOS Release、Dungeon Hunter 稳定期 5 秒采样，WU-PERF-04 之后）：

- 帧循环每次迭代无条件 `sleep_for(1ms)`，即使刚成功呈现帧，占稳定期约 15%；
- `DynarmicCpu::Run` 内 `GetOrEmit → Emit` 持续出现（约占 guest 执行 14%），
  Dynarmic 在 cache 满时执行整体 `ClearCache()` 后重编译热点块，16 MiB 被真实
  标题在稳定期打满。

验收：

- [x] 帧循环只在未成功呈现 guest 帧的迭代休眠（`ShouldIdleSleepAfterFrameStep`
  纯函数 + 单元测试）；`!advance` 空转分支的休眠保持不变。
- [x] `code_cache_size` 16 MiB → 64 MiB（Dynarmic 上限 128 MiB，映射惰性提交）；
  预热完成后稳定期采样中 `GetOrEmit/Emit` 从约 180/3s 降到 4 行残留。
- [x] full CTest 通过；Dungeon Hunter 600 帧 Release 实测 13.2s → 11.87s（约 -10%），
  1500 帧 15.8s（加载后约 165 FPS）。
