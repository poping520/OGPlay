# GUI-12 · 可执行文件相对的 Profile/quirk 数据

## 目标

让双击启动器和其同目录 CLI 在没有源码树的机器上仍能加载默认 Profile 与 quirk 注册表。

## 依赖

- GUI-1：`ogplay-gui` 双击入口、同目录 `ogplay` CLI 与安装规则。
- GUI-5：导入时的默认 Profile catalog。
- M5 Quirk Registry：Profile quirk 必须由受检注册表交叉验证。

## 结果

- `HostBundledDataPaths` 优先解析 `<executable>/data`；macOS bundle 解析
  `Contents/Resources/data`；编译期源码目录只作为开发构建的最后回退。
- 常规构建始终把 `data/profiles` 与 `data/quirks.toml` 复制到可执行文件旁；安装规则携带
  同一 payload，macOS bundle 同时把它放入 Resources。
- GUI 导入和 `run-apk` 共用该定位器；用户设置的 Profile 目录只覆盖 profiles，quirks
  始终来自随程序交付的注册表。
- `QuirkRegistry::LoadPackaged` 仍严格校验 schema、字段和测试引用形状；测试源文件存在性
  由源码树 `Load` 与打包前 CI 门禁验证，发行产物无需携带 `tests/`。

## 验收

单元测试锁定 executable → macOS Resources → development fallback 的顺序、构建 payload
完整性和 packaged quirk 失败语义；Windows/MSVC 安装到临时前缀后必须存在
`bin/data/profiles` 与 `bin/data/quirks.toml`，全量 CTest 通过。
