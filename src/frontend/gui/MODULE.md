# 子模块：frontend/gui

## 职责

桌面系统 WebView 启动器（Windows WebView2、macOS WKWebView）的呈现、RPC、导入向导与子进程管理，编入 ogplay_desktop_app。
游戏库领域经 ogplay::app 外观共享，GUI 装配当前 EXE 的
`--run-game` 子进程，不拥有 guest/runtime。GUI v2 第一阶段已替换 ImGui；macOS 宿主见 GUI-11，Linux 暂缓。

## 公共 API

- `RunGuiCommand`：桌面默认模式，直接接受 `--library-root`、`--smoke-frames`；参数错误与
  冒烟失败只返回日志/退出码，交互 GUI 初始化失败显示消息框。入口由 desktop/main 分派；GUI 日志由桌面配置。
  原生窗口、导航限制和目录打开经 `hal::WebViewHost`，不直接包含 Windows/WebView2 API。
- `GuiRpcService::Handle`：复用 `app::JsonRpcDispatcher` 注入模式；同步分派
  `library.list`、`library.launch`、`library.open_dir`、`library.remove`，以及导入任务与对话框 RPC。
  宿主上下文、进程、目录打开、分析、选择器与 UTC 时间通过显式回调注入。
- `library.analyze {path}` / `library.upload.begin/chunk/finish`：原生路径或拖放字节进入独立快照，
  单体 APK 上限 1 GiB；分块 base64 最大 256 KiB，严格顺序/总长度检查。
- `library.job.poll/cancel {job}`：每服务仅一个活动导入任务；后台只读分析，失败可查询。
  `library.import {job, new_instance:true, external_dir?, external_guest_dir?, external_mode?:copy|link}`
  只消费 ready 快照，异步原子发布，默认复制数据包入库；重复提交拒绝，结果返回真实 installation id。
  提交期间 poll 返回 `progress {copied_bytes,total_bytes}`；取消在发布前生效，已发布则结果仍为
  imported，worker 停止前状态保持 importing。服务析构先请求取消再回收。
- `dialog.pick {kind:file|directory}` / `dialog.poll {dialog}`：异步原生选择器，取消返回 null。
- LibraryStore、LibraryConfig、SettingDefinitions、GameSettings、APK 检视/图标/Profile、设置继承归
  frontend/library，只经 app 公开头消费。settings.get/set 保存前复用库内 schema/目录校验，
  未知键/损坏配置/过期读取版本拒绝覆盖；后台分析或入库尚未回收时禁止保存。
- `game_settings.get/set {installation_id, values?, revision?}`：只查找已枚举实例，set 的 values
  是完整覆盖集合，删除键恢复继承；读取版本绑定实例、覆盖集合及继承值。未知键、无效目录、
  损坏配置及过期版本明确失败。运行中可保存下次启动设置，不修改运行中的进程。
  get 返回保存值、继承值、字段定义和三种启动模式的 argv/错误，预览不启动子进程。
- `BuildLibraryTiles` / `BuildLibraryDetail` / `LibrarySelection`：统一状态、详情和稳定选择模型。
- `BuildLaunchPlan` 只调用 ResolveLaunch，再生成令牌/编辑器端口，并用 FormatGameRunArgs 生成 argv，禁止手工拼运行参数。
- `GuiProcessManager`：SDL3 子进程启动、单实例/端口约束、非阻塞回收；析构只解除跟踪，不杀游戏。
- `dashboard.list {}` / `dashboard.open {installation_id}`：仅查询/打开当前启动器跟踪的子进程，
  不接受前端 URL/端口。每实例记录 PID、MCP 端口和 disabled/starting/ready/unavailable。
  后台探测只读空 section 快照（750ms/8KiB 上限，完成后间隔 1s），核对 schema 与宿主 PID；
  只有 ready 能打开。固定端口冲突在 spawn 前失败；未启用 MCP 或预检没有 Dashboard。
  dashboard_auto_open 只在首次就绪执行一次；子进程退出后移除入口并关闭对应窗口。
  同实例复用窗口，关闭后可重开；启动器退出关闭监控窗口，保留游戏进程。
- 按键设置（ADR-0134）：非预检启动都生成 128 位令牌，只经子进程环境 `OGPLAY_CONTROL_TOKEN`
  传入；启用 MCP 时编辑器共用 MCP 端口，否则 `PickLoopbackPort` 选空闲端口并传
  `--key-editor-port`。`GuiProcessManager` 每 500ms 以令牌发 `control.status {client:"launcher"}`，
  据此标记就绪，并在 `edit_requests` 增加（游戏内 F2）时打开窗口。`controls.open {installation_id}`
  只对就绪实例打开；`dashboard.list` 带 `key_editor_status/detail`，从不返回令牌。
- `ValidateLibraryConfigDirectories`：共享配置目录验证；全局设置保存前调用。
- 原 CJK 字体选择、事件等待与消息队列模型保留用于既有调用/测试，不再驱动 WebView 渲染。

## 不变量

- 请求 envelope 和方法参数采用 closed schema；未知字段、方法、类型与重复 JSON 键明确失败。
  前端不传自由 argv、任意目录打开路径或游戏兼容结论；错误返回 code/message/next_step。
- Web UI 只渲染模型事实。状态优先级：损坏 > Profile catalog 不可用 > 缺数据包 > 运行中 >
  ready。通用启动路径是默认状态，无 Profile 不产生状态、提示或条件。
- Profile 条件只在记录了精确 Profile（身份与 quirk）或 catalog 失效（unavailable）时下发，
  否则为 null。
  外部数据只检查用户已选择的目录是否有效，不从 Profile 推断 required；默认 Profile
  与 quirk 来自同一 bundled payload。
- 启动器主窗口只加载 bundled `webui/gui/index.html`；WebView2 仅允许该入口导航，禁止新窗口。
  静态 CSP 禁止网络、框架、对象、表单和 base 重定向；只绑定 `rpc(string)`，不启用 HTTP 服务。
  Dashboard 独立窗口只允许宿主选择的 loopback `/dash/`，无启动器 RPC，禁止弹窗。按键设置窗口
  同样只允许 `/controls/`，令牌由文档创建脚本写入 sessionStorage，不进 URL。
- Node 仅用于构建；默认 CMake 不联网，SDK 需显式准备。GUI 构建校验 manifest 文件 SHA-256，
  缺失/不匹配明确失败；运行时找不到 Web UI 明确失败，不退回旧界面。
- 模型/RPC 层不 include SDL/WebView，不直接调用窗口或进程 API。
- `library.open_dir` 只由已枚举 installation id 映射 sandbox/log/external；目录不存在明确失败，
  不创建沙盒。log 为保存 last-run.log 的条目目录。
- GUI 日志覆盖写入 `<library-root>/gui.log`，桌面入口同时保留 stderr sink。
- 兼容 `--smoke-frames N` 正整数参数：现表示 WebView 加载后 N 次成功 `library.list`，
  不再声称 ANGLE present 次数；未完成前关闭窗口视为失败。
- 100ms 宿主事件计时器回收进程，不忙轮询。退出 0 静默，非零结构化退出码及有界 UTF-8
  日志尾部经前端队列呈现；不据此推断兼容性。
- 子进程使用宿主绝对 EXE 路径和 `--run-game`，不查找 CLI，stdin 关闭、stdout 继承、stderr 覆盖 last-run.log；显式传递
  `--sandbox-dir <library-root>/sandbox`、`--library-root` 与 `--installation-id`；单实例/移除由 OS 运行锁判定，进程表仅补充。
- 全局设置仅将超采样、解释器和 MCP 端口转为已支持的 CLI 参数；预留选项只保存，
  不改变运行时行为。启动成功后通过宿主回调最小化；GUI 配置不覆盖正在运行的进程。
- ResolveLaunch 在共享库内从实例配置合并全局值，支持 Profile/数据目录、逻辑宽高、超采样、解释器、MCP
  开关/端口/手动步进、临时沙盒。预检移除全部 MCP 参数；临时沙盒不传 `--sandbox-dir`。
  `library.launch` 的 mode 仅允许 normal/preflight/diagnostic，预览和启动使用同一路径。
  库视图和目录打开消费实例数据目录，Profile 覆盖目录的错误按实例隔离。
  本阶段不直接修改沙盒 meta、身份或存档；身份再生成/重置/导入导出入口禁用。
- APK/manifest 损坏失败；资源图标/名称失败记录 fallback，空 PNG 为明确占位；versionCode
  接受完整 uint32。禁止把含控制字符的 label 直接持久化。
- 导入未知 Profile 或未选外部目录 可以入库；无效目录、损坏 APK 和未解决的实例
  占位冲突必须阻止发布。重复 package 要求明确新实例确认，不覆盖旧条目。
- 删除只移除 `library/<installation-id>`；external 与持久存档不删除，运行中不删除。

- 本启动器入库期间枚举/启动/目录 RPC 返回忙；跨进程库锁有界等待超时映射 -32002，库列表整体返回忙而不把条目标为损坏，List 不删除活动 `.importing`；
  作业轮询始终可用。分析与入库线程在服务销毁时 join，再移除本服务持有的快照。
- 导入摘要的版本/API/ABI/Profile 来自解析；外部数据目录为可选用户输入。
  前端同包重复导入须确认新实例，需要数据时允许明确跳过；从不覆盖已有游戏。
- 原生选择器与后台操作期间向导保留并禁用重复操作；选择器取消不丢弃当前摘要。
  当前仅支持单体 APK，不实现 XAPK/APKM/APKS 拆包，也不从目录猜选 APK。

## 禁止

不实现 guest/session、syscall/JNI/GLES 或游戏专用兼容；不解析自由文本日志判断兼容性；
不使用前端猜测替代宿主状态。

## 验证

ogplay_app_tests 覆盖共享库领域与 GUI view_model/import/launch；library_store/library_visuals/
library_lock/cli_library/game_run_args 按主题验证；
`gui_rpc_tests.cpp` 覆盖协议闭合、事实序列化、启动约束和目录映射。
`npm run check` 验证前端类型、协议、库筛选和分块上传边界；`frontend.gui_webui_manifest` 校验制品哈希；
`frontend.gui_smoke` / `frontend.gui_library_smoke` 验证真实 WebView 加载与空库/CJK 非空库 RPC。

- `library.remove {installation_id, confirmed:true, delete_saves?}` 只作用于当前枚举的安装实例；请求字段严格校验，
  入库忙碌或实例运行中（包括损坏条目）拒绝移除。复用 LibraryStore::Remove，删除库内 APK、
  库内数据包、设置与日志，保留引用的 external 和原始 APK；sandbox 默认保留，
  `delete_saves:true` 时先删除。保留的存档由同包下次导入沿用原 id（ADR-0145）。

导入向导与实例设置均可编辑 external_guest_dir，校验和启动预览复用 library 契约；
挂载目录默认为 /sdcard，属于用户数据，与 Profile 无关。
