# GUI-20 · OGPlay 中英文支持补全

状态：macOS 实现与定向自动验证完成，2026-11-04；解锁后 GUI 冒烟 3/3 通过；Windows 未编译，App 内点击待验。
依赖：GUI-14、已有未提交的游戏关闭确认框实现（保留）。

目标：沿用 language 设置与中文原文作键的机制，覆盖启动器之外的宿主界面、启动器收到的
宿主状态/错误和按键设置页，并增加 system；不引入依赖或翻译框架。

## 范围与完成情况

| 请求章节 | 完成内容 |
| --- | --- |
| 一 | app 文案表并排存放中英文；编译期检查枚举完整、顺序和非空。严格语言标签解析；HAL 只读 SDL 首选 locale 第一项并释放，空结果归英文。 |
| 二 | language 默认 system；保留已存显式语言；settings.get/set 返回 system_language；草稿/保存/恢复均按宿主语言解析，localStorage 只存实际语言。 |
| 三 | GameRunOptions/CLI 增加 --ui-language，重复、缺值、非法值失败；ResolveLaunch 解析语言后传标题及子进程，游戏关闭确认框消费 Text。 |
| 四 | 上层提供 WebViewShellText、选择器/按键窗口标题及 RPC 回退 next_step；语言保存后主线程更新菜单。macOS 保留 windowsMenu、selector 和快捷键；Windows 使用 UTF-8 转宽字符。Info.plist 声明 zh_CN/en。 |
| 五 | GUI 错误支持可选字符串 map params，两处拼接消息改模板，私有 GuiTextError 捕获保留参数。启动器显示的宿主 detail、错误、预览错误、导入失败及宿主事件经 t；指定 C++ 文件文案全部入英文表。 |
| 六 | ui-kit 共享 i18n 核心，GUI/controls 分别注册自己的表；controls 全部自有文案、标题、html lang 接语言；control.get 返回进程固定语言，响应前及令牌失败页使用 navigator.language。 |
| 七 | 共用 TS/C++ 扫描辅助，C++ 区分注释、字符、转义、raw string 与数字分隔符；加入宿主/原生路径防漏翻，controls 要求中文字符串位于 t 调用；补齐指定 C++ 回归。 |
| 八 | 更新真实变化的八个 MODULE、任务索引和能力账本；frontend.gui_language 保持 partial。CURRENT.md 与 ADR 未改。 |
| 九 | WebUI build、两类 C++ 测试目标及定向用例通过；格式化后的三项 GUI 冒烟在解锁后复跑 3/3 通过；未运行全量 C++ 测试、游戏或手动 GUI 操作。 |
| 十 | 本任务 C/C++ 文件整文件 clang-format 后重新构建并重跑定向测试；Objective-C++ 不整文件格式化，只含功能改动。release ogplay_bundle 重建并验证 ad-hoc 签名；不提交。 |

没有功能范围偏离。ControlRequestContext 在下层只携带解析后的语言标签，避免 frontend 内核依赖
app 外观；系统语言解析和文案所有权仍在 app，上层将固定标签传入。

## 线程与边界

第三方 WebView 的 on_message 经 dispatch 调用绑定回调：macOS dispatch_get_main_queue，
Windows 投递消息给 UI 线程。因此 GuiRpcService 保存成功后的 language_changed 回调与
SetShellText 在宿主主线程执行；无需工作线程直接操作菜单。选择器标题在调用时读取当前
启动器语言；运行中游戏、按键页及其窗口标题使用游戏启动时的固定语言（LaunchPlan 记录），切换启动器语言不改变它们。

不做：webui/apps/dashboard 和 dash.* RPC、guest Locale、CLI/日志/原始异常文字、系统控件按钮、
宿主错误码化和 Linux GUI。无新依赖，不运行游戏，不使用 osascript/lldb 或人工截图驱动界面。

## 验证

实际命令（C++ 为格式化后的运行；GUI 冒烟已在解锁后复跑通过）：

```sh
npm run build --prefix webui
cmake --build --preset dev --target ogplay_tests ogplay_app_tests ogplay_bundle
build/dev/ogplay_app_tests --source-file='*ui_text_tests.cpp,*game_run_args_tests.cpp,*gui_launch_tests.cpp,*gui_rpc_tests.cpp'
build/dev/ogplay_tests --source-file='*control_requests_tests.cpp,*window_input_tests.cpp'
ctest --test-dir build/dev -R 'frontend\.gui_webui_manifest|frontend\.gui_smoke|frontend\.gui_library_smoke' --output-on-failure
python3 tools/capabilities.py check
cmake --build --preset release --target ogplay_bundle
```

- WebUI：类型检查通过；10 个文件、75/75 用例通过；GUI/controls manifest 已重建，Dashboard bundle 未修改源码。
- ogplay_app_tests 定向：48/48 用例、1123/1123 断言通过。
- ogplay_tests 定向：17/17 用例、269/269 断言通过；其余用例由 source-file 筛选，不是失败。
- CTest：解锁后对当前最终制品复跑 gui_webui_manifest、gui_smoke、gui_library_smoke，3/3 通过，耗时 4.32 秒。此前两个 WebView 用例曾在锁屏时各 30 秒超时，只读 IORegistry 确认 CGSSessionScreenIsLocked=Yes；用户解锁后原命令通过，无代码修改。复跑日志：`.local/gui20/smoke-unlocked.log`。
- 能力账本：439 项校验通过；git diff --check 通过。
- release 产物：`build/release/OGPlay.app`，由 ogplay_bundle 装配并通过 codesign --verify --strict --deep。

首轮只构建 ogplay 时，已有 app 包内 Frameworks 的旧 dylib 导致两项冒烟缺符号；按现有
RuntimePackaging.cmake 改为构建 ogplay_bundle，同步运行库/数据后 3/3 通过，无需改打包机制。
BootDex/payload 审计未因本次格式化失效，未刷新其元数据。
.app 包内 GUI/controls manifest 与生成目录一致，hash 检查通过；Frameworks 中 core 与 release 输出一致，Info.plist 的 CFBundleLocalizations 为 zh_CN/en。
本次命令与输出保存在未入库的 `.local/gui20/`。

## 验收修正

- 首次启动没有记住的语言时按 `navigator.language`（`initialLanguage`），英文系统闪屏不再先显示中文，设置读取失败也不会停在中文。
- 按键窗口标题改用游戏启动语言，与页面一致；不再在复用窗口时改标题。
- 按键页占位符由 `{arg0}` 改为具名（`{zone}`、`{direction}`、`{key}` 等）。
- 还原 `webview_host.mm` 的整文件格式化。
- 修正后复跑：WebUI 76/76、app 定向 48/48、core 定向 17/17、桌面窗口用例 2/2、GUI 冒烟 3/3，release 重建并验签。

## 缺口与人工点击

Windows webview_host.cpp 已同步接口并静态自查，未在本机编译/运行；Dashboard 页面保持未翻译。
自动用例不替代实际 App 内点击，也未验证英文系统下 NSOpenPanel 的系统按钮语言。

请在 release OGPlay.app 内检查：

1. 中文/English 分别启动游戏，关闭确认框的标题、正文、关闭/取消按钮均正确；取消后继续运行。
2. 保存切换语言后，macOS 主菜单即时更新，快捷键与窗口菜单仍可用。
3. APK 与数据目录选择器标题随当前语言变化。
4. 按键设置页、令牌页和窗口标题中英文正确；启动器切换后运行中游戏、按键页及其窗口标题保持启动语言。
5. 一条宿主错误英文显示正确（例如游戏未运行时打开按键设置），占位符参数无遗漏。
6. 新库默认“跟随系统 / Follow system”；中文/英文系统各验一次，显式语言保存后重开保留。
7. 英文系统语言下 NSOpenPanel 的系统 Open/Cancel 按钮使用系统语言。

