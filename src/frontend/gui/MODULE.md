# 子模块：frontend/gui

## 职责

提供独立的 SDL3 + ANGLE/GLES2 + Dear ImGui 主面板 shell，以及不依赖 ImGui 的游戏库
模型。GUI 只维护宿主游戏库并装配既有 CLI 子进程，不拥有 guest/runtime 行为。

## 公共 API

- `RunGuiCommand`：`ogplay gui` 的开发/CI 入口，只接受 `--library-root` 与
  `--smoke-frames`。
- `RunGuiStandalone`：双击 `ogplay-gui` 的零参数产品入口；失败通过图形消息框呈现。
- `HostBundledDataPaths`：解析随可执行文件交付的默认 Profile/quirk payload；源码树仅作
  开发回退。
- `LibraryStore`：枚举、原子导入和删除 `<root>/library/<package>/` 条目；损坏条目
  仍以带错误原因的记录返回；枚举前清理崩溃遗留的 `.importing` 目录。
- `LoadGuiConfig` / `SaveGuiConfig`：严格 schema 1 TOML 配置读写；`.bak` 提供跨 rename
  崩溃恢复，旧配置在新配置发布前始终可恢复。
- `ExtractApkApplicationVisuals`：从 APK 严格读取 manifest 身份，复用 loader 的
  resources.arsc 与统一条目读取；名称回退 package，图标归一化为 128×128 PNG，
  非致命失败以 `ApplicationVisualFallback` 枚举返回。
- `ResizeArgbBilinear`：以像素中心双线性插值归一化图标 ARGB，拒绝尺寸/像素数不一致。
- `BuildLibraryTiles`：将库条目与运行/required-external 事实合成为名称排序的磁贴，
  状态优先级固定为损坏、Profile catalog 不可用、缺 Profile、缺数据包、运行中、ready。
- `GuiMessageQueue`：按 FIFO 保存运行/启动诊断，仅在没有其他 popup 时激活下一条。
- `SelectCjkFont`：按注入候选顺序选择宿主字体，全部缺失时返回空路径供 ASCII 回退。
- `GuiEventWaitMilliseconds`：普通运行最多等待 100ms 事件，smoke 为 0ms。
- `AnalyzeApkImport` / `BuildLibraryImport`：组合 GUI-3 visuals 与 session 精确 Profile
  匹配，生成可确认的导入摘要和 GUI-2 原子入库请求；时间戳由调用方注入。
- `CanDismissImportModal`：宿主选择器未决时禁止导入模态先行关闭。
- `GuiImportUi`：SDL 异步文件/目录对话框、后台只读分析和 ImGui 三态摘要控制器；
  回调和 detached 分析结果只经共享 mailbox 投递事件，文件 IO 与 Profile 判断留在
  模型/session 层；控制器析构不等待分析完成。
- `LauncherSandboxRoot` / `BuildLaunchPlan`：从库根、严格库条目与 `GuiConfig` 生成
  唯一 `run-apk` argv，并在 spawn 前验证全部宿主输入。
- `GuiProcessManager`：以 SDL3 启动/非阻塞回收游戏子进程，维护同 package 单实例和
  `last-run.log`；GUI 退出只解除跟踪，不终止游戏。
- `ValidateGuiConfigDirectories` / `GuiSettingsUi`：保存前严格验证已配置目录；设置页只
  编辑 system/Profile 目录，库根只读，Profile 留空使用内置默认。
- `GuiManagementUi`：呈现删除边界并调用 `LibraryStore::Remove`；运行中条目拒绝删除，
  external 数据和 `<library-root>/sandbox/<package>` 存档始终保留。

## 不变量

- shell 使用 SDL3 的 OpenGL ES profile，强制 EGL/OpenGL ES driver，使 ImGui GLES2
  backend 运行在随程序交付的 ANGLE 上；不得回退出第二套桌面 GL renderer。
- GUI 进程日志覆盖写入 `<library-root>/gui.log`；CLI 入口同时保留同源 stderr sink。
- `--smoke-frames` 必须为正整数，完成指定成功 present 数后正常退出。
- 普通主循环必须用 SDL 事件等待降低空闲渲染频率，最长 100ms 后唤醒轮询子进程与导入；
  smoke 不得等待。CJK 宿主字体作为 18px scalable 主字体，不得拉伸像素 fallback。
- 模型层不得 include ImGui/SDL，不触碰窗口或进程 API。
- APK/archive/manifest 损坏必须失败；图标/名称资源失败不得阻止导入，也不得静默，
  调用方必须记录返回的 fallback 枚举，空 `icon_png` 明确表示使用内置占位磁贴。
- 视图每帧只消费 `LibraryTile` 事实，不读取 meta/Profile 或管理进程；长名称单行省略，
  悬停显示全文，状态角标互斥。
- 精确 Profile 和 required external 事实必须来自 `MatchApkTitleProfile` 与
  `SummarizeApkProfileMatch`/`FindApkProfileSummary`，不得在 GUI 遍历 Profile mounts。
- Profile catalog 不可用时所有非损坏磁贴必须显示显式不可用状态，不得把空的
  required-external 集合解释为 ready。
- 未匹配 Profile 或跳过 required external 仍允许入库并显示对应角标；APK/manifest
  损坏、重复 package 和所选 external 目录不存在必须阻止发布并给出下一步。
- 库枚举必须删除所有 `.importing` 崩溃残留；配置替换必须保留可恢复旧版本，启动发现
  仅有 `.bak` 时自动恢复。关闭 GUI 不得 join 正在进行的只读 APK 分析。
- 子进程 CLI 只能从 GUI 可执行文件同目录解析，不查询 PATH；stdin 关闭、stdout
  继承、stderr 覆盖重定向到条目日志，并显式传 `--sandbox-dir <library-root>/sandbox`。
  退出 0 静默，非零结果呈现退出码与有界日志末尾。
- 日志尾部按字节截断时必须前移到下一个完整 UTF-8 codepoint，不得把 continuation byte
  作为弹窗首字节。
- SDL 对话框返回的路径必须按 UTF-8 解码；每个用户可见失败同时记录结构化原因，模型
  错误还必须记录错误码与路径，弹窗必须给出可执行下一步。
- SDL 宿主文件/目录选择器未决时不得关闭其所属导入模态；必须等待回调完成选择或取消，
  禁止留下后台分析或阻塞后续点击的悬挂选择器。
- 每个 ImGui 按钮必须经 `GuiButton` 提交；同一帧同一有效作用域的按钮 ID 必须唯一，
  同名按钮使用 `##` 隐藏后缀或 `PushID` 区分，重复即让真实 GUI 冒烟明确失败。
- 删除只移除 `library/<package>`；不得触碰库外 external 或同库根的持久存档。设置保存
  后必须重载 Profile catalog，不能继续使用旧目录事实。
- 默认 Profile 与 quirk 注册表必须来自同一完整 bundled data payload；用户覆盖 Profile
  目录时仍使用 bundled quirk 注册表，发行运行不得依赖编译机源码路径。
- 游戏运行结果和启动诊断必须排队，且只在导入、设置、删除、上下文菜单和退出确认均
  未打开时呈现；不得用根级 `OpenPopup` 顶掉正在进行的工作流。

## 禁止

- 不实现 syscall/JNI/GLES/game-specific compatibility。
- 不在 GUI 进程内启动 guest session。
- 不解析自由文本日志来判断兼容性状态。

## 测试

`tests/frontend/gui_model_tests.cpp` 锁定配置、导入、损坏、重复与删除边界；
`tests/frontend/gui_visuals_tests.cpp` 以合成 APK 锁定 resid→arsc→PNG→128×128
缓存全链和逐级回退；
`tests/frontend/gui_view_model_tests.cpp` 锁定排序、角标优先级与字体候选；
`tests/frontend/gui_import_tests.cpp` 锁定无 Profile 分析、metadata 装配、可选 external
与明确失败；
`tests/frontend/gui_launch_tests.cpp` 锁定 argv、spawn 前校验、单实例状态、退出码与
有界日志末尾；
`tests/frontend/gui_model_tests.cpp` 同时锁定设置目录校验和删除保留 external/存档；
`frontend.gui_smoke`/`frontend.gui_library_smoke` 在有界三帧内验证真实
SDL3/ANGLE/ImGui 空库与 CJK 非空库窗口，并由 `GuiButton` 审计可见按钮 ID 唯一性。
