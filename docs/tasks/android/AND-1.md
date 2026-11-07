# AND-1 · Android 工程框架与构建打通

目标：把 Android 作为新的宿主平台接入，建立 `android/` Gradle 工程，打出包含完整原生运行时
和运行数据的 APK。本任务不要求游戏已在设备上运行。
依赖：`third_party/angle-prebuilt`、`third_party/ffmpeg-prebuilt` 的 `android-arm64` 包。
契约：[android MODULE](../../../android/MODULE.md)、
[hal/android](../../../src/hal/android/MODULE.md)、
[frontend/android](../../../src/frontend/android/MODULE.md)。

范围：
- 决定：图形继续只用 ANGLE（Vulkan）；`minSdk` 29、`targetSdk`/`compileSdk` 36；只打
  `arm64-v8a`；一个 `:game` 进程运行一个游戏；不调用宿主 framework 服务运行 guest。
- 原生：`ThirdParty.cmake` 增加 Android 的 ANGLE/FFmpeg 分支；`hal/linux` 拆出
  `host_paths.cpp`，其余 POSIX 实现由 Android 复用；`ogplay_android` 导出 `SDL_main`；
  `OGPLAY_RUNTIME_DATA_DIR` 让运行数据放到 Gradle assets；固定 fmt 缺 `<cstdlib>` 由构建层补。
- Java：`LauncherActivity`（准备数据、启动内置 Sweet Shop）、`GameActivity`、`RuntimeData`。

状态：构建与打包完成；一加 7 Pro 上 Demo 已显示标题画面，触摸可进入选关界面。

验证（2026-11-07，macOS arm64，NDK r30，AGP 9.5.1，Gradle 9.6.0）：
- `cmake --preset android-arm64` 后 `ninja ogplay_android`（Release）通过。
- `android/gradlew assembleDebug` 通过；APK 约 52.5 MB，内含 7 个 `.so`（ogplay_android、
  ogplaycore、SDL3、EGL_angle、GLESv2_angle、ogplay_ffmpeg、c++_shared）与 35 个
  `assets/data` 文件；清单 minSdk 29、targetSdk 36，`GameActivity` 位于 `:game`。
- `zipalign -c -P 16 -v 4` 通过，7 个 `.so` 的 LOAD 对齐均为 0x4000。
- 桌面回归：`build/dev` 的 `ogplay_hal`、`ogplay_stage_data` 通过，数据仍放在
  `build/dev/data`；`hal/linux` 拆分后的两个文件仅经 NDK 目标做过语法检查，Linux 未实跑。
- 一加 7 Pro（Android 11）启动无 `data/profiles/` 的 Debug APK，原运行数据报错消失；
  ANGLE 报告 Vulkan 1.1.128 / Adreno 640，随后 `eglChooseConfig failed (EGL error 0x3000)`，游戏进程退出 0。
- 原因：该驱动下 ANGLE 只提供 ES 2.0，45 个配置的 `EGL_RENDERABLE_TYPE` 均为 0x05，开启
  `exposeNonConformantExtensionsAndVersions` 也创建不了 ES3 context（`EGL_BAD_MATCH`）；
  而显示级 pbuffer 配置一律要求 ES2|ES3。现优先选 ES2|ES3 配置，无则回退 ES2，RGB 配置沿用
  主配置的可渲染类型；ES3 context 在此类设备上于创建时明确失败。
- 修复后同路径复跑：原错消失，标题画面呈现，点击 Play 进入选关界面，`:game` 进程持续运行
  （CPU 约 120%、RSS 约 477 MB）。macOS dev 的 EGL/ANGLE 相关 64 个用例中 63 个通过；
  失败的 `DVM-197 renderer EGL policies…` 在改动前同样失败（12 个断言），与本次无关。
- 一加 13（Android 15，Adreno 830，Vulkan 1.3.284）：ANGLE 提供 ES 3.2，原 ES2|ES3 配置可选；
  Demo 运行正常（用户实测）。
- 屏幕方向：SDL 对可缩放且无方向提示的窗口选 FULL_USER，横屏游戏随手机竖放显示。`hal` 现于
  创建窗口前按宽高设置 `SDL_HINT_ORIENTATIONS`（仅 Android/iOS 后端读取）。一加 7 Pro 竖放启动
  Demo：请求方向 11（USER_LANDSCAPE），屏幕转为 3120×1440，标题画面横屏显示，点击 Play 进入选关。
- 全屏：Android 上窗口以 `SDL_WINDOW_FULLSCREEN` 创建，SDL 切入粘性沉浸式模式。一加 7 Pro 上
  窗口为整屏 3120×1440，状态栏与导航栏隐藏；点击 Play 进入选关；下滑调出的系统栏约 4 秒内自动收回。

未验证（需要真机，按此顺序）：
1. 退出游戏与再次启动；切后台后恢复。
2. 返回键能否传到 guest；竖屏游戏的方向（逻辑同上，未实测）。
3. 游戏画面比例接近或宽于屏幕时，挖孔区域是否遮挡内容（SDL 设为 cutout ALWAYS）。
3. 需要 ES3 的游戏：在只提供 ES2 的设备上会在创建 context 时失败。
4. APK 未带 SwiftShader，没有可用 Vulkan 的设备无软件回退。

已知缺口：
- 原生日志：`RunGame` 的 stderr 输出在 Android 上丢失，只有入口异常进 logcat。
- 切后台、Surface 重建、context-loss 未处理（`session/MODULE.md`）。
- 游戏库界面、APK/数据包导入、MCP（需 INTERNET 权限和 `adb forward`）未接入。
- `--cpu-cores` 用默认值 1，未按设备配置。
- 发行前：`THIRD_PARTY_NOTICES.md` 与 credits 需加入 APK 随带的 NDK `libc++_shared`，并配置
  release 签名与开发者验证。
