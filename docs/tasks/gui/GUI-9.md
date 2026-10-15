# GUI-9 · 统一应用 DLL 与桌面/CLI 入口

状态：Windows 完成；其他平台未实构建。决定见 ADR-0137。

## 目标与依赖

Windows 交付 `ogplay.exe`、`ogplay-cli.exe` 与 `ogplaycore.dll`；同名共享库 target 为
`ogplaycore`。桌面入口默认 GUI，以自身 `--run-game` 子进程运行游戏；CLI 只用于开发与
自动化。两入口共用参数解析、运行装配与执行，不保留旧入口兼容包装。

按依赖顺序实施：公共运行服务 → DLL/双入口与构建交付 → GUI 自身启动及工具迁移 → 定向验收。
GUI/游戏进程、MCP、Profile、沙盒和按键编辑器既有边界保持；不补其他平台 GUI 或游戏兼容能力。

## 验收

- Windows Release 定向构建；导出/导入检查证明 EXE 仅通过窄 C ABI 使用共享应用实现。
- 运行参数、GUI 真实入口冒烟、LaunchPlan/RPC/MCP 定向回归；安装目录及无 CLI 启动验证。
- Angry Birds 2.3.0 原版样本基线、桌面游戏模式和 CLI 各一次有界复跑；记录首错与退出。
- 真实 GUI 导入/启动/退出/移除、Dashboard/编辑器及存档保留；无法验证的项明确列缺口。
- 能力账本、相关契约、当前状态与操作手册更新；不宣称 title gate 或跨平台验收。

证据目录：`.local/app-refactor/`。


## 已验证证据

- Windows Release 的 `ogplay`、`ogplay-cli`、`ogplaycore` 与测试目标完成定向构建；
  GUI 关闭配置也完成 CLI 构建及独立 Runtime 安装。
- 原版样本：重构前、桌面游戏模式、CLI 和无 GUI 宿主的 CLI 安装副本均推进 120 帧，
  无 guest fault、有呈现截图，MCP shutdown 退出 0。该证据不等于 title gate。
- 真实 WebView/RPC 流程结果：`.local/app-refactor/gui-flow-1792065836/result.json`。
  安装目录移走 CLI，中文库路径导入原版 APK，实际点击启动按钮进入自身子进程并推进
  120 帧；打开 Dashboard/编辑器窗口；损坏库内 APK 产生非零退出而 GUI 存活；恢复 APK
  后重启并关闭 GUI，游戏继续运行；正常关闭游戏后重开 GUI 移除条目，sandbox 内容保留。
- 原生文件选择器、拖放和编辑器编辑/热应用/F2 未复验；其他平台未实构建。
- 中文库路径验证暴露 sandbox 日志的本地编码转换，已改为 UTF-8 并补持久沙盒回归。

完整命令、构建日志、定向测试、截图、导入/导出及安装检查保存在 `.local/app-refactor/`；
按用户要求同时提交 `third_party/ffmpeg-prebuilt` 的 Windows SDK 指针更新；子模块自身工作区无未提交改动。不修复独立运行时缺口。


## 收尾结果

- 定向构建退出 0；恢复 `OGPLAY_BUILD_GUI=ON`。CLI-only 构建及独立安装同样退出 0。
- 入口/参数/GUI 冒烟 6 项、Dashboard/HTTP/资源 4 项、相关 GUI/运行配置/VFS/宿主路径
  52 例均通过；另有 3 项相关架构检查、Scenario runner 自检与能力账本检查通过。
- C ABI 只导出两个入口；两个 EXE 各只导入自身对应入口。缺 DLL 的隔离副本启动明确失败。
- 最终 Runtime 安装副本的 CLI 再跑 120 帧正常退出，日志确认 payload 来自安装目录。
- UTF-8、当前修改文档的本地链接、`git diff --check` 通过；未运行全量 CTest。
