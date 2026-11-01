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

## 修正：只对没跑起来的运行做推断（2026-11-01）

问题：游戏正常运行后退出也弹窗。DH（数据包挂载正确）在建模目录下按相对路径先探测纹理，失败后
回退到数据根读到，被误判为挂到 `…/data/3d/interface`；Lili、PVZ 无数据包，引擎探测
`/sdcard/UnrealEngine3`、`Android/data/<包>/files` 的可选文件，被误判为需要数据包。

改动：报告升为 schema 2，记录 `end`（运行循环停止、teardown 前判定：startup_failed / fault /
crashed / no_frame 为失败，closed / exited 为已跑起来），库层只推断失败运行；VFS 观察者同时报告
命中，报告记录外部挂载内找到的路径（found），候选可解释的缺失数不超过游戏已从当前挂载读到的
数据包文件数时不建议移动。旧 schema 1 报告视为不可读（无建议）。

验证：Release 构建 ogplay_tests/ogplay_app_tests/ogplay-cli/OGPlay.app；ADR-0146 用例 11/11，
data_hint/gui_launch/game_run_args 26/26。真实复跑（暂存沙盒，隔离库只链接用户 APK/数据）：
DH 1500 帧 end=closed → none；用户原 37 条缺失置为 fault 且无 found 时复现 `…/data/3d/interface` x16，
加上本次真实 found（2048）后 none。Lili 900 帧 end=closed（110 条缺失）→ none，置为 fault 时仍为
needs_data。PVZ 600 帧用户关闭后 onDestroy 失败、进程退出 1，end=closed → none。

扩展文件证据（同日）：Dead Trigger 1.1.0 不带数据包导入（隔离库）运行，第 2 帧空指针崩溃（end=fault），
只缺 Android/obb 下 main/patch 两个 .obb，原 ≥3 条门槛与单条浅层巧合规则使退出后和选目录后均无建议。
现缺失扩展文件（`main|patch.<版本>.<包名>.obb`）即为 needs_data，数据包内含该文件时单条匹配也成立：
真实报告给出 needs_data（`/sdcard/android/obb/com.madfingergames.deadtrigger`），选含 main obb 的目录后
remount 到该目录；按建议写入设置后复跑见下。

DH 黑屏特例（暂按已运行处理）：DH 不带数据包时进入 Gameloft 下载器 InstallerView，黑屏、有背景音乐、
不崩溃，持续提交黑帧（约 197 FPS，24247 帧后关闭），end=closed，510 条缺失不提示。画面空白判定
（如关闭前连续纯色 ≥5 秒）暂不做，遇到更多同类再定方案。

缺口：游戏能出画面但数据不全（如下载提示页）且被用户关闭时不再提示；DH 纹理探测只在实际游玩中出现，
本次无人值守复跑未触达，found 规则由用户原报告组合验证；OGPlay.app 内点击仍未实测。
