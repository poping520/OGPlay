# BND-37 · Java GLES 初始化入口修复

目标：补齐 API19 游戏常用 GLSurfaceView 配置入口，并让 `queueEvent` 在持有 current
Context 的渲染线程执行。依赖 [BND-36](BND-36.md)。

## 已落地

- 发布并校验 `setEGLContextClientVersion(int)`。
- 发布 boolean 与六整数 `setEGLConfigChooser`，保存逐 View 的确定配置请求。
- `queueEvent(Runnable)` 将对象保活并进入受锁 FIFO；lifecycle 在 renderer callback 前、
  current Context 所在线程排空队列。null、停止后的投递明确抛 Java 异常。
- View 退役清理版本与配置状态；GC root 扫描覆盖尚未执行的 GL 事件。

## 验证

- 构建：`cmake --build --preset windows-msvc --target ogplay_tests -j 6`。
- 定向：`*GLSurfaceView*,*BND37*`，3/3 tests、60/60 assertions 通过。
- 未运行全量测试。

## 后续

GLU、Java GLES 复杂 descriptor、requestRender/WHEN_DIRTY 帧调度仍需继续闭合。
