# 子模块：frontend/gui

## 职责

Windows 系统 WebView2 启动器与独立游戏库模型。GUI 只管理宿主游戏库、装配同目录 CLI
子进程，不拥有 guest/runtime。GUI v2 第一阶段已替换 ImGui；Linux 暂缓，macOS 宿主待接入。

## 公共 API

- `RunGuiCommand` / `RunGuiStandalone`：CLI 与双击入口；失败记录日志，独立入口显示消息框。
  原生窗口、导航限制和目录打开经 `hal::WebViewHost`，不直接包含 Windows/WebView2 API。
- `GuiRpcService::Handle`：复用 `agent::JsonRpcAdapter` 注入模式；同步分派
  `library.list`、`library.launch`、`library.open_dir`。宿主上下文、进程和打开目录通过显式回调注入。
- `LibraryStore`：枚举、原子导入、按 installation id 删除；损坏条目携带原因，清理 `.importing` 残留。
- `LoadGuiConfig` / `SaveGuiConfig`：严格 schema 1 TOML；配置发布保留 `.bak` 崩溃恢复。
- `ExtractApkApplicationVisuals` / `ResizeArgbBilinear`：APK 名称、128×128 PNG 与明确资源回退原因。
- `BuildLibraryTiles` / `BuildLibraryDetail` / `LibrarySelection`：统一状态、详情和稳定选择模型。
- `AnalyzeApkImport` / `BuildLibraryImport`：只读 APK 分析、Profile 匹配和原子入库请求。
- `LauncherSandboxRoot` / `BuildLaunchPlan`：唯一 run-apk argv 与 spawn 前宿主输入验证。
- `GuiProcessManager`：SDL3 子进程启动、单实例约束、非阻塞回收；析构只解除跟踪，不杀游戏。
- `ValidateGuiConfigDirectories`：配置目录验证；设置、导入和删除 UI 在后续 GUI v2 阶段接回。
- 原 CJK 字体选择、事件等待与消息队列模型保留用于既有调用/测试，不再驱动 WebView 渲染。

## 不变量

- 请求 envelope 和方法参数采用 closed schema；未知字段、方法、类型与重复 JSON 键明确失败。
  前端不传自由 argv、任意目录打开路径或游戏兼容结论；错误返回 code/message/next_step。
- Web UI 只渲染模型事实。状态优先级：损坏 > Profile catalog 不可用 > 缺 Profile >
  缺数据包 > 运行中 > ready。缺 Profile 为可启动通用 APK 提示，不是兼容性等级。
- Profile/required-external 事实来自 session 摘要；catalog 失效必须显示 unavailable。
  不得把空 required-external 集合当成 ready；默认 Profile 与 quirk 来自同一 bundled payload。
- GUI 只加载 bundled `webui/gui/index.html`；WebView2 仅允许该入口导航，禁止新窗口。
  静态 CSP 禁止网络、框架、对象、表单和 base 重定向；只绑定 `rpc(string)`，不启用 HTTP 服务。
- Node 仅用于构建；默认 CMake 不联网，SDK 需显式准备。GUI 构建校验 manifest 文件 SHA-256，
  缺失/不匹配明确失败；运行时找不到 Web UI 明确失败，不退回旧界面。
- 模型/RPC 层不 include SDL/WebView，不直接调用窗口或进程 API。
- `library.open_dir` 只由已枚举 installation id 映射 sandbox/log/external；目录不存在明确失败，
  不创建沙盒。log 为保存 last-run.log 的条目目录。
- GUI 日志覆盖写入 `<library-root>/gui.log`，CLI 同时保留 stderr sink。
- 兼容 `--smoke-frames N` 正整数参数：现表示 WebView 加载后 N 次成功 `library.list`，
  不再声称 ANGLE present 次数；未完成前关闭窗口视为失败。
- 100ms 宿主事件计时器回收进程，不忙轮询。退出 0 静默，非零结构化退出码及有界 UTF-8
  日志尾部经前端队列呈现；不据此推断兼容性。
- 子进程只解析同目录 CLI，stdin 关闭、stdout 继承、stderr 覆盖 last-run.log；显式传递
  `--sandbox-dir <library-root>/sandbox` 与 `--installation-id`，同实例不重复启动。
- APK/manifest 损坏失败；资源图标/名称失败记录 fallback，空 PNG 为明确占位；versionCode
  接受完整 uint32。禁止把含控制字符的 label 直接持久化。
- 导入未知 Profile 或跳过 required external 可以入库；无效目录、损坏 APK 和未解决的实例
  占位冲突必须阻止发布。重复 package 允许多个安装实例，不覆盖旧条目。
- 删除只移除 `library/<installation-id>`；external 与持久存档不删除，运行中不删除。

## 禁止

不实现 guest/session、syscall/JNI/GLES 或游戏专用兼容；不解析自由文本日志判断兼容性；
不使用前端猜测替代宿主状态。

## 验证

保留 `tests/frontend/gui_{model,visuals,view_model,import,launch}_tests.cpp`；
`gui_rpc_tests.cpp` 覆盖协议闭合、事实序列化、启动约束和目录映射。
`npm run check` 验证前端类型与协议边界；`frontend.gui_webui_manifest` 校验制品哈希；
`frontend.gui_smoke` / `frontend.gui_library_smoke` 验证真实 WebView 加载与空库/CJK 非空库 RPC。
