# BND-28 · GLES 原生错误回送 guest

## 目标与根因

ANGLE 返回的 GLES API error 必须遵循 guest `glGetError` 状态机，不得作为宿主 C++ 异常
终止游戏。PvZ 经 `libGLESv2.so` 调用
`glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_FALSE)`；0x8191 只属于 GLES1，
ANGLE 因此返回 GLES2 `GL_INVALID_ENUM`。原 `AngleFrame::RequireNoError` 把该错误转成普通
`runtime_error`，边界无法区分 guest API error 与宿主契约故障，导致整个调用链失败。

## 设计边界

- `AngleFrame` 将原生 GL error 转为携带精确 GLenum 的 `GlesApiError`。
- GLES1/GLES2 module 只捕获该类型并锁存到唯一 `SharedGlState`，调用正常返回。
- `glGetError` 优先返回并清除首个 guest 锁存错误，再查询 ANGLE；后续合法调用可继续。
- guest 内存错误、搬运错误、无当前 frame、逻辑错误等宿主契约故障仍明确失败。
- 不把 GLES1 的 `GL_GENERATE_MIPMAP` 扩展成 GLES2 能力，也不为 APK 增加特例。

## 验收

- [x] GLES2 非法 texture pname 返回调用方，随后 `glGetError` 得到一次
  `GL_INVALID_ENUM`，第二次为 `GL_NO_ERROR`。
- [x] 同一真实 ANGLE 流程在错误后继续 texture upload、mipmap 与 framebuffer 操作。
- [x] PvZ exact 命令越过 `glTexParameteri` 1280，下一首错为 `guest memory is unmapped`。

Windows Release `ogplay_tests/ogplay` 受影响目标构建通过；真实 ANGLE texture 定向测试
1 项、60 断言通过。未跑全量测试、游戏 gate 或跨平台验收。

状态：完成。
