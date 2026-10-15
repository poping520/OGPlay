# GUI-10 · C++ 外观接口与游戏库下沉

状态：macOS 实现与定向验收完成；Windows 原生 GUI、Linux 待验。
决策：[ADR-0138](../../adr/development.md#adr-0138)。

## 目标与依赖

按 WU-A 外观/动态 SDL → WU-B 库领域与 OS 锁 → WU-C CLI → WU-D 桌面 → WU-E 契约验收顺序，各自构建与定向验证后提交。
保留 GUI/游戏独立进程、GUI 退出不杀游戏、存档保留及 MCP/编辑器边界。不增加游戏兼容能力。

## 验收与证据

证据目录 `.local/app-split/`。每阶段记录构建、定向测试；最终检查仅导出 app 命名空间、EXE 不链接内核、CLI 无 WebView，真实原版 APK 两条路径各有界复跑一次。
当前主机 macOS；Windows Release 构建、GUI 冒烟、真实导入/启动/退出/移除及 GUI 运行时 CLI 移除拒绝为待验收项；Linux 未实跑。

## WU-A

公开运行选项、参数编解码、日志配置、agent/diagnostics/host 服务已建立；SDL 改为共享库，app 测试实际链接动态外观。全部选项往返及值校验与原参数测试共 5 例、入口/参数 CTest 2 项通过。
构建：`cmake --build --preset dev --target ogplaycore ogplay-cli ogplay_app_tests -j 8`；日志 `wu-a-build-final.log`、`wu-a-ctest.log`。
两个旧 C 入口只在迁移阶段继续服务尚未迁出的 EXE，随 WU-C/D 删除；导出检查此阶段显式允许它们，其余仅允许 app 命名空间。

## WU-B

库领域迁至 frontend/library，公开值类型不暴露 Manifest/Profile 内核类型。ResolveLaunch 与参数格式化替代 GUI 拼参；导入、移除、残留清理采用 OS 排他锁；运行租约跨异常保持，移除/二次运行明确拒绝。全局与库内实例配置读写同样受库锁保护。
构建 `ogplaycore ogplay-cli ogplay_app_tests ogplay_file_lock_tests` 通过；app 23 例、HAL/并发 2 例与入口/参数 2 项通过。并发测试持锁后在另一线程调用 Import/Remove/List，明确 busy、保留活动 `.importing`，释放后清理；运行锁验证移除拒绝与释放、删除保留沙盒。证据 `wu-b-*.log`。

## WU-C

CLI 命令、usage、错误块与 stdio 循环迁入 ogplay_cli_app，仅依赖 app 公开头。新增 library 全部子命令；设置 patch 在库锁内原子合并，复用 schema/目录校验。library run 当前进程内运行，进度 stderr、JSON stdout、日志写实例 last-run.log。删除旧 CLI C 入口；JSON 外观隔离内核 JSON 类型。
定向构建通过；app 26 例（含 3 个 CLI library 主题、缺 Profile/重复导入/严格设置/运行锁拒绝）和入口/参数 2 项通过。证据 `wu-c-build.log`、`wu-c-app-tests.log`、`wu-c-ctest.log`。

## WU-D

桌面模式分派、GUI/RPC/视图、导入状态机及子进程管理迁入 ogplay_desktop_app/desktop，公开前端 GUI 头改为私有。WebView 宿主单独 target ogplay_desktop_webview，仅桌面消费；core 无桌面 GUI/WebView/OGPLAY_HAS_GUI。删除 api.h 与两个 C main；Windows 入口直接调用所属 EXE 的 C++ OgplayMain。GUI 补查 OS 运行锁，进程表只作补充。
定向构建 `ogplaycore ogplay-cli ogplay_app_tests ogplay_desktop_integration_tests` 通过；71 个 app 例、10 个 HTTP/编辑器例和入口/参数 2 项通过。macOS 额外编译可移植 shell/process 源码，不代表原生 GUI 可用。HTTP/native host 夹具另设 integration target，app tests 只链接共享外观及两类 EXE app 库。
最终导出仅 app 命名空间；CLI 链接行只有自己的 ogplay_cli_app 静态库与共享 core，无内核静态库/WebView 导入。证据 `wu-d-*.log`、`link-boundary.json`、`cli-link-command.txt`、`exports.txt`、`cli-imports.txt`。Windows EXE 导入/链接表待原生构建复核。


## WU-E · 契约与最终验收

新增 ADR-0138，更新职责变化的 frontend/app/cli/game/gui/library/desktop 与 HAL WebView/文件锁契约。
能力账本仅更新现有条目的范围/验证入口，CURRENT 只更新 GUI 行。
收尾修正：Windows SDL 的 Runtime install component；同构建 STL/Pimpl 的 MSVC 警告配置；
异步导入冲突保留 typed busy → RPC -32002；无 Profile 的 CLI 外部数据要求为 null；
严格 JSON 空句柄失败；重复包检查不被无关损坏条目阻塞，同包损坏实例仍要求确认；拒绝不安全实例路径。对应回归已通过。

实际命令（仓库根运行，二进制测试的 cwd 为 build/dev）：

```sh
cmake --preset dev
cmake --build --preset dev --target ogplaycore ogplay-cli ogplay_app_tests ogplay_file_lock_tests ogplay_desktop_integration_tests -j 8
(cd build/dev && ./ogplay_app_tests)
(cd build/dev && ./ogplay_file_lock_tests)
(cd build/dev && ./ogplay_desktop_integration_tests)
ctest --test-dir build/dev -R '^frontend\.(app_entrypoints|run_apk_options)$' --output-on-failure
python3 tools/capabilities.py check
ctest --test-dir build/dev -R '^architecture\.(no_raw_output|platform_boundaries|documentation_layout)$' --output-on-failure
cmake --install build/dev --prefix .local/app-split/install --component Runtime
```

- 定向构建退出 0；app 72 例、OS 锁/并发 2 例、HTTP/编辑器集成 10 例、入口/参数 2 项通过。
  未运行全量 CTest，也未构建/运行无关内核测试。
- 导出、CLI 导入与链接表通过；仅 app 外观，CLI 只链接自身 app 静态库和 core 动态库。
  命名空间过滤同时维护 macOS exported_symbol 与 Linux version script（含 app RTTI）；Linux 未实跑。
- OS/GUI 回归覆盖运行中 GUI RPC 移除返回 -32002、异步入库 busy 映射、List 持锁不清理活动目录。
  外部进程持 OS 库锁时，CLI Import/Remove 明确 busy，List 保留 `.importing`；解锁后 List 清理。
  外部 OS 运行锁持有进程模拟崩溃后，CLI Remove 成功，sandbox 哨兵保留。证据 `cross-process-locks.json`。
- Runtime 安装含动态 SDL/core/ANGLE/FFmpeg，安装 CLI --version 退出 0；ANGLE/FFmpeg 与 staging 哈希一致。
  macOS 安装检查 cwd 为 lib，遵循既有 ANGLE `./libEGL.dylib` 加载约定，未扩展任意 cwd 运行保证。
- 能力账本、3 项相关架构检查、变更文档 UTF-8/本地链接与 git diff --check 通过。

### 原版 APK 单次有界复跑

样本 `.local/games/com.rovio.angrybirds_2.3.0.apk`，解析版本 2.3.0/versionCode 2300；
身份和 SHA-256 存 `apk-identity.json`。未开启 survey，不宣称 title gate/绘制验收。

1. `ogplay-cli run-apk <原版 APK> --exit-after-frames 120 --ephemeral-sandbox`：成功呈现 120 帧，退出 0。
2. `ogplay-cli library --library-root <隔离库根> import <原版 APK>` → `settings set <id>
   mcp_enabled=true mcp_port=<空闲端口> mcp_manual_step=true` → `library run <id>`：同一 CLI
   进程运行宿主，复用 tools/run_scenario.py 的 McpClient 请求 step 120 后 shutdown；
   guest frame=120、presentedFrame sequence=114、successfulPresents=113，guestFault=null，退出 0。
   库进程存活时另一 CLI 进程的 remove 与重复 run 均退出 1，原因 instance_running。

两次首条 error 日志均为游戏自身 `BillingService: Could not bind to service.`；无 reached guest fault。
库运行截图 `library-frame.png`、结构化状态 `library-state.json`，命令/帧数/首错/退出码见 `replay-result.json`。
库路径使用 MCP 手动步进，因为 library run 不接受自由运行参数；没有额外增加一次 APK 复跑。

证据索引：`wu-e-build-final.log`、`wu-e-{app,lock,http}-tests.log`、`wu-e-ctest.log`、
`wu-e-final-ctest.log`、`static-ctest.log`、`capabilities-check.log`、`documentation-check.json`、`link-boundary.json`、
`delivery.json`、`install.log` 与以上 replay/lock 文件，全部位于 `.local/app-split/`。

### 未验证缺口

- 无 Windows 环境：windows-msvc Release 的 ogplay 构建、DLL/EXE 原生导入表与 /MD 实构建、
  GUI/库冒烟、真实 GUI 导入→启动→退出→移除、GUI 运行时 CLI 移除拒绝均待复验；
  本机 OS 锁模拟和 CLI 双进程证据不替代 Windows GUI 验收。GUI-9 的 Windows 证据属于旧架构。
- Linux 未实构建/实跑；Linux/macOS 原生 GUI 未接入。
- 原生选择器/拖放、Dashboard/编辑器原生窗口/F2/编辑热应用未在新架构复验。

## 工作单元提交

| 工作单元 | 提交 |
| --- | --- |
| WU-A 外观/参数/动态 SDL | f5fdd746 |
| WU-B 游戏库/OS 锁 | 82b1d1f6 |
| WU-C CLI 与 library 命令 | f03f587d |
| WU-D 桌面/WebView/导出边界 | e5c275be |
| WU-E 契约与验收 | 本文所在提交 |
