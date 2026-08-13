# 子模块：frontend/gui

## 职责

提供独立的 SDL3 + ANGLE/GLES2 + Dear ImGui 主面板 shell，以及不依赖 ImGui 的游戏库
模型。GUI 只维护宿主游戏库并装配既有 CLI 子进程，不拥有 guest/runtime 行为。

## 公共 API

- `RunGuiCommand`：`ogplay gui` 的开发/CI 入口，只接受 `--library-root` 与
  `--smoke-frames`。
- `RunGuiStandalone`：双击 `ogplay-gui` 的零参数产品入口；失败通过图形消息框呈现。

## 不变量

- shell 使用 SDL3 的 OpenGL ES profile，强制 EGL/OpenGL ES driver，使 ImGui GLES2
  backend 运行在随程序交付的 ANGLE 上；不得回退出第二套桌面 GL renderer。
- GUI 进程日志覆盖写入 `<library-root>/gui.log`；CLI 入口同时保留同源 stderr sink。
- `--smoke-frames` 必须为正整数，完成指定成功 present 数后正常退出。
- 模型层不得 include ImGui/SDL，不触碰窗口或进程 API。

## 禁止

- 不实现 syscall/JNI/GLES/game-specific compatibility。
- 不在 GUI 进程内启动 guest session。
- 不解析自由文本日志来判断兼容性状态。

## 测试

`frontend.gui_smoke` 在有界三帧内验证真实 SDL3/ANGLE/ImGui 空库窗口。
