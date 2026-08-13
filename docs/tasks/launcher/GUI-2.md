# GUI-2 · LibraryStore 与 GuiConfig

## 目标

提供完全不依赖 SDL/ImGui 的严格游戏库模型：配置与条目 TOML、APK 复制、原子目录
发布、重复拒绝、损坏显式枚举和有界删除。

## 结果

- `GuiConfig` schema 1 保存 system/profile 绝对目录；未知/重复 key、非法 UTF-8、
  非绝对路径明确失败。
- `LibraryStore` 只在 `<root>/library/` 内操作，导入经同根临时目录整体 rename 发布。
- 同 package 不覆盖；meta 包名不符或 APK 丢失作为损坏条目返回，不静默跳过。
- 删除不触碰 `external_dir`，也不触碰当前 ADR-0020 使用的同级 `<root>/sandbox/`。

## 验收

Windows/MSVC `/W4 /WX` 构建；模型聚焦 7/7，GUI/架构联合 13/13，全量 CTest
672/672。
