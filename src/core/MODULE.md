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
- `JsonDocument` / `JsonWriter`：基于 yyjson 的唯一 JSON 解析、只读访问、树复制与构造
  边界；调用方不接触第三方类型。
- `CapabilityLedger::RecordNullCall/NullCalls`：所有吞错 quirk 共用的空调用观测表。
- `SoftwareSurface` + `CompareFrames`：M0 无 GPU 黄金帧生产、像素差与感知哈希基础设施。
- `byte_order.h` / `arithmetic.h`：overflow-safe range、little-endian 整数读取/写入与
  power-of-two 对齐原语；格式模块负责在调用前检查并保留自己的 typed error。
- `text.h`：canonical UTF-8 校验与 UTF-16 code-unit 计数、受检 code point 编码、
  ASCII whitespace trim，以及 reject/replace 策略化 UTF-16→UTF-8；
  `IsValidPackageName` / `IsValidLowercaseIdentifier` 提供点分包名与小写标识符的
  共享词法校验，供 session、input 与前端复用。
- `encoding.h`：标准/URL-safe Base64（padding/wrap/newline 可配置）与大小写可选 hex；
  Android flags 仍由 runtime adapter 解释。

## 不变量

- text 日志中的单行 field 保持 `key=value`；多行 string field 渲染为 `key=` 后的缩进块，
  并保留值自身的相对缩进。JSONL 仍保存原始字符串，不把展示空格写回结构化值。

- 日志 message 为固定文本，可变信息只能放在 fields。
- 环形缓冲常开；Trace/Debug/Info 默认首次记录并累计重复次数。
- 未实现能力的调用计数不会被清零或伪装为 complete。
- CI 将当前能力状态与基线逐项比较；条目不得删除，状态不得后退。
- core 不知道游戏、guest ABI、前端或平台实现。
- GPU provider 的 trace 必须由调用方给出上限；结构中不得夹带预序列化 JSON。
- JSON 输入默认不超过 1 MiB/64 层，必须是合法 UTF-8 且无重复对象键；输出对象也拒绝
  重复键。所有失败均显式返回或抛出，不做宽松修复。

## 禁止

- 不得裸写 stdout/stderr。
- 不得反向依赖任何其他 `src/` 模块。

## 测试

`tests/core/`；`ctest -L unit`。
