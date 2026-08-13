# GUI-3 · APK application visuals 提取链

## 目标

扩展严格 binary Manifest 事实并复用既有 arsc/APK/stb 能力，在导入时得到可缓存的
显示名称与固定尺寸 PNG；资源侧失败走显式 fallback，不阻止合法 APK 入库。

## 依赖

- GUI-2：`LibraryStore` 接收提取后的 `display_name` 与 `icon_png` 并原子发布。
- `loader.apk_manifest_identity`、`loader.arsc`、`loader.apk_stored_entry`。

## 结果

- `AndroidManifestFacts` 严格区分 icon resource id、label resource id 和 label 字面量；
  attribute 缺失合法，非法 typed value 与结构损坏明确失败。
- `ExtractApkApplicationVisuals` 完整复用 `ParseArsc` 与 `ReadApkEntry`；默认资源名称
  成功解析，PNG 经既有 stb 解码并归一化为 128×128 RGBA PNG。
- 缺 attribute/arsc/resid、空 label、非 PNG 或 `.9.png`、条目读取失败、解码失败均返回稳定
  `ApplicationVisualFallback`；名称回到 package，空 PNG 表示视图使用占位磁贴。
- 合成 APK 将 manifest、resources.arsc、PNG 和 `LibraryStore` 缓存连成机器可判定全链。

## 验收

Windows/MSVC `/W4 /WX` 构建；manifest 与提取链聚焦测试全绿；架构检查与全量
CTest 全绿，具体计数记录在 `docs/state/CURRENT.md`。
