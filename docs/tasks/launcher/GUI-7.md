# GUI-7 · 设置、删除与基础版契约收口

## 目标

补齐启动必需设置与安全删除交互，统一用户可见失败的原因、路径、下一步和结构化日志，
并逐项核对主面板基础版契约修订清单。

## 依赖

- GUI-5：导入向导与 Profile/external 事实。
- GUI-6：运行状态、退出结果与子进程生命周期。

## 结果

- 设置页只暴露 Android 系统库与可选 Profile 目录；两个 SDL 目录选择器使用 UTF-8
  路径，保存前验证已配置目录存在，库根只读。Profile 留空恢复内置默认，保存后热重载
  导入/Profile 状态。
- 磁贴右键进入删除确认：逐项说明库内删除内容，并显示不会删除的原地数据包和
  `<library-root>/sandbox/<package>` 持久存档；运行中禁止删除。删除继续复用
  `LibraryStore::Remove` 的唯一严格路径。
- 缺 Profile、缺数据包、损坏和运行中磁贴均给出可执行下一步；配置、导入、启动、
  删除的模型错误把错误码、路径和原因同时写结构化日志。非零游戏退出提示日志证据可随
  issue 提交。
- macOS bundle 构建把 `ogplay` CLI 放入 `Contents/MacOS`；README 登记双击入口、
  开发子命令和平台构建产物。frontend/gui、frontend、loader、模块索引、第三方清单、
  usage 与四项能力账本已按 04 §5 全量核对。

## 验收

Windows/MSVC `/W4 /WX` 构建；设置目录校验、配置 round-trip、删除保留 external/存档、
状态文案测试全绿；真实空/非空库 ANGLE 冒烟与完整 CTest 692/692。
