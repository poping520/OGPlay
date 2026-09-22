# 子模块：frontend/gui

## 职责

Windows 系统 WebView2 启动器与独立游戏库模型。GUI 只管理宿主游戏库、装配同目录 CLI
子进程，不拥有 guest/runtime。GUI v2 第一阶段已替换 ImGui；Linux 暂缓，macOS 宿主待接入。

## 公共 API

- `RunGuiCommand` / `RunGuiStandalone`：CLI 与双击入口；失败记录日志，独立入口显示消息框。
  原生窗口、导航限制和目录打开经 `hal::WebViewHost`，不直接包含 Windows/WebView2 API。
- `GuiRpcService::Handle`：复用 `agent::JsonRpcAdapter` 注入模式；同步分派
  `library.list`、`library.launch`、`library.open_dir`，以及导入任务与对话框 RPC。
  宿主上下文、进程、目录打开、分析、选择器与 UTC 时间通过显式回调注入。
- `library.analyze {path}` / `library.upload.begin/chunk/finish`：原生路径或拖放字节进入独立快照，
  单体 APK 上限 1 GiB；分块 base64 最大 256 KiB，严格顺序/总长度检查。
- `library.job.poll/cancel {job}`：每服务仅一个活动导入任务；后台只读分析，失败可查询。
  `library.import {job, new_instance:true, external_dir?}` 只消费 ready 快照，异步原子发布；
  重复提交拒绝，结果返回真实 installation id。取消不发布；提交期间不能取消。
- `dialog.pick {kind:file|directory}` / `dialog.poll {dialog}`：异步原生选择器，取消返回 null。
- `LibraryStore`：枚举、原子导入并返回实际 installation id、按 id 删除；损坏条目携带原因，清理 `.importing` 残留。
- `LoadGuiConfig` / `SaveGuiConfig`：严格 schema 2 TOML，兼容读取 schema 1；配置发布保留 `.bak` 崩溃恢复。
- `GuiSettings`：全局字段的默认值、类型、范围、枚举与预留标记唯一来源。
  `settings.get/set` 返回完整有效值；保存校验读取版本、目录和字段，未知键或损坏配置拒绝覆盖。
  `settings.open_dir` 只接受 library 枚举，不接受任意路径。制品信息缺失不影响配置保存。
  后台 APK 分析或入库尚未回收时禁止保存，避免配置发布与后台读取并发。
- `GameSettingDefinitions` / `LoadGameSettings` / `SaveGameSettings`：每实例 `settings.toml`
  严格 schema 1，与全局配置共用私有 TOML 解析与 `.bak` 发布/恢复。只写显式覆盖键；
  缺省继承全局或字段默认值，`external_dir` 缺省继承导入记录，空字符串明确取消数据包目录。
- `game_settings.get/set {installation_id, values?, revision?}`：只查找已枚举实例，set 的 values
  是完整覆盖集合，删除键恢复继承；读取版本绑定实例、覆盖集合及继承值。未知键、无效目录、
  损坏配置及过期版本明确失败。运行中可保存下次启动设置，不修改运行中的进程。
  get 返回保存值、继承值、字段定义和三种启动模式的 argv/错误，预览不启动子进程。
- `ExtractApkApplicationVisuals` / `ResizeArgbBilinear`：APK 名称、128×128 PNG 与明确资源回退原因。
- `BuildLibraryTiles` / `BuildLibraryDetail` / `LibrarySelection`：统一状态、详情和稳定选择模型。
- `AnalyzeApkImport` / `BuildLibraryImport`：只读 APK 分析、Profile 匹配和原子入库请求。
- `LauncherSandboxRoot` / `BuildLaunchPlan`：唯一 run-apk argv 与 spawn 前宿主输入验证。
- `GuiProcessManager`：SDL3 子进程启动、单实例/端口约束、非阻塞回收；析构只解除跟踪，不杀游戏。
- `dashboard.list {}` / `dashboard.open {installation_id}`：仅查询/打开当前启动器跟踪的子进程，
  不接受前端 URL/端口。每实例记录 PID、MCP 端口和 disabled/starting/ready/unavailable。
  后台探测只读空 section 快照（750ms/8KiB 上限，完成后间隔 1s），核对 schema 与宿主 PID；
  只有 ready 能打开。固定端口冲突在 spawn 前失败；未启用 MCP 或预检没有 Dashboard。
  dashboard_auto_open 只在首次就绪执行一次；子进程退出后移除入口并关闭对应窗口。
  同实例复用窗口，关闭后可重开；启动器退出关闭监控窗口，保留游戏进程。
- `ValidateGuiConfigDirectories`：配置目录验证；全局设置保存前执行，删除 UI 待后续接回。
- 原 CJK 字体选择、事件等待与消息队列模型保留用于既有调用/测试，不再驱动 WebView 渲染。

## 不变量

- 请求 envelope 和方法参数采用 closed schema；未知字段、方法、类型与重复 JSON 键明确失败。
  前端不传自由 argv、任意目录打开路径或游戏兼容结论；错误返回 code/message/next_step。
- Web UI 只渲染模型事实。状态优先级：损坏 > Profile catalog 不可用 > 缺 Profile >
  缺数据包 > 运行中 > ready。缺 Profile 为可启动通用 APK 提示，不是兼容性等级。
- Profile/required-external 事实来自 session 摘要；catalog 失效必须显示 unavailable。
  不得把空 required-external 集合当成 ready；默认 Profile 与 quirk 来自同一 bundled payload。
- 启动器主窗口只加载 bundled `webui/gui/index.html`；WebView2 仅允许该入口导航，禁止新窗口。
  静态 CSP 禁止网络、框架、对象、表单和 base 重定向；只绑定 `rpc(string)`，不启用 HTTP 服务。
  Dashboard 独立窗口只允许宿主选择的 loopback `/dash/`，无启动器 RPC，禁止弹窗。
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
- 全局设置仅将超采样、解释器和 MCP 端口转为已支持的 CLI 参数；预留选项只保存，
  不改变运行时行为。启动成功后通过宿主回调最小化；GUI 配置不覆盖正在运行的进程。
- BuildLaunchPlan 从实例配置合并全局值，支持 Profile/数据目录、超采样、解释器、MCP
  开关/端口/手动步进、临时沙盒。预检移除全部 MCP 参数；临时沙盒不传 `--sandbox-dir`。
  `library.launch` 的 mode 仅允许 normal/preflight/diagnostic，预览和启动使用同一路径。
  库视图和目录打开消费实例数据目录，Profile 覆盖目录的错误按实例隔离。
  本阶段不直接修改沙盒 meta、身份或存档；身份再生成/重置/导入导出入口禁用。
- APK/manifest 损坏失败；资源图标/名称失败记录 fallback，空 PNG 为明确占位；versionCode
  接受完整 uint32。禁止把含控制字符的 label 直接持久化。
- 导入未知 Profile 或跳过 required external 可以入库；无效目录、损坏 APK 和未解决的实例
  占位冲突必须阻止发布。重复 package 允许多个安装实例，不覆盖旧条目。
- 删除只移除 `library/<installation-id>`；external 与持久存档不删除，运行中不删除。

- 入库期间库枚举/启动/目录 RPC 返回忙，避免枚举清理仍在写入的 `.importing`；
  作业轮询始终可用。分析与入库线程在服务销毁时 join，再移除本服务持有的快照。
- 导入摘要的版本/API/ABI/Profile 来自解析；无 Profile 时 required external 为未知。
  前端同包重复导入须确认新实例，需要数据时允许明确跳过；从不覆盖已有游戏。
- 原生选择器与后台操作期间向导保留并禁用重复操作；选择器取消不丢弃当前摘要。
  当前仅支持单体 APK，不实现 XAPK/APKM/APKS 拆包，也不从目录猜选 APK。

## 禁止

不实现 guest/session、syscall/JNI/GLES 或游戏专用兼容；不解析自由文本日志判断兼容性；
不使用前端猜测替代宿主状态。

## 验证

保留 `tests/frontend/gui_{model,visuals,view_model,import,launch}_tests.cpp`；
`gui_rpc_tests.cpp` 覆盖协议闭合、事实序列化、启动约束和目录映射。
`npm run check` 验证前端类型、协议、库筛选和分块上传边界；`frontend.gui_webui_manifest` 校验制品哈希；
`frontend.gui_smoke` / `frontend.gui_library_smoke` 验证真实 WebView 加载与空库/CJK 非空库 RPC。
