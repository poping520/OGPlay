# 任务：压缩包导入与 Android 免复制选取

目标：PC 与 Android 都能直接导入游戏压缩包（ZIP/RAR，内含 APK 与数据包），自动认出 APK 和数据包及挂载位置；
Android 选 APK、压缩包或数据包目录时不再先整份复制到 cache，数据只在导入时一次写进游戏库（有进度、可取消）。
依赖：[frontend/library](../../src/frontend/library/MODULE.md)（导入、数据包复制与挂载）、[frontend/gui](../../src/frontend/gui/MODULE.md)
（导入 RPC）、[webui/gui](../../webui/apps/gui/MODULE.md)（导入向导）、[android](../../android/MODULE.md)（SAF）、[hal](../../src/hal/MODULE.md)。
样例：`.local/games/1`（ZIP：Android/obb 结构、包名目录、拇指玩 GPK 反斜杠路径、多 APK；RAR4：Android/data 结构、包名/files）。

## 已定决定（用户确认）

- RAR 用 libarchive（BSD-2，third_party 子模块按源码编入，只用其 RAR 读取）；UnRAR 许可与 AGPL 不兼容，不用。
  ZIP 用项目自有读取（loader 的 Deflate），补 ZIP64 与非 UTF-8（GBK）文件名。
- Android 直接读 SAF：文件以本进程持有的 fd（`/proc/self/fd/N`）交给原生层；目录以句柄列出文件、导入时逐个打开复制。
- 认不出用途的顶层目录/文件默认不导入，向导列出可勾选加入数据包（挂到 `/sdcard/<名称>`）。

## 设计

- app 层 `ImportSource`（文件列表 + 按目标路径解出）：ZIP、RAR、Android SAF 目录三种实现；类型按文件头判断
  （含根 AndroidManifest.xml 的 ZIP 是 APK），不看扩展名。
- `PlanArchive`：APK 候选（浅层、非 Mod 优先，可改选）；数据映射到 /sdcard：`Android/(obb|data|media)/…` 原样，
  `sdcard/…` 去前缀，包名目录含 `main|patch.*.obb` → `Android/obb/<包名>`、否则 → `Android/data/<包名>`，根部扩展文件 →
  `Android/obb/<包名>`，`\` 视为目录分隔；说明/截图/图标/清单等杂项忽略。挂载位置取所选内容的最近公共目录，
  深于 `Android/(obb|data)/<包名>` 时取该层；勾选了其他内容则为 `/sdcard`。
- 导入时把选中文件解到导入任务目录（库根下）再整体移入条目 `data/`，不再二次复制。

## 阶段

1. GBK 解码（构建时生成码表）；ZIP 读取（ZIP64、编码、防越界路径）；PlanArchive；解出与入库移动；CLI `library import` 接压缩包；单测。
2. libarchive 子模块与 RAR 来源；桌面 CMake 与 Android NDK 构建。
3. RPC（analyze 识别压缩包、可改选 APK、导入时解出并报进度）与向导界面。
4. Android：文件/目录选取改为直接读，删去 cache 复制；目录句柄的列出、预览与导入。
5. 样例实测（macOS CLI/GUI、PJZ110），文档。

## 当前状态与下一步

阶段 1–4 完成，阶段 5 大部分完成：
- macOS CLI（Release）实测 ZIP（Android/obb、GPK 反斜杠、GBK 文件名、多 APK）与 RAR（Android/data、包名/files、
  非 ASCII 名）的识别与导入，解出的文件与 unzip/bsdtar 逐字节一致；474 MB deflate 约 5 秒、1.1 GB RAR 约 10 秒。
  为此 loader 的 Deflate 改为批量输出、9 位查表解码（APK/资源回归通过）。
- PJZ110 实测：选 ZIP/RAR/大 APK 不再等待复制；ZIP、RAR、SAF 目录导入成功，数据包与挂载位置正确。
- 待验：桌面 GUI 向导（用户自测）；Windows（MSVC 下 libarchive、文件对话框过滤）未构建。

下一步：用户桌面实测反馈后收尾，结论迁入 MODULE 并删除本单。

## 验收边界

- 单测：压缩包规划与 ZIP 读取主题（合成 ZIP），`npm run check`。
- 实测：`.local/games/1` 中 ZIP/RAR 各取数个，经 CLI 与向导导入，数据包落在预期挂载位置；Android 真机导入
  压缩包与数据包目录不出现 cache 整份复制的等待。
- 未覆盖：加密/分卷压缩包（明确报错），7z。
完成后按事实归属迁移结论并删除本文件。
