# GUI-10 · C++ 外观接口与游戏库下沉

状态：实施中。决策：ADR-0138（待 WU-E 收尾）。

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
