# 子模块：frontend/desktop

ogplay 桌面 EXE 的模式分派：默认 WebView GUI、--run-game、--version、--help。
只调用 app 公开外观和 frontend/gui 私有接口，Windows 宽字符入口由 hal/windows/desktop_entry 适配，macOS 使用标准 main。
macOS 产物为 OGPlay.app：运行时库在 Contents/Frameworks、payload 在 Contents/Resources/data，每次构建 ad-hoc 签名。
RunGame 自行执行 SDL 启动；GUI 配置和日志归桌面，不经过共享库 C main。不静态链接内核。
GUI 与自身游戏子进程独立；GUI 退出不终止游戏，产品运行不依赖 CLI 文件。
验证：frontend.app_entrypoints、frontend.gui_smoke；Windows 与 macOS 的真实库流程各自在本平台验收。
