# 模块：hal

## 职责

定义 window、gfx、audio、input、fs、video、thread、clock 的宿主接口；平台实现只能位于
`hal/windows`、`hal/linux`、`hal/macos`。

## 公共 API

- `hal::Clock`：所有 guest 时间源使用的抽象。
- `hal::ClockRate`：正整数分子/分母表示的精确倍率。
- `hal::FixedStepClock`：无 sleep、按帧推进且保留倍率余数的确定性后端。
- `hal::RealtimeClock`：基于单调宿主时间的实时后端，支持暂停与倍速。
- 其余 HAL 接口在 M1 定义；上层不得包含平台头文件。

## 不变量

- 平台差异不泄漏到 runtime/session。
- 时间由统一 Clock 提供；线程接口必须映射真实宿主线程。
- 暂停期间 ticks 不增长；不支持的推进方式必须明确失败。

## 禁止

- 不包含游戏规则、Title Profile 或 guest API 语义。
- 不依赖 cpu/memory/runtime/gles/audio/input/session/frontend。

## 测试

后端契约测试放在 `tests/hal/`。
