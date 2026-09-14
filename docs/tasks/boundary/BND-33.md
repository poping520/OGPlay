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
- GLES1 `GL_FLAT` 在 CPU 顶点准备阶段把 triangle/strip/fan 展开为独立三角形，并按每个
  primitive 的最后顶点复制 provoking color/normal；`DrawArrays`、client-index 与 buffer-index
  `DrawElements` 均走同一转换。`GL_SMOOTH` 保持原生插值路径。
- 非均匀缩放下的斜法线结果已用独立逆转置数学参考校验；normalize/rescale 开关与
  GLES1 Context 切换恢复均有真实 ANGLE 回归，GLES2 Context 不受 GLES1 fixed state 污染。
- Java EGL10 与 API19 EGL14 已通过 managed EGL 冷入口复用 Native EGL registry；wrapper
  只保存 native handle 映射，current、sticky error、延迟销毁及 teardown 不再另立事实。
- EGL10/EGL14 均支持真实 pbuffer 与 shared context；EGL14 的数组 overload 校验 offset
  并只回写目标切片。pixmap、client buffer 与 texture pbuffer 继续由 Native EGL 以规范
  error 明确拒绝。
- Java/native 交叉回归在同一 guest thread 观察同一 current context，并通过 Java GLES
  clear/readback 验证 pbuffer 真实绘制；managed window lifecycle 存在时 pbuffer backing
  仍保持独立。

## 验证

- `cmake --build --preset windows-msvc --target ogplay_tests`：通过。
- `ogplay_tests --test-case='*EGL*'`：26/26，623/623 断言通过。
- `ogplay_tests --test-case='*GLES1*'`：20/20，1624/1624 断言通过。
- `ogplay_tests --test-case='*GLES2*'`：13/13，737/737 断言通过。
- `ogplay_tests --test-case='EGL lifecycle*'`：7/7，64/64 断言通过。
- `ogplay_tests --test-case='ANGLE pbuffer contexts share resources but keep framebuffer content'`：
  1/1，36/36 断言通过。
- `ctest -R "GLES1|GLES2|EGL"`：60/60 通过。
- `ctest -R "WU-3|Java EGL bridge|EGL facade"`：7/7 通过。

## 后续 WU

- WU-4 GLES3 104 项、Java GLES30 与选定扩展。

WU-1、WU-2、WU-3 状态：完成。BND-33 总任务仍进行中。
