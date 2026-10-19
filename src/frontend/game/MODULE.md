# 子模块：frontend/game

统一游戏运行选项解析、校验、APK/宿主资源装配与执行；桌面游戏模式和 CLI 共用此实现。
`app::ParseGameRunArgs` 接受以 APK 路径开头的 UTF-8 参数，不依赖入口名称或读取 payload。
`app::FormatGameRunArgs` 与解析按选项值严格往返；公开选项不暴露 runtime 类型。
`app::RunGame` 在 app 层完成 SDL bootstrap/运行租约/日志装配，内部 `RunGame` 消费选项，按宿主 EXE 定位 payload 并编排 session；兼容行为仍由下层实现。
VFS 装配、持久实例选择、MCP/诊断、Clock、输入与退出不变量沿用父模块契约。
未实现能力明确失败；不拥有启动器窗口，不包含平台 API 或游戏专属分支。
参数由 frontend.run_apk_options 和运行选项定向测试验证；真实 APK 按 playbook 验证。

Profile v4 只提供身份匹配后的 quirk 与输入模板；启动固定 API 19、800×480，
ABI/入口来自 APK，VM/guest call 使用默认值。OBB 原文件挂到标准包目录，cwd 为 `/`；
外部目录由显式选项指定，不做 Profile 挂载、清单预检或音频替代。
