# RES-1 API19 ROM 资源接入

状态：完成（P2，macOS arm64 Release）。ROM 资源 native 使用裁剪版 `framework-res.apk` 和当前游戏 APK。Java 与 NDK 共用 AssetManager 和配置。清单、游戏库和运行时共用唯一 ARSC 解析器。UI 投影从 View 持有的 ROM 对象读取资源，发布现有绘制快照。

- 契约：[ARSC 表](../../../src/loader/resources/MODULE.md)、[资源核心](../../../src/runtime/resources/MODULE.md)、[ROM 接缝](../../../src/runtime/integration/dexvm_android/MODULE.md)。
- 验收入口：`ogplay_tests --test-case='resources core*'`、`ogplay_tests --source-file='*rom_skin_tests.cpp,*theme_resources_tests.cpp,*golden_frame_tests.cpp,*ui_renderer_tests.cpp'`、`ogplay-cli dexvm dump-surface`。制品校验使用 `tools/validate_android_payload.py --root data/android/19` 与 `tools/bootdex/build_bootdex.py check`。20 款 900 帧对照、native 清单和内存分解见本地 `.local/analysis/p2-resources/round3/report.md`。
- 剩余：overlay/idmap 与 ObbScanner 范围外。Android NDK r25c 缺失，guest JNI 沿用既有制品。macOS x64 已交叉链接，Windows 发布规则已检查，实机待验。P3 接管运行期配置事件、窗口与生命周期并删除 UI 投影。
