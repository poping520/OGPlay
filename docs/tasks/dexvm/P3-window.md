# P3 窗口与生命周期

阶段 A 提供进程内 AMS、WMS、输入分发和 IMMS，以及软件 Surface、图层合成与 NDK 输入队列。
阶段 B 将生产装配切换到这些服务。ROM ActivityThread、Activity、PhoneWindow 和 ViewRootImpl
处理生命周期、布局、焦点和软件绘制。会话推进 Clock，排队 vsync，取已 post 的图层并交给
FrameService。手动步进等待主消息队列空闲和当步软件绘制。窗口类型、坐标、透明区及窗口 alpha
由 WMS 合成；满足条件时 FrameService 直接持有全屏 EGL 图层的像素引用。

GLSurfaceView 和 NativeActivity 使用 ROM Surface 取得 ANGLE 窗口，视频经 ROM
MediaPlayer.setDisplay/setSurface 使用同一合成入口。SurfaceTexture 继续使用 SurfaceStream。
SDL 文本和 composing 进入 IMMS 的 IInputContext，物理按键仍经 ViewRootImpl 分发。Toast
经本地 INotificationManager 排队，以 Clock 计时并调用 ROM ITransientNotification。
Activity 入口方向和运行中请求的方向交给前端。关闭时先等 ROM 回报生命周期，若回调超过
Clock 截止时间则打断 guest 等待并释放资源。
本轮删除旧 Activity 调度、主线程过渡通道、JavaTraversal、UI 投影、宿主 modal、runtime/ui
和对应 BootDex 替换。WebView 继续提供离线加载失败回调，作为普通 View 附着到 ROM 窗口。

## 验证入口

- Release 的 `ogplay_tests` 用 `--source-file` 选择 `*rom_window_integration_tests.cpp`、
  `*rom_activity_service_tests.cpp`、`*rom_window_system_tests.cpp`、
  `*rom_window_services_tests.cpp`、`*native_activity_process_tests.cpp` 和
  `*android_native_activity_io_tests.cpp`。APK 夹具见 [tests/fixtures/ui](../../../tests/fixtures/ui/README.md)。
- 定向组、`dexvm dump-surface` 与逐项 scope 核对、20 款游戏的 900 帧报告及性能原始记录
  位于 `.local/analysis/p3-window/round2/stage-b/`。删除的测试及替代入口见同轮目录中的
  `test-coverage-map.tsv`。画面证据与场景限制见 `round2/ui-compare/notes.md`。

## 剩余缺口

运行期配置变化、窗口旋转后的重建、多显示、系统装饰与硬件加速不在本轮支持范围。
已核对第二轮 scope 的每一项。与第一轮快照的类差异还包括范围表外的 native 绑定、本地服务、
ROM 依赖和旧状态字段移除，逐类见 `surface-outside-scope.tsv`。游戏矩阵中的 Fruit Ninja
进入了定位订阅的缺口，交替三次复跑的呈现中位数从 108 降为 69。Wild Blood、PvZ 和 NFS17 缺少完整的双侧画面证据。
定向组仍有 DVM-84 和 DVM-98 两个既有失败，后者相对 P0 的具体失败点有变化。
第三轮处理运行期配置变化与 ActivityThread relaunch、剩余清理、macOS x64 构建和 P3 最终验收。
窗口与输入的职责见
[DexVM Android 契约](../../../src/runtime/integration/dexvm_android/MODULE.md)。
