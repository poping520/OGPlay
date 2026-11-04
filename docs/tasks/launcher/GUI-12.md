# GUI-12 · 可执行文件相对的 Profile 数据

## 目标

让双击启动器和其同目录 CLI 在没有源码树的机器上仍能加载默认 Profile。

## 依赖

- GUI-1：`ogplay-gui` 双击入口、同目录 `ogplay` CLI 与安装规则。
- GUI-5：导入时的默认 Profile catalog。

## 结果

- `HostBundledDataPaths` 优先解析 `<executable>/data`；macOS bundle 解析
  `Contents/Resources/data`；编译期源码目录只作为开发构建的最后回退。
- 常规构建始终把 `data/profiles` 复制到可执行文件旁；安装规则携带
  同一 payload，macOS bundle 同时把它放入 Resources。
- GUI 导入和 `run-apk` 共用该定位器；用户设置的 Profile 目录只覆盖 profiles。

## 验收

单元测试锁定 executable → macOS Resources → development fallback 的顺序、构建 payload
完整性；Windows/MSVC 安装到临时前缀后必须存在
`bin/data/profiles`，全量 CTest 通过。
