# VFS-04 · CLI 数据输入脱离 Profile 必需声明

目标：无匹配 Profile 的 `run-apk` 可读取显式 external 目录和标准 OBB 原文件路径。

范围：单个 `--external-dir`、可选 `/sdcard` 内 `--external-guest-dir`、单个 `--obb`；
保留已有 Profile 特殊 mount、required 与 manifest 校验，不扩展 GUI 导入和多 OBB 输入。

验收：

- [x] 无 Profile external 默认挂到 `/sdcard`，显式 guest 根可选；Profile 路径优先。
- [x] OBB 原文件在 `/sdcard/Android/obb/<package>/<filename>` 可定位读取，不整包复制。
- [x] 缺文件、非法 OBB 文件名和挂载冲突明确失败；原有 ZIP 条目 Profile 挂载可用。
- [x] Windows Release 定向测试及 Dead Trigger APK/OBB `--preflight` 越过挂载检查。

证据：`run-apk mounts*` 2 项/15 断言、`run-apk archive*` 2 项/14 断言通过；
真实 APK/OBB `--preflight --ephemeral-sandbox` 报 `profile=none` 且退出码 0。
同 APK 的显式 external guest 根预检通过；非法 OBB 文件名和缺文件均明确失败。
实际 `--exit-after-frames 1` 在 guest `Application.onCreate` 进入 Urban Airship 后，
首错为 `Resources.getAssets()` 未解析；这是下一项 DexVM 缺口，不是本工作单的挂载失败。
