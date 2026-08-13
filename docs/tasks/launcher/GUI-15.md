# GUI-15 · 启动器持久化与后台分析健壮性

## 目标

让崩溃恢复、配置替换和大 APK 分析期间退出均不留下用户不可见的阻塞或损坏。

## 依赖

- GUI-2：LibraryStore 与 GuiConfig 原子持久模型。
- GUI-5：后台 APK 分析控制器。

## 结果

- `LoadEntries` 在发布库事实前清理全部 `.<package>.importing` 目录；清理失败明确返回
  `io_error` 与路径，不把临时目录伪装为库条目。
- 配置替换先把旧目标移到 `.bak`，新文件发布失败则恢复；若进程恰在两次 rename 之间
  退出，下次 `LoadGuiConfig` 自动从 `.bak` 恢复，不再先删除唯一有效配置。
- APK 分析线程通过共享 mailbox 投递结果并自行释放，不再由 `std::future` 析构 join；
  主面板关闭不会等待最多 1 GiB APK 的读取/解析，晚到结果不访问已销毁 UI 状态。

## 验收

模型测试锁定 config backup 自动恢复和任意 package 临时目录清理；Windows/MSVC
`/W4 /WX` 构建、真实 GUI 冒烟和全量 CTest 通过。
