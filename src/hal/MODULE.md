# 模块：hal

## 职责

定义 window、gfx、audio、input、fs、video、thread、clock 的宿主接口；平台实现只能位于
`hal/windows`、`hal/linux`、`hal/macos`。

## 公共 API

- `hal::Clock`：所有 guest 时间源使用的抽象。
- `hal::FixedStepClock`：无 sleep、精确按帧推进的测试/Agent 后端。
- 其余 HAL 接口在 M1 定义；上层不得包含平台头文件。

## 不变量

- 平台差异不泄漏到 runtime/session。
- 时间由统一 Clock 提供；线程接口必须映射真实宿主线程。

## 禁止

- 不包含游戏规则、Title Profile 或 guest API 语义。
- 不依赖 cpu/memory/runtime/gles/audio/input/session/frontend。

## 测试

后端契约测试放在 `tests/hal/`。
