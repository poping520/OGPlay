# VFS-09 · 用户外部挂载与 guest 工作目录

状态：有界完成，2026-10-19。依赖：VFS-01/07/08、GUI 库设置、ADR-0142。
决策：[ADR-0143](../../adr/session.md#adr-0143)。

目标：删除 Profile 后，外部布局由用户数据表达，guest 自行改变 cwd 并经标准媒体读取。
不恢复 Profile runtime/data/audio，不加标题分支，不改变默认 800×480。

## 范围

- meta.toml 的 external_guest_dir 默认 /sdcard，settings.toml 同名项可覆盖；GUI 导入及
  实例设置均可编辑，ResolveLaunch 传给原 CLI 参数。拒绝 /sdcard 外路径、点段及非法字符。
- VFS 初始 cwd=/；路径变更逐目录校验，目录 fd/dup 共享打开事实，失败不变更 cwd；
  被删除 cwd/fd 不因同名重建而重新绑定。ARM chdir(12)/fchdir(133)/getcwd(183)
  返回真实 errno；getcwd 返回含 NUL 长度，AT_FDCWD 共用 cwd。
- AndroidAppProcess 与两种 native 请求使用同一 100 亿 tick 看门狗默认值。
  不改已有 observable-progress 续期、不新增每游戏配置。
- 挂载根、内容与 storage 别名原已共用 ASCII 大小写折叠；新增真实宿主目录 mixed-case
  打开/读取回归，视频源通过同一 VFS lease 读取。不依赖宿主大小写宽容，不新增媒体旁路。

## 验证

只构建 Release ogplay-cli、ogplay_bundle、ogplay_app_tests、ogplay_tests。
定向 syscall/VFS/sandbox/外部挂载/guest call/application startup 151 用例通过；
导入/RPC/设置继承/ResolveLaunch/CLI 参数/库 54 用例通过；初始 cwd 的
Java getAbsolutePath 5 断言通过。WebUI TypeScript 与 46 用例通过。
Profile/quirk 校验与 capabilities check 通过，最终日志放在证据目录。

真实 APK 数据目录直接包含 data/，两次均 profile=none、无 survey、全新持久沙盒：

1. CLI 使用 --external-dir .local/games/DungeonHunter
   --external-guest-dir /sdcard/gameloft/games/DungeonHunter --surface-width 1024
   --surface-height 600 --exit-after-frames 900，呈现 900 帧，退出 0。
2. 隔离库 GUI 的数据与沙盒页保存相同挂载点，显示页明确选 1024×600，实际子进程
   预览带正确参数；视频播放后恢复游戏，MCP 观测 successfulPresents=1036 后正常关闭，
   运行日志 presented=1085，GUI 正常回收子进程。未改原用户库数据。
3. 两条路径都没有 PyData ERROR LOADING；开场 Logo 影片实际截图显示多个不同视频帧，
   之后回到标题/游戏画面，并非只凭 prepared/onCompletion 判定。
4. 默认 800×480 另跑原命令，f=661 复现 pc=0x30885b08 读 0，退出 1；
   已独立登记 [DVM-251](../dexvm/DVM-251.md)，本单不修复或绕过。

证据：`.local/dh-generic-fix-20261019/`：cli1024/{command.json,run.log,states.jsonl,
frame-*.png,exit.txt}、gui-proof/{states.jsonl,frame-*.png}、gui/library/*/settings.toml
与 last-run.log、cli800/，以及 validation/ 中的构建与定向日志。
CLI frame-00565.png、GUI frame-00403.png 是影片画面；CLI frame-00645.png 是恢复后的标题。

边界：非 cwd 的旧 traversal 拒绝保持；任意 dirfd 相对 *at 仍 ENOTSUP，无 symlink/
目录子树 rename；不是完整 POSIX 文件系统。跨平台与正式 title gate 未验收。
