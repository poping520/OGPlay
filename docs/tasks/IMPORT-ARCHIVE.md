# 任务：压缩包导入与 Android 免复制选取

目标：PC 与 Android 都能直接导入游戏压缩包（ZIP/RAR/7z，内含 APK 与数据包），自动认出 APK 和数据包及挂载位置；
Android 选 APK、压缩包或数据包目录时不再先整份复制到 cache，数据只在导入时一次写进游戏库（有进度、可取消）。
依赖：[frontend/library](../../src/frontend/library/MODULE.md)（导入、数据包复制与挂载）、[frontend/gui](../../src/frontend/gui/MODULE.md)
（导入 RPC）、[webui/gui](../../webui/apps/gui/MODULE.md)（导入向导）、[android](../../android/MODULE.md)（SAF）、[hal](../../src/hal/MODULE.md)。
样例：`.local/games/1`（ZIP：Android/obb 结构、包名目录、拇指玩 GPK 反斜杠路径、多 APK；RAR4：Android/data 结构、包名/files）。

## 已定决定（用户确认）

- 游戏 ZIP、RAR、7z 统一由 7-Zip 动态库解码，固定上游 26.06，发行库名为 `lib7z.dylib`、`lib7z.so`、`7z.dll`。ZIP/RAR4 用构建目录中的
  补丁副本暴露原始文件名及编码属性，上游子模块保留原样。APK 识别与读取保留自有实现。
- 用户接受 LGPL 与 RAR 解码附带的 unRAR 限制。OGPlay 依据 AGPL-3.0 第 7 条授予组合与分发附加许可，
  随包提供上游许可文本，并保留文件名补丁的修改说明。
- Android 直接读 SAF：文件以本进程持有的 fd（`/proc/self/fd/N`）交给原生层；目录以句柄列出文件、导入时逐个打开复制。
- 认不出用途的顶层目录/文件默认不导入，向导列出可勾选加入数据包（挂到 `/sdcard/<名称>`）。

## 设计

- app 层 `ImportSource`（文件列表 + 按目标路径解出）：游戏压缩包和 Android SAF 目录两种实现；类型按文件头判断
  （含根 AndroidManifest.xml 的 ZIP 是 APK），不看扩展名。
- `PlanArchive`：APK 候选（浅层、非 Mod 优先，可改选）；数据映射到 /sdcard：`Android/(obb|data|media)/…` 原样，
  `sdcard/…` 去前缀，包名目录含 `main|patch.*.obb` → `Android/obb/<包名>`、否则 → `Android/data/<包名>`，根部扩展文件 →
  `Android/obb/<包名>`，`\` 视为目录分隔；说明/截图/图标/清单等杂项忽略。挂载位置取所选内容的最近公共目录，
  深于 `Android/(obb|data)/<包名>` 时取该层；勾选了其他内容则为 `/sdcard`。
- 导入时把选中文件解到导入任务目录（库根下）再整体移入条目 `data/`，不再二次复制。

## 阶段

1. GBK 解码（构建时生成码表）；ZIP 读取（ZIP64、编码、防越界路径）；PlanArchive；解出与入库移动；CLI `library import` 接压缩包；单测。
2. 7-Zip 动态库与构建时文件名补丁；桌面与 Android 发行物。
3. RPC（analyze 识别压缩包、可改选 APK、导入时解出并报进度）与向导界面。
4. Android：文件/目录选取改为直接读，删去 cache 复制；目录句柄的列出、预览与导入。
5. 样例实测（macOS CLI/GUI、PJZ110），文档。

## 当前状态与下一步

- macOS Release CLI 使用 7-Zip 验过 18 个 ZIP/RAR 样本的检视基线；Wild Blood 的 RAR3 Itanium 数据、
  GBK 名、GPK 反斜杠路径和非 ASCII RAR 名均经导入与 `unar` 逐字节比对。solid 7z 由定向测试覆盖。
- Android arm64 APK 已验证动态库打包与 ELF 依赖。PJZ110 原导入链已实测直接选取 ZIP/RAR/SAF 目录，
  新解码库的真机路径待复验。
- 待验：用户桌面向导与 Android 真机、Windows MSVC 构建与文件对话框、RAR5 实样。

完成平台复验后迁入 MODULE 并删除本单。

## 验收边界

- 单测：压缩包规划、合成 ZIP、solid 7z 和加密拒绝，GUI RPC 压缩包用例与 Web UI import 测试。
- 实测：`.local/games/1` 中 ZIP/RAR 各取数个，经 CLI 与向导导入，数据包落在预期挂载位置；Android 真机导入
  压缩包与数据包目录不出现 cache 整份复制的等待。
- 未覆盖：加密/分卷压缩包（明确报错）。
完成后按事实归属迁移结论并删除本文件。
