# GUI-5 · 导入向导与 Profile/external 摘要

## 目标

把 GUI-3 的 APK visuals、session 精确 Profile 事实和 GUI-2 原子入库连接为可用的
SDL/ImGui 导入向导，不复制 Profile TOML 或匹配逻辑。

## 依赖

- GUI-3：manifest/arsc/PNG visuals 提取链。
- GUI-4：库网格与状态角标消费面。

## 结果

- session 新增只读 `ApkProfileSummary`，封装稳定 profile id/name 与 required external
  布尔事实；GUI 不遍历 `data.mounts`，缓存条目也经同一公共 API 刷新角标。
- 系统 APK 对话框回调经共享 mailbox 投递 SDL event；后台线程只读分析 APK，完成后
  主线程显示图标、名称、package、版本、Profile 与数据包要求。
- 未匹配 Profile 仍可入库；required external 可跳过或选择存在的绝对目录，入库后
  网格即时刷新。重复 package、损坏 APK、Profile 目录故障与无效 external 明确呈现
  原因和可执行下一步。
- `AnalyzeApkImport`/`BuildLibraryImport` 不依赖 ImGui/SDL，时间戳由控制器注入；
  `LibraryStore::Import` 继续拥有唯一原子发布路径。

## 验收

Windows/MSVC `/W4 /WX` 构建；导入模型与 Profile 摘要聚焦测试全绿；真实空/非空库
冒烟与完整 CTest 全绿，准确计数记录在 `docs/state/CURRENT.md`。
