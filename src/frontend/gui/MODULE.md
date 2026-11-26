# 子模块：frontend/gui

## 职责

系统 WebView 启动器（Windows WebView2、macOS WKWebView、Android WebView）的 RPC 模型、导入向导与桌面子进程管理。
平台无关部分（RPC、启动计划、共享装配 `LauncherServices`、`GuiPlatform`）编入 `ogplay_launcher_rpc`，桌面外壳与进程管理编入
ogplay_desktop_app，Android 外壳见 `frontend/android`。
游戏库领域经 ogplay::app 外观共享，GUI 装配当前 EXE 的
`--run-game` 子进程，不拥有 guest/runtime。GUI v2 第一阶段已替换 ImGui；macOS 使用 WKWebView，Linux 暂缓。

## 公共 API

- `GuiPlatform`：平台提供的启动器能力（按键设置、Dashboard、MCP、预检/诊断、打开目录、桌面窗口、宿主路径设置、
  拖放上传、开发者设置）及是否经独立可执行文件启动。桌面全开；Android 全关且游戏在进程内入口启动。
  被关能力的 RPC 方法按未知方法（-32601）拒绝，其设置不进入 fields/values、保存时按未知设置拒绝，已有覆盖值原样保留；
  `library.launch` 只接受 normal，预览只给 normal；启动计划不生成按键令牌/端口，argv 只含游戏选项。
  宿主路径能力关闭时数据包只能复制入库（`external_mode: link` 与 `data_mount.apply` 的 `external_dir` 被拒绝）。
  `single_game`（仅 Android）：同时只运行一个游戏进程；`library.resume`/`library.stop {installation_id}` 只对运行中的
  游戏有效（否则 -32001），经宿主回调回到游戏或请它按自身退出流程退出；有游戏运行时启动另一个返回 -32003。
  `host.features {}` 返回 `{platform, features}`，前端据此决定入口，不自行判断平台。
- `LauncherServices`：各平台共用的 Demo 补入、库视图上下文（宿主跟踪实例 + 运行锁）、Profile/制品事实、开源致谢与 APK 分析回调；
  平台只补充启动、选择器与窗口类回调。
- `RunGuiCommand`：桌面默认模式，直接接受 `--library-root`、`--smoke-frames`；参数错误与
  冒烟失败只返回日志/退出码，交互 GUI 初始化失败（包括无法解析或写入默认库根）显示启动失败消息框。
  入口由 desktop/main 分派；GUI 日志由桌面配置。
  原生窗口、导航限制和目录打开经 `hal::WebViewHost`，不直接包含 Windows/WebView2 API。
- `GuiRpcService::Handle`：复用 `app::JsonRpcDispatcher` 注入模式；同步分派
  `library.list`、`library.launch`、`library.open_dir`、`library.remove`、`library.clear_data`、`library.storage`，以及导入任务与对话框 RPC。
  宿主上下文、进程、目录打开、分析、选择器与 UTC 时间通过显式回调注入。
- `library.analyze {path, apk?}` / `library.upload.begin/chunk/finish`：原生路径或拖放字节进入独立快照，
  单体 APK 上限 1 GiB；分块 base64 最大 256 KiB，严格顺序/总长度检查（拖放只收 APK）。`analyze` 按文件头区分 APK 与
  游戏压缩包（ZIP/RAR/7z，`ogplay/app/archive_import.h`）：压缩包取首选或 `apk` 指定的 APK 解出分析，摘要多一个
  `archive {apks, apk, data{guest_dir, path, entries…, sdcard{…}} | null, extras[{name, files, bytes}]}`，APK 为 `archive: null`。
  任务快照与解出的数据放在库根 `import-staging/work-N`（持锁，下次导入清理无持有者的残留及旧版 `.gui-import-*`）。
- `library.data_pack {job, path?}`：为 ready 任务另选数据包，按分析出的包名规划，返回 `{kind: directory|archive,
  package, data, extras}`（data/extras 与 `archive` 同形）；不带 path 解除，重新分析即丢弃。目录（宿主目录或 `open_tree`
  句柄）以 GuessDataMount 定位后按 `PlanDataDirectory` 只取包名目录（`package` 为所用包名，整目录回退时为 null）；
  文件按内容识别为 ZIP/RAR/7z 后按 `PlanArchive` 规划，APK 拒绝。无数据也无杂项时拒绝。选定后它代替 APK 压缩包的数据，
  `extras` 指它的杂项，不能另给 external_dir，未选中任何内容时 import 拒绝；复制方式解出到任务目录后整体移入。
  `external_mode: link` 只对宿主目录有效且不接受 extras：引用规划挂载点对应的宿主目录（挂载点在所选目录之上时取其祖先），
  未给 external_guest_dir 时挂到规划位置。摘要的挂载预览条目在挂载点高于 Android/<类>/<包> 时展开到该包名目录。
- `library.job.poll/cancel {job}`：每服务仅一个活动导入任务；后台只读分析，失败可查询。
  `library.import {job, new_instance:true, external_dir?, external_guest_dir?, external_mode?:copy|link, extras?}`
  只消费 ready 快照，异步原子发布，默认复制数据包入库；重复提交拒绝，结果返回真实 installation id。压缩包带数据时
  不能另给 external_dir：识别的数据与勾选的 extras 解到任务目录后整体移入条目（`ExternalDataMode::move`），未给
  external_guest_dir 时用规划的挂载位置；宿主 `open_tree` 认得的目录句柄（Android SAF）同样逐文件写入后移入，只能复制。
  普通请求在请求时校验，目录或挂载位置无效时分析结果保留可重试。
  提交期间 poll 返回 `progress {copied_bytes,total_bytes}`；取消在发布前生效，已发布则结果仍为
  imported，worker 停止前状态保持 importing。服务析构先请求取消再回收。
- `dialog.pick {kind:file|directory}` / `dialog.poll {dialog}`：异步原生选择器，取消返回 null；结果带 `label`
  （宿主 `label` 回调给出的显示名，默认即路径）。选择句柄而非宿主路径的平台（Android）经 `label`/`open_tree`
  回调提供显示名、推断挂载用的路径和文件列表；`library.external.inspect` 对句柄按列表描述，不扫描磁盘。
- `library.data_hint {installation_id, external_dir?}`：SuggestDataMount 的结果（kind none/remount/
  needs_data、候选及示例）；`library.data_mount.apply {installation_id, external_guest_dir, external_dir?}`
  经 PatchGameSettings 写挂载位置，未配置数据包时连同目录（引用方式）。均拒绝未知字段，入库时忙碌。
- `library.external.inspect {path, package?}`：同步描述候选数据包目录，供挂载预览：目录优先排序的前 8 个
  顶层名、顶层总数、文件数与字节；递归统计以 20 万项为上限并标记 truncated，不跟随目录符号链接。
  `suggested_guest_dir` 为 GuessDataMount 结果或 null；向导在用户未手动编辑挂载位置时采用。
- LibraryStore、LibraryConfig、SettingDefinitions、GameSettings、APK 检视/图标/Profile、设置继承归
  frontend/library，只经 app 公开头消费。settings.get/set 保存前复用库内 schema/目录校验，
  未知键/损坏配置/过期读取版本拒绝覆盖；后台分析或入库尚未回收时禁止保存。
- 全局 `language` 支持 system 与 zh-CN/zh-TW/en-US/ja-JP/ko-KR/ru-RU/es-ES/pt-BR/fr-FR，默认 system；保存的显式语言不迁移。
  `settings.get/set` 带 `system_language`（同一组标签），前端按它解析 system。
  启动时读配置确定外壳语言，配置不可读时用系统语言；保存后实际语言改变才经
  `GuiRpcHost::language_changed` 在 WebView UI 线程更新外壳菜单。选择器标题在使用时取当前语言；按键窗口标题取该游戏启动时固定的语言，与页面一致。
  ResolveLaunch 把解析后的语言写入游戏进程参数和 LocalizedDisplayName；不影响 guest Locale。
- `settings.get`/`game_settings.get` 带 `reserved_visible`：`NDEBUG` 构建为 false，前端据此隐藏预留项；字段列表仍完整。
  每个字段带 `hidden`：`rpc_settings.cpp` 的 `kHiddenSettings` 列出暂不展示的设置（全局与游戏设置页共用），任何构建都不显示，
  值、校验与启动解析不变。
- `settings.icon_preview`（无参数）：以随包占位图标与同一合成器返回 `{cradle, badge}` 两张 192px PNG data URL，
  素材缺失明确报错。
- `game_settings.get/set {installation_id, values?, revision?}`：只查找已枚举实例，set 的 values
  是完整覆盖集合，删除键恢复继承；读取版本绑定实例、覆盖集合及继承值。未知键、无效目录、
  损坏配置及过期版本明确失败。运行中可保存下次启动设置，不修改运行中的进程。
  get 返回保存值、继承值、字段定义和三种启动模式的 argv/错误，预览不启动子进程。
- `BuildLibraryTiles` / `BuildLibraryDetail` / `LibrarySelection`：统一状态、详情和稳定选择模型。
  库条目与设置页信息只传事实、不传句子：状态码、`damage_reason`、Profile（id 或目录错误原文）、
  数据包（宿主路径与 `owned`）；settings/game facts 以稳定英文 id 为键，值为版本、文件内容、路径或
  代码（null 为不可读文件）。界面语言的措辞全部由 webui 用 `t()` 生成。
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
  据此标记就绪，并在 `edit_requests` 增加（游戏内 F2）时打开窗口、`close_requests` 增加（页面“完成”）时关闭窗口。`controls.open {installation_id}`
  只对就绪实例打开；`dashboard.list` 带 `key_editor_status/detail`，从不返回令牌。
- `about.open_repository {}`：用系统浏览器打开 OGPlay 的 GitHub 仓库（关于页 Star 卡片）；URL 固定在宿主
  （`kOgplayRepositoryUrl`），页面不传网址。桌面经 `SDL_OpenURL`，Android 经 `LauncherBridge.openUrl`（ACTION_VIEW）。
- `about.credits {}` / `about.notices {id}`：关于页开源致谢；`parent` 指向引入该项目的已列项目（须排在其前），OGPlay 直接使用的项目没有此字段。单文件 `data/credits.json` 内嵌许可证原文，由
  `tools/credits.py` 生成（`--check` 检查过期）；项目可带非空 `platforms` 数组（`Windows`/`Darwin`/`Linux`），省略则通用。
  可选 `original_url` 记录原始仓库，经 RPC 原样传递；填写时界面分别显示“当前使用仓库”（`url`）和“原始仓库”，未填写保持单一 URL 展示。
  全平台原样分发同一份清单，仅宿主读取时按当前平台筛选，其他平台的项目不展示、不可查询全文；筛选后父项目仍须在子项前。AOSP NOTICE 以路径引用已发布的 `android/19/notices`，路径不得离开
  数据目录。全文仅在展开时返回，单文件读取上限 1 MiB。
- `ValidateLibraryConfigDirectories`：共享配置目录验证；全局设置保存前调用。
- 原 CJK 字体选择、事件等待与消息队列模型保留用于既有调用/测试，不再驱动 WebView 渲染。

## 不变量

- 请求 envelope 和方法参数采用 closed schema；未知字段、方法、类型与重复 JSON 键明确失败。
  前端不传自由 argv、任意目录打开路径或游戏兼容结论；错误返回 code、诊断文本 message、
  可选 message_id 与字符串 map `params`、next_step_id；宿主不措辞界面文字，ID 均为 Web UI 目录的 `host.*` 消息，
  由 `app::UiMessage` 随异常携带（GuiTextError/GuiArgumentError、LibraryError），无 ID 的失败只给诊断文本。
  导入失败（`failure`）、启动预览错误与自动打开 Dashboard 的失败事件同为 `{text, id?, params?}`；
  `dashboard.list` 的状态说明为 `detail_id`/`key_editor_detail_id`。设置字段只发 key/group/unit/reserved/
  global_default 等结构事实，标签与说明由前端按 key 取 `setting.*` 文案。
- Web UI 只渲染模型事实。状态优先级：损坏 > Profile catalog 不可用 > 缺数据包 > 运行中 >
  ready。通用启动路径是默认状态，无 Profile 不产生状态、提示或条件。
- Profile 条件只在记录了精确 Profile 或 catalog 失效（unavailable）时下发，
  否则为 null。
  外部数据只检查用户已选择的目录是否有效，不从 Profile 推断 required；默认 Profile
  来自 bundled payload。
- 启动器主窗口只加载 bundled `webui/gui/index.html`；WebView2 仅允许该入口导航，禁止新窗口。
  静态 CSP 禁止网络、框架、对象、表单和 base 重定向；只绑定 `rpc(string)`，不启用 HTTP 服务。
  Dashboard 独立窗口只允许宿主选择的 loopback `/dash/`，无启动器 RPC，禁止弹窗。按键设置窗口
  同样只允许 `/controls/`，令牌由文档创建脚本写入 sessionStorage，不进 URL。
- Node 仅用于构建；默认 CMake 不联网，SDK 需显式准备。GUI 构建校验 manifest 文件 SHA-256，
  缺失/不匹配明确失败；运行时找不到 Web UI 明确失败，不退回旧界面。
- 模型/RPC 层不 include SDL/WebView，不直接调用窗口或进程 API。
- `library.open_dir` 只由已枚举 installation id 映射 sandbox/log/external；目录不存在明确失败，
  不创建沙盒。log 为保存 last-run.log 的条目目录。
- 内置 Demo：启动器启动时读一次随包 `demos/` 并在 `show_demo_games` 开时补入缺失的 Demo（失败只记 warn）；
  `library.list` 在该项关时剔除 Demo 条目（条目与存档不动），每项带 `builtin_demo`；`library.remove`
  删除 Demo 时同时关闭该项并返回 `demo_hidden`；`settings.set` 把它由关改开时立即补入。
- `library.storage` 只在请求时（安装信息弹窗）统计 APK、数据包目录与沙盒（游戏数据）的普通文件字节数，
  不跟随链接，尚不存在为 null；有数据包时同时返回生效的 guest 挂载位置。`library.list` 不做目录统计。
- GUI 日志覆盖写入 `<library-root>/gui.log`，桌面入口同时保留 stderr sink。
  `frontend.user_data` 记录有效根与便携模式状态，便携启动时若标准根存在则附记其路径。
- Windows WebView2 数据根直接使用 `<library-root>`，浏览器在其下管理 `EBWebView/`。标准默认位置为
  `%APPDATA%\OGPlay\EBWebView`，便携模式为 `<EXE目录>\userdata\EBWebView`。
  主窗口及辅助窗口共用，不再使用按 EXE 名命名的 AppData 目录。旧浏览器数据不自动迁移或删除。
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
- `library.clear_data {installation_id, confirmed:true}` 同样只作用于当前枚举实例、字段严格校验，入库忙碌或运行中拒绝；
  复用 LibraryStore::ClearSaves 清空存档并保留沙盒身份与库内文件。

导入向导与实例设置均可编辑 external_guest_dir，校验和启动预览复用 library 契约；
挂载目录默认为 /sdcard，属于用户数据，与 Profile 无关。
