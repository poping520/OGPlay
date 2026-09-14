# BND-33 · EGL/GLES 修复报告执行

## 目标

按 `docs/design/boundary/04-egl-gles-repair-report.md` 的四个 WU 闭合 Native EGL、
GLES1 绘制、Java EGL 与 GLES3/选定扩展。

## 当前交付

- 已追加 [ADR-0061](../../adr/media.md#adr-0061)，以唯一 EGL registry、每 Context 状态和
  独立 Surface backing 替代旧的进程唯一 Context/Surface 设计。
- ANGLE lifecycle 已接受明确 client version 与 native share context，并公开仅供 registry
  使用的 native identity；真实 ANGLE 回归验证两个不同尺寸 pbuffer 的内容隔离及 texture
  share group 可见性。
- Native EGL config 正确区分 `EGL_NONE` 与数值零，补齐已支持 surface 的 EGL 1.4
  查询属性，并让 swap interval 只接受 config 宣告的 0..1。
- GLES1 normal matrix 使用 modelview 上三阶逆转置；`GL_NORMALIZE`、
  `GL_RESCALE_NORMAL` 在 fixed shader 中分别控制单位化与比例补偿；真实 ANGLE 像素
  回归验证关闭、normalize、统一缩放及 rescale 四种结果存在规范要求的差异。
- Native EGL registry 已为每个 Context/Surface 组合持有真实 ANGLE backing；不共享与
  share group、Context viewport/GL error、不同尺寸 pbuffer 内容、draw/read 分离、共享创建者
  销毁后的资源存活及 current 对象延迟销毁均有 guest 定向回归。
- `eglGetProcAddress` 返回独立稳定 thunk，查询不依赖 current Context，调用时按 guest thread
  的 ES1/ES2 current Context 转发；直接 ELF import 继续保留 SONAME 语义。
- 两个真实宿主线程可同时 current 不同 Context；同一 Context 抢占失败不破坏原绑定，释放后
  可接管。Terminate 对其他线程的 current 对象延迟回收，线程释放后可重新初始化 display。

## 验证

- `cmake --build --preset windows-msvc --target ogplay_tests`：通过。
- `ogplay_tests --test-case='*EGL*'`：26/26，623/623 断言通过。
- `ogplay_tests --test-case='*GLES1*'`：20/20，1624/1624 断言通过。
- `ogplay_tests --test-case='*GLES2*'`：13/13，737/737 断言通过。
- `ogplay_tests --test-case='EGL lifecycle*'`：7/7，64/64 断言通过。
- `ogplay_tests --test-case='ANGLE pbuffer contexts share resources but keep framebuffer content'`：
  1/1，36/36 断言通过。

## 后续 WU

- WU-2 的 flat shading 像素语义及新增数学/像素回归。
- WU-3 Java EGL10/EGL14 复用 Native registry。
- WU-4 GLES3 104 项、Java GLES30 与选定扩展。

WU-1 状态：完成。BND-33 总任务仍进行中。
