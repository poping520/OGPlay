# BND-31 · GLES 版本、扩展与参数错误契约修正

## 目标

修复 2026-09-14 GLES 审计报告 D/E 项：guest 只看到边界真实支持的版本/扩展能力，
负 draw/resource 参数通过 `glGetError` 返回规范的 `GL_INVALID_VALUE`。

## 交付

- GLES1 `GL_VERSION (0x1F02)` 固定返回 `OpenGL ES-CM 1.1`；
  `GL_RENDERER (0x1F01)` 继续返回真实 ANGLE renderer。
- GLES2 `GL_EXTENSIONS` 不透传 ANGLE 后端全集，只发布 ETC1、PVRTC 软件解码及
  已验证 RGBA8 renderbuffer/FBO 能力；字符串保留尾随空格。
- GLES1/GLES2 `glDrawArrays` 的负 first/count、`glDrawElements` 的负 count，以及
  已覆盖的负 GLsizei/imageSize/stride 路径锁存 `GL_INVALID_VALUE`；枚举错误仍为
  `GL_INVALID_ENUM`。Bounds wrapper 进入同一错误锁存，内存与生命周期错误仍抛出。

## 验证

- Windows `windows-msvc` Debug `ogplay_tests` 构建通过。
- `ogplay_tests --test-case='*GLES*'`：51/51 用例、2621/2621 断言通过。
- 未运行全量测试、游戏 gate 或跨平台测试。

状态：完成。
