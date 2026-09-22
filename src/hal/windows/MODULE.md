# 模块：hal/windows

## 职责

实现 Windows 宿主专属 HAL 适配，仅处理 OS 资源与跨平台 HAL 类型之间的转换。

## 依赖与边界

- 可依赖公开 HAL 接口、SDL3 及 Windows SDK。
- 不得被除 HAL 装配点之外的上层模块直接包含。
- 回调上层必须经过显式 HAL 接口，不包含 guest、游戏或 Android 语义。
- `WebViewHost` 封装固定 webview/WebView2、同一本地页面导航限制、禁止新窗口、100ms 宿主
  事件计时器与 PNG 冒烟截图。绑定只转交字符串请求，方法语义由上层提供；不启动 HTTP 服务。
  上游实现单独编译，WebView2 SDK 通过显式准备脚本提供，不在配置阶段下载。
- `HostExecutableDirectory` 与宿主环境覆盖使用 Win32 模块路径及进程环境 API，
  不把 Windows SDK 类型泄漏到公共 HAL。
- 共享库加载使用 `LoadLibraryW`/`GetProcAddress`，命名规则为 `<name>-<major>.dll`。
- 诊断触发使用当前会话 `Local\\` named event；event 与输出文件使用仅当前用户的保护
  DACL，Windows handle 不泄漏到公共 HAL。

## 测试

契约测试放在 `tests/hal/`；需要窗口的测试必须提供 headless 跳过条件。

`HostUserDataDirectory()` 返回本平台的用户数据根（Windows `%APPDATA%\OGPlay`、
macOS `~/Library/Application Support/OGPlay`、Linux `$XDG_DATA_HOME/ogplay` 或
`~/.local/share/ogplay`）；宿主环境未声明时返回空，由调用方明确报错而不是猜测。

DVM-105：FillSecureRandom 分块调用 BCryptGenRandom 的 SYSTEM_PREFERRED_RNG，链接
bcrypt，失败抛出；本 WU 未验证 Windows 实际构建。
