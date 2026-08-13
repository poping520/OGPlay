# GUI-6 · LaunchPlan、子进程与运行结果

## 目标

让 ready 库条目通过同目录 `ogplay run-apk` 的唯一受检路径启动，并在主面板中呈现
运行状态、非零退出结果和有界日志末尾。

## 依赖

- GUI-4：库网格与互斥运行中角标。
- GUI-2：严格库条目和 `GuiConfig`。

## 结果

- `BuildLaunchPlan` 在 spawn 前校验 CLI、APK、system/Profile/external 目录，且只装配
  设计登记的 `run-apk` 参数；CLI 只从 GUI 可执行文件同目录解析，不查询 PATH。
- `GuiProcessManager` 通过 SDL3 创建子进程：stdin 关闭、stdout 继承、stderr 以 UTF-8
  路径覆盖写入条目的 `last-run.log`；主循环逐帧非阻塞回收退出码。
- 同 package 只允许一个实例；运行中角标随启动和退出即时刷新。退出码 0 安静结束，
  非零结果显示退出码、完整日志路径和有界日志末尾。
- 主面板关闭时如仍有游戏运行则要求确认；确认退出只销毁 SDL 进程跟踪对象，不终止
  游戏子进程。

## 验收

Windows/MSVC `/W4 /WX` 构建；LaunchPlan 参数/前置校验、重复运行、退出码和日志末尾
测试全绿；真实空/非空库 ANGLE 冒烟与完整 CTest 691/691。
