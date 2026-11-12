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
API19 framework payload 中的启动类路径归档和 `framework-res.apk` 以只读随机访问文件挂载到
`/system/framework/`。run-game 与 run-dex 共用挂载入口。

输入仅内置默认 + 用户文件；用户文件位置沿用 --input-config、库条目/input.toml 或
sandbox 旁的 <installation_id>.input.toml。缺文件为空布局，旧 template 字段拒绝。

`--missing-data-report <file>`（ADR-0146）：MissingDataRecorder 启动时删除旧报告，收集 `/sdcard`
子树缺失（去重、512 条上限并标记 truncated）与外部挂载内查找成功的路径（found，去重、2048 条上限），
会话结束或析构时剔除已存在的缺失并原子写 JSON（schema 2、end、external_dir、external_guest_dir、
missing、truncated、found）；写失败只记 warn。preflight 不记录。end 由 ClassifyRunEnd 在运行循环停止、
teardown 之前判定，用户关闭后 onDestroy 等退出错误不改变它：游戏自行退出时信号或非零码为 crashed、
无呈现帧为 no_frame、否则 exited；其余运行故障为 fault，无呈现帧为 no_frame，用户/宿主关闭为 closed；
未写即析构为 startup_failed。

`--run-report <file>`：非 preflight 时 RunGame 开局原子写未完成报告（started_at），结束写 finished、
同一 end（会话未到判定点即失败时，进入运行循环或已有 guest 退出按 ClassifyRunEnd，否则 startup_failed）、退出码
（异常退出为 null）、呈现帧、首个故障/启动失败文本与 closed/exited 后的 teardown 错误（exit_error）。
文本截断至 4 KiB 并保证 UTF-8；写失败只记 warn，不改变运行结果。

`--window-title <name>`：窗口标题为 `OGPlay · <name> · FPS`，缺省用 APK 文件名；限 1～256 字节可打印 UTF-8。
库启动由 ResolveLaunch 传入游戏库显示名称。
`--window-icon <png> [--window-icon-style badge|cradle]`：建窗前以 `ComposeRunningIcons` 合成
512/256/64/48/40/32/24/20/16 px 一组图标（小尺寸覆盖 Windows 100%～200% DPI 的图标度量），然后隐藏建窗并 `SetIcon`；读图、合成或设置失败只记 warn，保留默认图标；设置成功或失败后再显示，手动步进模式保持隐藏。样式缺省 badge，单独给样式参数明确失败。
库启动（有 installation id 与图标）在建窗前设置 `OGPlay.Game.<id>` 任务栏身份，失败只记 warn。

ValidateExternalGuestDirectory 为 CLI、导入与游戏设置提供统一 /sdcard 子树约束；
初始 cwd 由 VFS 建立为 /，之后仅 guest chdir/fchdir 改变，不由挂载配置隐式改变。

显式诊断环境变量 `OGPLAY_FRAME_BENCHMARK=1` 在 Activity 会话结束输出
`frontend.frame_timing`：最后至多 4096 个呈现帧的 active wall-time 中位数。统一 Clock
计时覆盖 step、离线音频混合与 present，累计无呈现的有效 step；不含 MCP 等待、截图、
启动与循环外 idle sleep。关闭时不读额外时间源，不输出逐帧明细。


`AssembleAndroidContext` 在建立 VM 前校验 API19 启动类路径。会话将 framework-res 以
只读方式挂载到 guest VFS。ROM AssetManager 从框架归档及当前应用 APK 建立资源视图。

启动建窗前只读取入口 Activity 的 screenOrientation（alias 用继承的入口配置）：
portrait/sensorPortrait/reversePortrait/userPortrait 仅对继承全局或内置尺寸交换宽高，已有竖向或正方形尺寸不变。
命令行任一 --surface-width/--surface-height 或游戏级任一尺寸显式设置均固定整组尺寸。
GameRunOptions.surface_size_explicit 保存来源，--inherited-surface-width/height 用于跨进程参数往返，
不将继承值升级为显式设置；窗口、DisplayMetrics、Surface 和触摸映射共用交换后的同一尺寸。
运行中请求仍仅向 SDL 设置方向提示；逻辑显示宽高及后续 Activity 配置变化由本地 AMS/WMS 更新，前端从呈现帧取得新尺寸并同步输入映射。

实时循环在开始计时和推进 step 前查询 session 的 ReadyForFrame；生产者未完成且未进入真实阻塞时，
复用已有空闲分支处理窗口/MCP 事件。查询不放行 swap，也不推进 guest 时间；手动步进仍以完整
step 为单位。帧时间的计算口径保持原样，空闲分支与原有 FPS 节流一样位于活跃帧计时之外。
