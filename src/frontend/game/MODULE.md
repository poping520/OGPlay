# 子模块：frontend/game

统一游戏运行选项解析、校验、APK/宿主资源装配与执行；桌面游戏模式和 CLI 共用此实现。
`app::ParseGameRunArgs` 接受以 APK 路径开头的 UTF-8 参数，不依赖入口名称或读取 payload。
`app::FormatGameRunArgs` 与解析按选项值严格往返；公开选项不暴露 runtime 类型。
`app::RunGame` 在 app 层完成 SDL bootstrap/运行租约/日志装配，内部 `RunGame` 消费选项，按宿主 EXE 定位 payload 并编排 session；兼容行为仍由下层实现。
`--ui-language <zh-CN|en-US>` 只影响宿主界面；重复、非法或缺值明确失败，缺省使用系统语言。
游戏启动时固定解析结果供关闭确认框与 control.get 使用，不跟随启动器后续切换，也不影响 guest Locale。
用户关闭请求经 HAL 确认后才停止，确认框文案来自 app 文案表；取消恢复输入/音频推进。

VFS 装配、持久实例选择、MCP/诊断、Clock、输入与退出不变量沿用父模块契约。
未实现能力明确失败；不拥有启动器窗口，不包含平台 API 或游戏专属分支。
参数由 frontend.run_apk_options 和运行选项定向测试验证；真实 APK 按 playbook 验证。

Profile v4 只提供身份匹配；启动固定 API 19，surface 使用 GameRunOptions 用户值，
ABI/入口来自 APK，VM/guest call 使用默认值。OBB 原文件挂到标准包目录，cwd 为 `/`；
外部目录由显式选项指定，不做 Profile 挂载、清单预检或音频替代。

输入仅内置默认 + 用户文件；用户文件位置沿用 --input-config、库条目/input.toml 或
sandbox 旁的 <installation_id>.input.toml。缺文件为空布局，旧 template 字段拒绝。

`--missing-data-report <file>`（ADR-0146）：MissingDataRecorder 启动时删除旧报告，收集 `/sdcard`
子树缺失（去重、512 条上限并标记 truncated）与外部挂载内查找成功的路径（found，去重、2048 条上限），
会话结束或析构时剔除已存在的缺失并原子写 JSON（schema 2、end、external_dir、external_guest_dir、
missing、truncated、found）；写失败只记 warn。preflight 不记录。end 由 ClassifyRunEnd 在运行循环停止、
teardown 之前判定，用户关闭后 onDestroy 等退出错误不改变它：游戏自行退出时信号或非零码为 crashed、
无呈现帧为 no_frame、否则 exited；其余运行故障为 fault，无呈现帧为 no_frame，用户/宿主关闭为 closed；
未写即析构为 startup_failed。

`--window-title <name>`：窗口标题为 `OGPlay · <name> · FPS`，缺省用 APK 文件名；限 1～256 字节可打印 UTF-8。
库启动由 ResolveLaunch 传入游戏库显示名称。
`--window-icon <png> [--window-icon-style badge|cradle]`（GUI-17）：建窗前以 `ComposeRunningIcons` 合成
512/256/64/48/40/32/24/20/16 px 一组图标（小尺寸覆盖 Windows 100%～200% DPI 的图标度量），然后隐藏建窗并 `SetIcon`；读图、合成或设置失败只记 warn，保留默认图标；设置成功或失败后再显示，手动步进模式保持隐藏。样式缺省 badge，单独给样式参数明确失败。
库启动（有 installation id 与图标）在建窗前设置 `OGPlay.Game.<id>` 任务栏身份，失败只记 warn。

ValidateExternalGuestDirectory 为 CLI、导入与游戏设置提供统一 /sdcard 子树约束；
初始 cwd 由 VFS 建立为 /，之后仅 guest chdir/fchdir 改变，不由挂载配置隐式改变。

显式诊断环境变量 `OGPLAY_FRAME_BENCHMARK=1` 在 Activity 会话结束输出
`frontend.frame_timing`：最后至多 4096 个呈现帧的 active wall-time 中位数。统一 Clock
计时覆盖 step、离线音频混合与 present，累计无呈现的有效 step；不含 MCP 等待、截图、
启动与循环外 idle sleep。关闭时不读额外时间源，不输出逐帧明细。


ADR-0162 / DVM-306：AssembleAndroidContext 在 catalog 注册/VM 建立前读取同一份 BootDex
归档，校验固定 java-traversal v1 模式及 DEX/成员表/主题投影 SHA-256 与主题 schema，
再选择不可变 context owner；未知模式、错配、缺投影或无标识实验制品明确失败，无自动
Legacy fallback。无标识生产制品仍 Legacy。frontend 三入口共用此装配，显式注入不可变
平台无 accessibility 服务事实，生产不设置测试 bool；测试覆盖入口保留。
本阶段不开放 test-root、不改变 StepFrame；模式选择不等于生产遍历/E2E 验收。

启动建窗前只读取入口 Activity 的 screenOrientation（alias 用继承的入口配置）：
portrait/sensorPortrait/reversePortrait/userPortrait 仅对继承全局或内置尺寸交换宽高，已有竖向或正方形尺寸不变。
命令行任一 --surface-width/--surface-height 或游戏级任一尺寸显式设置均固定整组尺寸。
GameRunOptions.surface_size_explicit 保存来源，--inherited-surface-width/height 用于跨进程参数往返，
不将继承值升级为显式设置；窗口、DisplayMetrics、Surface 和触摸映射共用交换后的同一尺寸。
不处理 setRequestedOrientation 的运行时旋转或后续 Activity 方向切换。
