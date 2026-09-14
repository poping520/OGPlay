# BND-33 · EGL/GLES 修复报告执行

## 目标

按 [EGL/GLES 边界修复报告](../../design/boundary/04-egl-gles-repair-report.md) 闭合
Native EGL、GLES1 绘制、Java EGL 及 API 19 GLES3 调用面。选定扩展清单由
[ADR-0062](../../adr/media.md#adr-0062) 冻结为空。

## 最终交付

### WU-1 · Native EGL

- [ADR-0061](../../adr/media.md#adr-0061) 定义唯一 EGL registry。每个 Context/Surface
  组合持有真实 ANGLE backing，Context 状态、Surface 内容和 share group 生命周期分离。
- EGL config、属性解析、pbuffer、draw/read surface、共享 Context、current 线程所有权、
  延迟销毁、terminate/reinitialize 及稳定 proc thunk 均按 guest thread 路由。
- 两个宿主线程可同时绑定不同 Context；同一 Context 不能被并发抢占，释放后可以接管。

### WU-2 · GLES1 绘制

- `GL_FLAT` 将 triangle/strip/fan 展开为独立三角形，并复制每个 primitive 最后顶点的
  provoking color/normal；arrays、client indices 和 buffer indices 共用该转换。
- normal matrix 使用 modelview 上三阶逆转置；`GL_NORMALIZE` 与 `GL_RESCALE_NORMAL`
  分别执行单位化和比例补偿。GLES1 Context 切换恢复 fixed state，GLES2 不受污染。

### WU-3 · Java EGL

- Java EGL10 与 API 19 EGL14 通过 managed 冷入口复用 Native registry；wrapper 只保存
  native handle identity，current、sticky error、延迟销毁和 teardown 只有一份事实。
- EGL10/EGL14 支持真实 pbuffer 和 shared context；EGL14 数组 overload 校验 offset 并只
  回写目标切片。pixmap、client buffer 与 texture pbuffer 以规范 EGL error 明确拒绝。
- Java/native 交叉调用在同一 guest thread 共享 current Context，并可通过 Java GLES 在
  pbuffer 上真实绘制和读回。

### WU-4 · GLES3 Native/Java

- 从本地 AOSP 4.4.4 `GLES3/gl3.h` 与 `GLES2/gl2.h` 精确生成 104 项 delta IDL；生成器保留
  指针方向、二级指针、`GLint64`、`GLuint64`、`GLsync` 及 A32 AAPCS 偶数字槽对齐。
- EGL config 宣告 ES3 bit，并创建真实 client-version 3 ANGLE Context；104/104 新增 core
  入口由 `libGLESv2.so` 发布，直接导入与 `eglGetProcAddress` 共用版本路由。
- 标量、对象名、word-array、字符串、64 位查询、program binary、sync、buffer offset、
  3D texture、多输出查询和 transform-feedback 名称数组均进入真实 ANGLE。所有 guest
  指针先完整预检，输出仅在调用成功后提交。
- `GLsync` 使用 guest handle；map-buffer 使用 `0x78000000` 有界 guest arena，并在
  flush/unmap 时同步写入，均不向 32 位 guest 暴露 host 指针。
- 普通 3D texture 按 ES3 unpack alignment、row length、image height 和 skip 状态计算受检
  搬运范围；shared texture state 支持 3D 与 2D array target。
- DexVM 从固定 AOSP 签名生成并发布 `android.opengl.GLES30` 常量和 overload surface，
  复用同一 Native GLES3 catalog 与 managed marshaller。

## 验证

- `cmake --build --preset windows-msvc --target ogplay_tests`：通过。
- WU-4 的 GLES1/GLES2/GLES3/EGL、catalog 与三项 architecture 定向集：66/66 通过。
- 真实 ES3 回归覆盖 VAO、64 位查询、indexed string、sync lifecycle、map-buffer round-trip
  与 3D texture upload。
- `git diff --check`：通过。

## 状态

WU-1、WU-2、WU-3、WU-4 均已完成，BND-33 完成。
