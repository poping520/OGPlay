# SBX-7 · session/CLI 接线与一次性沙盒

## 目标（一句话）

让 `run-apk` 默认为每个 package 打开持久沙盒，并给自动化一条确定性的
一次性沙盒路径。

## 依赖

- SBX-3（覆盖层）。SBX-4..6 的通道收敛不阻塞本 WU，各自落地后即刻生效。

## 验收

- 默认根为平台用户数据目录 + `sandbox/`；`--sandbox-dir` 显式指定；
  `--ephemeral-sandbox` 关闭持久；后两者互斥。
- preflight 输出沙盒根、package 键与用量事实。
- 装配失败明确终止并给出可执行修复，不静默降级为内存模式。
- scenario runner 默认一次性沙盒，`asphalt5.title_flow` golden 逐位不变。

## 交付（完成）

- `hal::HostUserDataDirectory()`：平台用户数据根，三平台各自实现
  （平台分支只允许在 `hal/<platform>`，这是架构门禁 `platform_boundaries`
  强制的）。`frontend::DefaultSandboxRoot()` 只决定 OGPlay 在其下放什么。
- `session::ProfileWritableRoots()`：可写命名空间取自 manifest 受检 package
  身份（`/data/data/<pkg>` 与 `/sdcard`），Profile 无需为存档新增声明；
  `AssembleProfileVfs` 增加可选沙盒参数，attach 失败即装配失败。
- `run-apk` 新增 `--sandbox-dir` / `--ephemeral-sandbox`，preflight 与结构化
  日志报告沙盒事实，崩溃残留清理计数进 warn。
- `src/frontend/cli/run_apk_vfs.{h,cpp}`：底层挂载与沙盒装配从 `run_apk.cpp`
  拆出（该文件此前已超 800 行，本次拆分把它降到 856 行，仍待后续继续拆分）。
- `tools/run_scenario.py` 默认传 `--ephemeral-sandbox`。

## 验证

macOS/arm64 CTest 636/636（含扩展后的 `frontend.run_apk_options` 门禁）；
真实 APK preflight 建出 `<root>/<package>/{meta.toml,fs/}` 并报告用量；
`asphalt5.title_flow` 468/468000、SHA-256 `9ee57323…`、clean shutdown 不变。
