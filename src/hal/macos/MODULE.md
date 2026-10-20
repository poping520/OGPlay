# 模块：hal/macos

## 职责

实现 macOS 宿主专属 HAL 适配，仅处理 OS 资源与跨平台 HAL 类型之间的转换。

## 依赖与边界

- 文件克隆用 clonefile(CLONE_NOFOLLOW)，ENOTSUP/EXDEV 视为不支持。
- 宿主只读文件使用 O_CLOEXEC/O_NOFOLLOW fd 与 pread；身份来自 fstat dev/inode，
  EINTR 重试、取消及错误传播，不使用共享文件偏移。

- 可依赖公开 HAL 接口、SDL3 及 Apple 宿主框架。
- 不得被除 HAL 装配点之外的上层模块直接包含。
- 回调上层必须经过显式 HAL 接口，不包含 guest、游戏或 Android 语义。
- `HostExecutableDirectory` 通过 dyld 查询并规范化；宿主环境覆盖使用 POSIX
  `setenv`/`unsetenv` 并遵守公共 HAL 的作用域恢复契约。
- 共享库加载使用 `dlopen`/`dlsym`(RTLD_NOW|RTLD_LOCAL),命名规则为
  `lib<name>.<major>.dylib`。
- 浮点 `FromChars` 使用 `strtof_l`/`strtod_l` 与 C locale，对齐 `std::from_chars`
  的 general/hex 契约。
- 诊断触发使用 `SIGUSR1` 写 nonblocking self-pipe；signal handler 只执行 async-signal-safe
  `write`，输出文件权限替换为 owner read/write。

- RGB 客户端表面存储使用有界尺寸的 BGRA IOSurface，创建/释放只经公共 HAL；
  BGRX/alpha 与绘制语义由 ANGLE 的 GL_RGB 导入处理。

- `WebViewHost`（`webview_host.mm`）与 webview Cocoa 后端编入仅桌面链接的 ogplay_desktop_webview，
  不属于 ogplay_hal。主窗口以限于 GUI 目录的读权限加载本地页面，精确地址导航委托取消其他导航，
  不打开新窗口；100ms 计时器在公共运行循环模式下轮询，主窗口关闭时回收全部运行窗口并结束循环。
  Dashboard/按键设置窗口仅允许指定 loopback 地址、不绑定 rpc，令牌只经 sessionStorage 注入。
  文件/目录选择使用依附主窗口的 `NSOpenPanel`，异步完成 future；`OpenHostDirectory` 经 NSWorkspace。
  宿主建立应用/编辑/窗口菜单，退出菜单正常结束运行循环；冒烟截图来自 WKWebView 快照。

## 测试

契约测试放在 `tests/hal/`；需要窗口服务器的测试必须提供 headless 跳过条件。

`HostUserDataDirectory()` 返回本平台的用户数据根（Windows `%APPDATA%\OGPlay`、
macOS `~/Library/Application Support/OGPlay`、Linux `$XDG_DATA_HOME/ogplay` 或
`~/.local/share/ogplay`）；宿主环境未声明时返回空，由调用方明确报错而不是猜测。

DVM-105：FillSecureRandom 使用系统 arc4random_buf 产生 CSPRNG 字节。

Packed storage 拥有 native Metal B5G6R5 或 Depth32Float_Stencil8 纹理；dimensions/device 受检，
仅允许 upper gles 的 host device。无 guest 地址解引用或 GLES/ANGLE 逻辑。

`HostExecutablePath` 返回宿主完整可执行路径，`HostExecutableDirectory` 由其父目录派生；共享库不作为宿主路径锚点。
