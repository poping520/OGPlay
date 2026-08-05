# 模块：core

## 职责

提供不依赖其他 OGPlay 模块的日志、错误、配置、能力账本和序列化基础设施。

## 公共 API

- `ogplay::core::Logger`：结构化记录、环形缓冲、限流、sink、符号化与诊断包。
- `ConsoleSink` / `FileSink`：文本或 JSONL 输出；Agent 通过 Logger 快照读取同源记录。
- `GuestSymbolProvider`：由 Loader 提供的地址符号化抽象，core 不依赖 ELF 实现。
- `GpuStateProvider`：图形实现注入统计、渲染目标、能力与有界 trace 的强类型只读快照；
  core 不依赖 GLES 或 Agent。
- `ogplay::core::CapabilityLedger`：加载账本并记录运行时命中。
- `CapabilityLedger::RecordNullCall/NullCalls`：所有吞错 quirk 共用的空调用观测表。
- `SoftwareSurface` + `CompareFrames`：M0 无 GPU 黄金帧生产、像素差与感知哈希基础设施。

## 不变量

- 日志 message 为固定文本，可变信息只能放在 fields。
- 环形缓冲常开；Trace/Debug/Info 默认首次记录并累计重复次数。
- 未实现能力的调用计数不会被清零或伪装为 complete。
- CI 将当前能力状态与基线逐项比较；条目不得删除，状态不得后退。
- core 不知道游戏、guest ABI、前端或平台实现。
- GPU provider 的 trace 必须由调用方给出上限；结构中不得夹带预序列化 JSON。

## 禁止

- 不得裸写 stdout/stderr。
- 不得反向依赖任何其他 `src/` 模块。

## 测试

`tests/core/`；`ctest -L unit`。
