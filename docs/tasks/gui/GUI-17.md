# GUI-17 · 运行中游戏图标

状态：macOS 已验（构建、单测、合成效果与 OGPlay.app 内 Dock 显示）；Windows 实现已写、编译与任务栏待验。依赖：GUI-3（图标提取）、GUI-5（库启动参数）。

目标：游戏运行时程序坞/任务栏不再显示与启动器相同的 OGPlay 图标，而是“游戏图标 + OGPlay 标记”，玩家能一眼分辨各个游戏进程。

## 范围

- 合成（frontend/library `ComposeRunningIcon`）：两种样式，默认 `badge`“圆形角标”（游戏图标铺满 macOS 圆角底板，右下角白色圆形 OGPlay 角标），
  可选 `cradle`“卡带插槽”（游戏图标作卡带插进 OGPlay 蓝色卡槽）。游戏图标不加底：裁掉透明边与软投影后，逐步放大（≤20%）
  直到圆角范围内没有透明像素，再按 824/1024 底板、185/1024 圆角裁剪。素材为随包数据 `data/branding/ogplay-cartridge.png`
  （与 webui logo 同图）；预乘浮点合成、Lanczos-3 重采样、三次盒式模糊投影。
- 设置：全局 `running_icon`（badge/cradle），下次启动生效；字段行正下方的预览卡片由宿主 `settings.icon_preview`
  用同一合成器和占位图标 `data/branding/preview-game.png` 生成，点击卡片与字段行分段控件编辑同一草稿值。
- 启动：ResolveLaunch 在条目有 icon.png 时传 `--window-icon <icon.png>`（非默认样式再传 `--window-icon-style`）；
  RunGame 打开窗口后一次合成 512～16px 共 9 个尺寸（游戏图标只裁剪一次），经 `hal::WindowInput::SetIcon` 设置；
  失败只记 warn 保留默认图标。
  库启动先 `hal::SetHostApplicationIdentity("OGPlay.Game.<id>")`，Windows 据此给游戏独立任务栏按钮（AppUserModelID）。

Windows（代码已写，本机无法编译，待用户在 Windows 上验证）：
- 标题栏/任务栏：SDL 只给一张 HICON 同时用作大小图标，会由系统把大图缩成 16/32 像素；`hal/windows/window_icon.cpp`
  改为按窗口 DPI 的 SM_CXSMICON/SM_CXICON 各选最接近且不小于的合成尺寸，`CreateIconIndirect` 生成 32 位 alpha 图标后
  WM_SETICON，并持有 HICON 到窗口销毁后释放。
- 任务栏分组：游戏进程建窗前 `SetCurrentProcessExplicitAppUserModelID("OGPlay.Game.<id>")`，与启动器分成独立按钮，
  按钮图标取窗口大图标；ogplay_hal 在 Windows 链接 shell32。
- 待验清单：MSVC 编译（`windows-msvc`）；启动游戏后任务栏出现独立按钮并显示合成图标；标题栏小图标清晰；
  100%/150%/200% 缩放下图标不发虚；Alt-Tab 图标；退出时是否有与 macOS 类似的闪回；固定到任务栏后的行为。

不做：固定在程序坞里的 OGPlay.app 图标（系统机制，不随运行进程变化）、每游戏单独样式、无图标条目的占位合成。

已知限制（macOS）：游戏退出时 Dock 用 .app 包内静态图标播放移除动画，瞬间闪回 OGPlay 图标。实验对比直接退出、
退出前切换激活策略为 Prohibited、改用 NSDockTile contentView，三者都会闪回；只有每游戏独立 .app（自带静态图标）
能消除，因签名/库路径风险与维护成本暂不做。

## 验证

- `ogplay_app_tests`：合成两种样式的几何与颜色、圆角图标与软投影裁剪后底板内无透明像素、坏图/全透明/越界尺寸拒绝；
  `--window-icon(-style)` 往返与“样式需图标”校验；ResolveLaunch 按 icon.png 与全局样式传参；`settings.icon_preview` 返回两张 PNG。
- 用库中 6 个真实游戏图标调用 C++ 合成，与 GUI 效果图 v2 一致。
- `npm run check`；浏览器模拟宿主核对两种皮肤下预览卡片与字段行联动。
- 待验：OGPlay.app 实际启动游戏后的 Dock 图标；Windows 编译与任务栏分组（本机无法编译）。
