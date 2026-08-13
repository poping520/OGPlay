# 子模块：frontend/gui

## 职责

提供独立的 SDL3 + ANGLE/GLES2 + Dear ImGui 主面板 shell，以及不依赖 ImGui 的游戏库
模型。GUI 只维护宿主游戏库并装配既有 CLI 子进程，不拥有 guest/runtime 行为。

## 公共 API

- `RunGuiCommand`：`ogplay gui` 的开发/CI 入口，只接受 `--library-root` 与
  `--smoke-frames`。
- `RunGuiStandalone`：双击 `ogplay-gui` 的零参数产品入口；失败通过图形消息框呈现。
- `LibraryStore`：枚举、原子导入和删除 `<root>/library/<package>/` 条目；损坏条目
  仍以带错误原因的记录返回。
- `LoadGuiConfig` / `SaveGuiConfig`：严格 schema 1 TOML 配置读写。
- `ExtractApkApplicationVisuals`：从 APK 严格读取 manifest 身份，复用 loader 的
  resources.arsc 与统一条目读取；名称回退 package，图标归一化为 128×128 PNG，
  非致命失败以 `ApplicationVisualFallback` 枚举返回。
- `BuildLibraryTiles`：将库条目与运行/required-external 事实合成为名称排序的磁贴，
  状态优先级固定为损坏、缺 Profile、缺数据包、运行中、ready。
- `SelectCjkFont`：按注入候选顺序选择宿主字体，全部缺失时返回空路径供 ASCII 回退。
- `AnalyzeApkImport` / `BuildLibraryImport`：组合 GUI-3 visuals 与 session 精确 Profile
  匹配，生成可确认的导入摘要和 GUI-2 原子入库请求；时间戳由调用方注入。
- `GuiImportUi`：SDL 异步文件/目录对话框、后台只读分析和 ImGui 三态摘要控制器；
  回调只经共享 mailbox 投递事件，文件 IO 与 Profile 判断留在模型/session 层。

## 不变量

- shell 使用 SDL3 的 OpenGL ES profile，强制 EGL/OpenGL ES driver，使 ImGui GLES2
  backend 运行在随程序交付的 ANGLE 上；不得回退出第二套桌面 GL renderer。
- GUI 进程日志覆盖写入 `<library-root>/gui.log`；CLI 入口同时保留同源 stderr sink。
- `--smoke-frames` 必须为正整数，完成指定成功 present 数后正常退出。
- 模型层不得 include ImGui/SDL，不触碰窗口或进程 API。
- APK/archive/manifest 损坏必须失败；图标/名称资源失败不得阻止导入，也不得静默，
  调用方必须记录返回的 fallback 枚举，空 `icon_png` 明确表示使用内置占位磁贴。
- 视图每帧只消费 `LibraryTile` 事实，不读取 meta/Profile 或管理进程；长名称单行省略，
  悬停显示全文，状态角标互斥。
- 精确 Profile 和 required external 事实必须来自 `MatchApkTitleProfile` 与
  `SummarizeApkProfileMatch`/`FindApkProfileSummary`，不得在 GUI 遍历 Profile mounts。
- 未匹配 Profile 或跳过 required external 仍允许入库并显示对应角标；APK/manifest
  损坏、重复 package 和所选 external 目录不存在必须阻止发布并给出下一步。

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
`frontend.gui_smoke`/`frontend.gui_library_smoke` 在有界三帧内验证真实
SDL3/ANGLE/ImGui 空库与 CJK 非空库窗口。
