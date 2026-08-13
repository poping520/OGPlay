# 模块：hal/macos

## 职责

实现 macOS 宿主专属 HAL 适配，仅处理 OS 资源与跨平台 HAL 类型之间的转换。

## 依赖与边界

- 可依赖公开 HAL 接口、SDL3 及 Apple 宿主框架。
- 不得被除 HAL 装配点之外的上层模块直接包含。
- 回调上层必须经过显式 HAL 接口，不包含 guest、游戏或 Android 语义。
- `HostExecutableDirectory` 通过 dyld 查询并规范化；宿主环境覆盖使用 POSIX
  `setenv`/`unsetenv` 并遵守公共 HAL 的作用域恢复契约。
- 共享库加载使用 `dlopen`/`dlsym`(RTLD_NOW|RTLD_LOCAL),命名规则为
  `lib<name>.<major>.dylib`。

## 测试

契约测试放在 `tests/hal/`；需要窗口服务器的测试必须提供 headless 跳过条件。

`HostUserDataDirectory()` 返回本平台的用户数据根（Windows `%APPDATA%\OGPlay`、
macOS `~/Library/Application Support/OGPlay`、Linux `$XDG_DATA_HOME/ogplay` 或
`~/.local/share/ogplay`）；宿主环境未声明时返回空，由调用方明确报错而不是猜测。
