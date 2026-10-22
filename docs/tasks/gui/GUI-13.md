# GUI-13 · 运行时推断数据包挂载位置

状态：macOS 完成，2026-10-20；OGPlay.app 内实际点击与 Windows/Linux 未验。依赖：GUI-12、VFS-09。决策：[ADR-0146](../../adr/session.md#adr-0146)。

目标：用户把数据包挂错位置或忘了配置时，游戏退出后由启动器根据游戏真实找不到的文件给出
建议挂载位置，用户确认后修改并可直接重新启动。不分析 APK，不依赖 Profile。

## 范围

- runtime/vfs：只读查找 ENOENT 的观察者接口。
- frontend/game：`--missing-data-report`，收集 `/sdcard` 子树缺失、剔除已存在路径、退出写 JSON。
- library：ResolveLaunch 传报告路径；推断函数（挂载候选/需要数据包）；游戏设置 `data_mount_hint`。
- gui/webui：`library.data_hint` RPC；退出后弹窗，挂载并重新启动 / 仅修改 / 忽略。
- 导入向导：选目录时按宿主路径结构预填挂载位置（如 `…/Android/data/<包名>`），无标记时为 `/sdcard/<目录名>`，不分析 APK。

不做：游戏内实时提示、APK 静态分析、自动应用、obb 单文件挂载建议。

## 验证

构建 Release ogplay_app_tests、ogplay_tests、ogplay-cli、ogplay_bundle。ogplay_app_tests 全部
84 用例通过（新增 data_hint_tests：推断/阈值/当前挂载排除/needs_data/报告校验/RPC 建议与应用/
设置关闭）；ogplay_tests 中 ADR-0146 VFS 观察者与 MissingDataRecorder、run-apk 与运行参数往返通过；
webui check 48 用例；GUI 冒烟、run_apk_options、app_entrypoints 与能力账本检查通过。
同次定向过滤命中的 DVM-104/File second-batch/DVM-88 共 7 条断言失败，暂存改动后基线同样失败，与本单无关。

真实 Dungeon Hunter（隔离库 `.local/gui13-missing/lib`，1024×600，复制入库，CLI 按 ResolveLaunch 参数运行）：

1. 挂载 `/sdcard`（错误）运行 900 帧退出 0；报告 510 条缺失（truncated），`library data-hint` 给出唯一
   候选 `/sdcard/gameloft/games/dungeonhunter`，解释 510/510（hint1.json）。
2. 按建议 `settings set external_guest_dir` 后复跑 900 帧退出 0，缺失降为 4 条（字体/可选贴图探测，
   数据包内本无），提示变为 none（hint2.json）。
3. 未配置数据包的第二份运行 600 帧：needs_data，示例与共同目录指向 `…/dungeonhunter/data/3d`；
   以 `--external-dir` 选包后同算法给出相同候选（hint3*.json）。

Web UI 以模拟宿主（真实 hint JSON）检查：退出事件后弹窗，挂载并重新启动调用 apply 与 launch，
needs_data 选目录后切到建议视图并在 apply 中携带目录。

缺口：未在 OGPlay.app 中真实启动并退出游戏走完弹窗；needs_data 的共同目录受 512 条截断影响可能偏深；
游戏内实时提示、obb 单文件建议未做。
