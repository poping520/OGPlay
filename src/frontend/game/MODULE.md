# 子模块：frontend/game

统一游戏运行选项解析、校验、APK/宿主资源装配与执行；桌面游戏模式和 CLI 共用此实现。
`app::ParseGameRunArgs` 接受以 APK 路径开头的 UTF-8 参数，不依赖入口名称或读取 payload。
`app::FormatGameRunArgs` 与解析按选项值严格往返；公开选项不暴露 runtime 类型。
`app::RunGame` 在 app 层完成 SDL bootstrap/运行租约/日志装配，内部 `RunGame` 消费选项，按宿主 EXE 定位 payload 并编排 session；兼容行为仍由下层实现。
VFS 装配、持久实例选择、MCP/诊断、Clock、输入与退出不变量沿用父模块契约。
未实现能力明确失败；不拥有启动器窗口，不包含平台 API 或游戏专属分支。
参数由 frontend.run_apk_options 和运行选项定向测试验证；真实 APK 按 playbook 验证。

Profile v4 只提供身份匹配后的 quirk；启动固定 API 19，surface 使用 GameRunOptions 用户值，
ABI/入口来自 APK，VM/guest call 使用默认值。OBB 原文件挂到标准包目录，cwd 为 `/`；
外部目录由显式选项指定，不做 Profile 挂载、清单预检或音频替代。

输入仅内置默认 + 用户文件；用户文件位置沿用 --input-config、库条目/input.toml 或
sandbox 旁的 <installation_id>.input.toml。缺文件为空布局，旧 template 字段拒绝。

`--missing-data-report <file>`（ADR-0146）：MissingDataRecorder 启动时删除旧报告，收集 `/sdcard`
子树缺失（去重、512 条上限并标记 truncated），会话结束或析构时剔除已存在路径并原子写 JSON
（schema 1、external_dir、external_guest_dir、missing、truncated）；写失败只记 warn。preflight 不记录。

`--window-title <name>`：窗口标题为 `OGPlay · <name> · FPS`，缺省用 APK 文件名；限 1～256 字节可打印 UTF-8。
库启动由 ResolveLaunch 传入游戏库显示名称。

ValidateExternalGuestDirectory 为 CLI、导入与游戏设置提供统一 /sdcard 子树约束；
初始 cwd 由 VFS 建立为 /，之后仅 guest chdir/fchdir 改变，不由挂载配置隐式改变。

启动读取 bundled data/dexvm/api19-skeleton.bin 原始索引数据，随 AndroidAppProcessRequest 下传；前端不解析/生成类，不决定平台行为。

显式诊断环境变量 `OGPLAY_FRAME_BENCHMARK=1` 在 Activity 会话结束输出
`frontend.frame_timing`：最后至多 4096 个呈现帧的 active wall-time 中位数。统一 Clock
计时覆盖 step、离线音频混合与 present，累计无呈现的有效 step；不含 MCP 等待、截图、
启动与循环外 idle sleep。关闭时不读额外时间源，不输出逐帧明细。
