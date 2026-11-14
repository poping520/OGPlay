# 任务：Android Web 启动器

目标：Android 主进程用系统 WebView 承载 `webui/gui`，复用 `GuiRpcService`，在真机上导入、设置并启动游戏；
手机竖屏触屏布局；平板布局以后再定（暂用现有游戏机布局）。
依赖：[frontend/gui](../../src/frontend/gui/MODULE.md)、[frontend/android](../../src/frontend/android/MODULE.md)、
[android](../../android/MODULE.md)、[webui/gui](../../webui/apps/gui/MODULE.md)。
范围：游戏库、导入（单体 APK 经 SAF 复制；数据包目录第二阶段）、全局与每游戏设置、内置 Demo、退出回报、开源致谢。
不含：按键设置、Dashboard、MCP、预检/诊断启动、打开目录、拖放上传、桌面窗口类设置、手填宿主路径、开发者诊断设置。

## 已定决定（用户确认）

- Android 不是桌面启动器移植：宿主以一份 `GuiPlatform` 声明平台功能，Android 关闭上述能力；
  被裁的 RPC 方法按未知方法拒绝，被裁设置不出现在 fields/values 且保存时拒绝，前端按 `host.features` 渲染。
- MCP/Dashboard 运行时代码仍编入 ogplaycore，Android 没有入口；体积确有需要再加 CMake 开关。
- `angle_backend` 在 Android 裁掉（Android ANGLE 预编译 `angle_enable_swiftshader=false`）；`cpu_*` 设备事实保留。
- 画面尺寸跟随设备：运行时尚未支持，第一阶段保留 `screen_width/height`，作为后续缺口。
- 布局（2026-11-14 修订）：现有横屏 UI 沿用电脑/手柄操作习惯，不适合手机。手机单独做一套竖屏触屏 UI（风格一致，
  面向 19:9～20:9），PC 不用竖屏 UI；平板 UI 以后再说。按最小宽度 600dp 区分手机。
- 不新增 Android 依赖（不用 androidx.webkit），只用平台 API。

## 阶段

1. 宿主功能集：`GuiPlatform`、设置/RPC/启动计划裁剪、`host.features`；桌面行为不变（GUI RPC 测试）。
2. 平台无关装配：从 `webview_shell.cpp` 拆出公共回调，桌面与 Android 共用。
3. Android 原生与外壳：`libogplay_launcher.so`（JNI）、`LauncherActivity` WebView 与 RPC 桥、SAF 导入、
   `GameActivity` 接收完整启动参数并回报退出码。
4. webui：Android 平台制品（独立输出目录，不覆盖桌面制品）、按功能事实隐藏入口、返回键/触屏基础适配。
5. 手机竖屏触屏布局（`webui/apps/gui/phone/`，共用 `useLauncher`）。
6. 数据包目录导入（SAF 目录树复制）。

## 当前状态与下一步

阶段 1–4 完成：PJZ110（root）上横屏游戏库、SAF 导入 APK（含同包新实例）、设置页裁剪、Demo 启动、
杀 `:game` 后退出回报（退出码 1、“进程意外终止”）、返回键（对话框 → 视图 → 退出）均已实测。
- Demo 标题界面不响应返回键，正常退出路径仍未验（既有 AND-1 缺口）。
- 阶段 6 主体完成：导入向导经 SAF 选数据包目录并复制入库已实测（同时修了 Android 上 FICLONE 被 SELinux 拒绝不回退的问题）。
  选取已改为直读 SAF（IMPORT-ARCHIVE）：不再先复制到 cache，导入时一次写入并有进度，向导显示原路径。
  剩余：运行后“需要数据包”提示与游戏设置里无法补选数据包（Android 不允许引用目录），需要“为已有游戏复制数据包”的入口。
- 阶段 5 首版完成：PJZ110（360×792 CSS px）上游戏库、游戏页、操作面板、全局/游戏设置两级页、选项面板、导入向导、
  两种皮肤与返回键已截图核对；待用户手机实测反馈。平板仍用游戏机布局。

下一步：按用户实测反馈调整手机布局，再补阶段 6 剩余项。

## 验收边界

- 桌面：`ctest -R "^frontend.gui_"`（及新增功能集用例）通过。
- Android：`./gradlew assembleDebug`、`zipalign -c -P 16`；真机（root，push 到 `/data/local/tmp` 后 `pm install`）
  从游戏库启动 Demo、退出回到游戏库并显示本次运行结果。
- 未验证：外接键盘/手柄导航、多窗口、后台被杀后的恢复。
完成后按事实归属迁移结论并删除本文件。
