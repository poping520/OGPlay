# GUI-11 · macOS WebView 启动器与 .app 交付

状态：macOS 完成：WU-0..5 已实现，自动化、包与真实点击流程验收通过。决策：[ADR-0139](../../adr/development.md#adr-0139)。

## 目标与依赖

macOS 交付可双击启动的 `OGPlay.app`（ad-hoc 签名），复用 GUI-10 的 core 外观、桌面应用库、
Web UI 与 SDL 子进程管理；新增 macOS `WebViewHost`、构建开关与包布局。
依赖顺序：WU-0 预研 → WU-1 构建与 .app/ANGLE `@rpath` → WU-2 macOS 宿主 → WU-3 流程差异 →
WU-4 测试与真实验收 → WU-5 契约。不做 Linux GUI、Developer ID 签名/公证、拆包格式与 Dock 合并。

## WU-0 预研

最小宿主：webview 0.12 Cocoa 后端（单独翻译单元，`WEBVIEW_STATIC`）+ ObjC++ 探针，加载仓库
`data/webui/gui`，RPC 返回固定数据；`webview_init` 注入 CSP 违规、脚本错误、DOM、拖放探针。
分别用 `webview_navigate(file URL)` 与 `loadFileURL:allowingReadAccessToURL:` 各跑一次。

| 风险 | 结论 |
| --- | --- |
| 本地页面与 CSP `'self'` | 两种加载均执行 `gui.js` 与样式，无 CSP 违规/脚本错误，`library.list`、`settings.get`、`dashboard.list` 均到达宿主；截图为完整渲染界面。不需要自定义 scheme。 |
| RPC 回调内建子窗口 | 在 `dashboard.list` 回调内同步 `webview_create` 成功；子窗口加载 loopback 页面，sessionStorage 令牌注入可读。关闭子窗口后主窗口与 100ms 计时器继续运行。 |
| 导航与弹窗限制 | 库不设置 navigationDelegate，宿主可经原生句柄接管；子窗口跳转外部被 CANCEL，`window.open` 返回空（无新窗口）。主窗口跳转 `file:///etc/hosts` 在 navigate 模式被委托取消；loadFileURL 模式在委托之前即被 WebKit 读权限拦截，页面保持不变。 |
| Finder 拖放 APK | 用户从 Finder 拖入原版样本：页面收到 `dragenter`（types=Files）与 `drop`，`dataTransfer.files` 为 `com.rovio.angrybirds_2.3.0.apk:25256888`，大小与原文件一致；放下后主窗口无任何导航。分块上传可沿用现有前端。 |

决定：主窗口用 `loadFileURL:allowingReadAccessToURL:`（读权限限定 GUI 目录）叠加精确 URL 导航委托；
子窗口同样设置精确 URL 委托，不绑定 rpc。webview 库不建主菜单，WU-2 须补 App/Edit/Window 菜单。

证据：`.local/macos-gui/`（`host.mm`、`backend.cpp`、`run-{navigate,loadfile,drop}.log`、`snap-*.png`、`www/dash`）。

## WU-1 构建与 OGPlay.app

- `OGPLAY_BUILD_GUI` 在 Windows/macOS 默认开启；macOS 编译 webview Cocoa 后端与 `webview_host.mm`。
  已有构建目录若缓存 OFF，需 `-DOGPLAY_BUILD_GUI=ON` 重新配置。
- ANGLE：配置期把 SDK 中安装名为 `./libEGL.dylib` 的两个库复制到 `build/<preset>/angle-runtime`，
  改为 `@rpath/...` 并 ad-hoc 重签；ogplaycore 只带 `@loader_path`。CLI 与包均可在任意工作目录运行。
- `ogplay_bundle`：每次构建把 ogplaycore、SDL3、ANGLE、ogplay_ffmpeg 复制到 `Contents/Frameworks`，
  payload 复制到 `Contents/Resources/data`，然后 `codesign --sign -` 并 `--verify --strict --deep`。
  `Info.plist` 由 `cmake/macos/Info.plist.in` 生成（标识 `org.ogplay.OGPlay`，最低 macOS 12）。
- 安装：`cmake --install --component Runtime` 复制已签名的包，签名校验通过，`open` 启动正常。

## WU-2 macOS 宿主

`src/hal/macos/webview_host.mm` 实现完整 `WebViewHost`：主窗口加载与导航限制、rpc 绑定、100ms 轮询、
Dashboard/按键设置窗口、`NSOpenPanel` 选择器、最小化、`NSWorkspace` 打开目录、应用/编辑/窗口菜单
与快照冒烟。CTest 后台启动时窗口被遮挡导致 WebKit 暂停 `requestAnimationFrame`，冒烟超时；
现启动时主动前置窗口，前后台启动均通过。

## WU-3/4 流程与测试

- `frontend.gui_dashboard_window` 与进程跟踪用例移植到 macOS（`/bin/sh` 子进程、`NSWindow` 关闭辅助），
  新增 stderr 写入日志检查；集成测试 13 例、GUI 相关 CTest 10 项通过。
- 包内可执行文件以启动器参数运行 `--run-game`：payload 取自 `Contents/Resources/data`，原版样本
  推进 120 帧退出 0，持久沙盒写入库根；CLI 在 `/` 下 `run-apk` 120 帧退出 0。
- `ogplay_tests` 中 4 个 EGL/GLES 用例失败；用原始未改写的 ANGLE 库对照仍是同样 4 例，与本任务无关。

证据：`.local/macos-gui/`（构建/CTest/集成/安装日志、`replay-cli-root.*`、`bundle-game.*`、`flow-library`）。

## 真实点击验收

用户在 `open -n OGPlay.app --args --library-root .local/macos-gui/manual-library` 打开的隔离库中操作原版样本，
宿主侧以库目录/存档时间戳、进程表与 CLI 核对：

- 文件选择器导入（23:14:29）；设置页启用 MCP 并保存（`config.toml` 23:14:49）。
- 启动、打开 Dashboard 与按键设置窗口、游戏内 F2 打开按键设置，关闭游戏后启动器回收为 exit 0。
- 拖入同一 APK 并确认新实例（23:25:05 分配 `com.rovio.angrybirds-2`），随后在启动器移除（23:25:48）；
  库中只剩原实例，两个实例的 sandbox 均保留。
- 再次启动后 Cmd+Q 退出启动器：游戏进程继续运行（父进程变为 launchd）；此时 CLI `library remove`
  与重复 `library run` 均以 `instance_running` 退出 1，条目保留。

观察到的既有缺口（未在本任务修复）：core Logger 对 warn 以下的同类消息默认只输出首条，
同一启动器会话内第二次“game process started”不会写入 `gui.log`；该行为与平台无关。
