# 子模块：frontend/app

共享应用 target `ogplaycore` 的装配边界；Windows 产物 `ogplaycore.dll`。
只导出 `ogplay_desktop_main` 和 `ogplay_cli_main` 两个同构建 C ABI 阻塞入口，参数为借用的
UTF-8 argc/argv，不跨边界传递 C++ 对象、异常或内存释放责任。EXE 与库同版本发布。
桌面入口默认 GUI；`--run-game` 和 CLI `run-apk` 共用 game 的解析/运行服务。
SDL bootstrap 在本层每次入口调用时执行，所有异常在 SDL C 回调及导出边界前收敛；
不在 DLL 加载期间初始化窗口、线程或会话。一个进程仅调用一次应用入口。
CLI 与游戏模式不创建 WebView，GUI/游戏保持独立进程。CLI 查询使用 bundled 能力账本。
`desktop_main` 是 EXE 专用薄适配，不编入 DLL；EXE 只适配 OS 入口和 UTF-8 参数，不静态链接 OGPlay 内核或 SDL。
验证：frontend.app_entrypoints、frontend.run_apk_options、GUI 冒烟及真实 APK 有界复跑。
