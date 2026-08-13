# GUI-1 · 主面板 shell 与双击入口

## 目标

以固定 Dear ImGui submodule 建立 SDL3 + ANGLE/GLES2 空库窗口，同时提供
`ogplay-gui` 双击入口和 `ogplay gui` 开发/CI 入口。

## 结果

- Dear ImGui 固定于 1.92.9b（`f1cc2ae15e53a861a874c3034aae6798fde194ab`）。
- `ogplay-gui` 使用 SDL main 包装；Windows 为 GUI subsystem，macOS 目标为 bundle。
- `ogplay gui` 严格接受一次 `--library-root` 与正整数 `--smoke-frames`。
- GUI 日志覆盖写入 `<library-root>/gui.log`；renderer 身份不是 ANGLE 时明确失败。

## 验收

Windows/MSVC `/W4 /WX` 构建 `ogplay` 与 `ogplay-gui`；`frontend.gui_options` 锁定
非法参数拒绝，`frontend.gui_smoke` 三帧通过，日志记录 RTX 4060 Ti D3D11 ANGLE、
OpenGL ES 3.0 与 `rendered_frames=3`。
