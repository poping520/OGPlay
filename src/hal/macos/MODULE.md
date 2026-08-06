# 模块：hal/macos

## 职责

实现 macOS 宿主专属 HAL 适配，仅处理 OS 资源与跨平台 HAL 类型之间的转换。

## 依赖与边界

- 可依赖公开 HAL 接口、SDL3 及 Apple 宿主框架。
- 不得被除 HAL 装配点之外的上层模块直接包含。
- 回调上层必须经过显式 HAL 接口，不包含 guest、游戏或 Android 语义。
- `HostExecutableDirectory` 通过 dyld 查询并规范化；宿主环境覆盖使用 POSIX
  `setenv`/`unsetenv` 并遵守公共 HAL 的作用域恢复契约。

## 测试

契约测试放在 `tests/hal/`；需要窗口服务器的测试必须提供 headless 跳过条件。
