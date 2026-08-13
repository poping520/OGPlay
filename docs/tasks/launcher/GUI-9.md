# GUI-9 · 库根与持久存档路径统一

## 目标

让主面板启动参数、删除确认和 `--library-root` 覆盖场景消费同一个持久存档根事实。

## 依赖

- GUI-6：LaunchPlan 与既有 `run-apk` 子进程路径。
- GUI-7：删除确认与持久存档保留说明。

## 结果

- `LauncherSandboxRoot` 唯一计算 `<library-root>/sandbox`；LaunchPlan 显式追加
  `--sandbox-dir`，`SandboxStore` 继续在其下按 package 建目录。
- 删除确认通过同一函数展示实际路径，始终只调用 `LibraryStore::Remove`，不触碰存档。
- 默认双击语义不变；`ogplay gui --library-root <dir>` 现在同时隔离库、日志、配置和
  游戏存档，不再把测试库条目连接到用户默认存档。

## 验收

Windows/MSVC `/W4 /WX` 构建；LaunchPlan 逐字段断言 `--sandbox-dir` 与删除保留存档；
架构检查和真实空库冒烟通过；完整 CTest 692/692。
