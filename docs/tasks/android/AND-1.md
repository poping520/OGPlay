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

状态：构建与打包完成；**运行行为全部未验证，当前没有测试设备。**

验证（2026-11-07，macOS arm64，NDK r30，AGP 9.5.1，Gradle 9.6.0）：
- `cmake --preset android-arm64` 后 `ninja ogplay_android`（Release）通过。
- `android/gradlew assembleDebug` 通过；APK 约 52.5 MB，内含 7 个 `.so`（ogplay_android、
  ogplaycore、SDL3、EGL_angle、GLESv2_angle、ogplay_ffmpeg、c++_shared）与 35 个
  `assets/data` 文件；清单 minSdk 29、targetSdk 36，`GameActivity` 位于 `:game`。
- `zipalign -c -P 16 -v 4` 通过，7 个 `.so` 的 LOAD 对齐均为 0x4000。
- 桌面回归：`build/dev` 的 `ogplay_hal`、`ogplay_stage_data` 通过，数据仍放在
  `build/dev/data`；`hal/linux` 拆分后的两个文件仅经 NDK 目标做过语法检查，Linux 未实跑。

未验证（需要真机，按此顺序）：
1. App 内 RWX 匿名映射与 Dynarmic 运行 A32（探针）；4 GiB 保留区。
2. ANGLE Vulkan 创建显示。`egl_lifecycle` 目前按 Vulkan 请求 headless 显示模式，Android 上
   是否可用未知；APK 未带 SwiftShader，无软件回退。
3. 启动、解压数据、进入 Sweet Shop 并退出。
4. 返回键能否传到 guest；屏幕方向（当前未约束）。

已知缺口：
- 原生日志：`RunGame` 的 stderr 输出在 Android 上丢失，只有入口异常进 logcat。
- 切后台、Surface 重建、context-loss 未处理（`session/MODULE.md`）。
- 游戏库界面、APK/数据包导入、MCP（需 INTERNET 权限和 `adb forward`）未接入。
- `--cpu-cores` 用默认值 1，未按设备配置。
- 发行前：`THIRD_PARTY_NOTICES.md` 与 credits 需加入 APK 随带的 NDK `libc++_shared`，并配置
  release 签名与开发者验证。
