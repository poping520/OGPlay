# 模块：core

## 职责

提供不依赖其他 OGPlay 模块的日志、错误、配置、能力账本和序列化基础设施。

## 公共 API

- `ogplay::core::Logger`：接收结构化 `LogRecord`，支持过滤与内存快照。
- `ogplay::core::CapabilityLedger`：加载账本并记录运行时命中。

## 不变量

- 日志 message 为固定文本，可变信息只能放在 fields。
- 未实现能力的调用计数不会被清零或伪装为 complete。
- core 不知道游戏、guest ABI、前端或平台实现。

## 禁止

- 不得裸写 stdout/stderr。
- 不得反向依赖任何其他 `src/` 模块。

## 测试

`tests/core/`；`ctest -L unit`。

