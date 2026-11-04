# 子模块：frontend/desktop

ogplay 桌面 EXE 的模式分派：默认 WebView GUI、--run-game、--version、--help。
只调用 app 公开外观和 frontend/gui 私有接口，Windows 宽字符入口由 hal/windows/desktop_entry 适配，macOS 使用标准 main。
macOS 产物为 OGPlay.app：运行时库在 Contents/Frameworks、payload 在 Contents/Resources/data，每次构建 ad-hoc 签名。
应用图标来自 assets/icons：Windows EXE 内嵌 ICO（CLI 同源），macOS 包在 Resources 携带 ICNS 并由 Info.plist 引用。
RunGame 自行执行 SDL 启动；GUI 配置和日志归桌面，不经过共享库 C main。不静态链接内核。
GUI 与自身游戏子进程独立；GUI 退出不终止游戏，产品运行不依赖 CLI 文件。
Windows 启动器在同一登录会话中单例（不按库根或 EXE 路径区分）；重复启动退出并请求恢复已有窗口，包含托盘隐藏/最小化状态。
实例锁在进入 GUI 配置、日志和库初始化前取得，退出/崩溃后由 OS 释放；`--run-game`、`--help`、`--version` 不受启动器锁限制。
验证：frontend.app_entrypoints、frontend.gui_smoke；Windows 与 macOS 的真实库流程各自在本平台验收。
