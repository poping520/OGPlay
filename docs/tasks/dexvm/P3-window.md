# P3 窗口与生命周期

阶段 A 提供进程内 AMS、WMS、input 和 input_method，软件 Surface 缓冲、窗口合成、ROM 输入 native 及 NDK 队列桥接。服务与新 native 在宿主测试的进程装配中启用。生产使用第一轮的 Activity 和呈现路径。

阶段 A 的验收入口是 Release 的 `ogplay_tests`，用 `--source-file` 选择 `*rom_activity_service_tests.cpp,*rom_window_system_tests.cpp,*rom_window_services_tests.cpp,*android_native_activity_io_tests.cpp`。生产回归使用第一轮的定向测试分组、`dexvm dump-surface` 和相同 APK 与 payload 的 20 款单次 900 帧对照。

阶段 B 依赖阶段 A 的服务和 native。先接通 ROM ActivityThread 与 ViewRootImpl、FrameService、EGL/ANGLE、NativeActivity、视频和 SDL 输入，再撤除 MainLooperChannel、JavaTraversal、UI 投影及其专用替换。用真实 ROM UI APK 验证软件首帧与局部重绘、Dialog/PopupWindow/Toast、EditText、GLSurfaceView 和 NativeActivity。切换后完成生产 surface、20 款游戏、指定画面、性能与清理对照。运行期配置变化派发和 P3 最终验收归第三轮。

窗口与输入的职责见 [DexVM Android 契约](../../../src/runtime/integration/dexvm_android/MODULE.md)。
