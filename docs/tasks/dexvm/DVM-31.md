# DVM-31 · 解释执行的 EGL10/GL10 façade

> 设计展开与分批执行计划：[08 · EGL/GL façade](../../design/dexvm/08-egl-facade.md)

## 目标（一句话）

让自带 `GLSurfaceView` 的 title 用解释执行的 `javax.microedition.khronos.egl`
面驱动**宿主已拥有的** ANGLE surface，而不是自己创建 EGL display/context。

## 依赖

- DVM-30（边界已固化）。

## 背景

Asphalt 6 打包了一份改名的 AOSP `GLSurfaceView`：`GLThread` 自己跑 EGL 建立
与 swap，宿主只提供 surface。现状在 `EGLContext.getEGL()` 明确失败
（DVM-30 三轮证据）。这不是 title 特判——`GLSurfaceView` 是这代游戏最常见
的形态，Dungeon Hunter 之后的 title 大概率同款。

## 范围（静态枚举，来自 `tools/dexdis.py`）

- `EGL10` 常量：`EGL_DEFAULT_DISPLAY`、`EGL_NO_CONTEXT`、`EGL_NO_DISPLAY`、
  `EGL_NO_SURFACE`。
- `EGL10` 方法 18 个：`eglGetDisplay`、`eglInitialize`、`eglTerminate`、
  `eglChooseConfig`、`eglGetConfigAttrib`、`eglCreateContext`、
  `eglDestroyContext`、`eglCreateWindowSurface`、`eglDestroySurface`、
  `eglMakeCurrent`、`eglSwapBuffers`、`eglGetCurrentDisplay`、
  `eglGetCurrentSurface`、`eglGetError`。
- `EGLContext.getEGL()` / `getGL()`；`GL10.glGetString`。

## 设计约束

- 复用 `runtime.host_managed_gl_surface` 的既有承诺：**guest EGL 不得替换或
  终止 host-owned surface**。这些入口是宿主 surface 的 façade，不是第二套
  EGL 实现：`eglCreateWindowSurface` 绑定既有 managed surface，
  `eglSwapBuffers` 走既有 present，`eglMakeCurrent` 在宿主 context 上是
  受检 no-op。
- `eglChooseConfig`/`eglGetConfigAttrib` 只回答宿主 surface 的真实属性
  （RGBA8 + D24S8，见 `gles.angle`），不编造配置列表。
- `GL10` 方法按需接入既有 GLES1 boundary；未接入的入口记账并明确失败。
- 渲染线程是 `GLThread`（真实宿主线程），present 必须与宿主帧循环协调：
  present 由谁触发、`dex_activity` 的 `onDrawFrame` 驱动是否与之互斥，需要
  在实现前定清楚——这是本 WU 的主要设计风险。

## 验收

- `asphalt6.bootstrap` 取得确定的首帧、无 guest fault、clean shutdown，
  三轮一致。
- 未实现的 EGL/GL 入口记账并明确失败，不返回中性值。
- Asphalt 5 exact 回归逐位持平。

## 2026-08-14 实施结果

- EGL/GL 的 10 个 Java handle、façade handler 与 swap pacer 已聚合到单一
  `javax_microedition_khronos_egl.cpp` 翻译单元；catalog 的 10 个声明入口和注册
  顺序保持不变。
- façade 实现已完成：AOSP `EGL10`/`GL10` 类形状、单一 host-owned surface
  状态机、受检 context currency 接力、managed present，以及未实现入口的记账
  与明确失败均有机器测试。
- swap 使用条件屏障：lifecycle driver 可运行时每宿主帧放行一次；driver 进入
  guest 阻塞原语时立即放行。`VmExecutionLock` 的通用 observer 不感知 EGL，
  N=2 monitor 握手与反向节拍用例均通过。
- A6 已越过原先 EGL 缺口且不再发生 swap/frame-driver 死锁，但在首帧前停于
  `Context.unregisterReceiver`。该入口不属于 EGL，本轮按范围不实现，因此 A6
  三轮首帧 gate **未完成**。
- A5 当前实现与改动前 `2c8243c` HEAD 对照运行的四个检查点 PNG 均逐位相同；
  主界面均为 `f91150b4…`。场景内历史期望 `9ee57323…` 已与当前基线漂移，故
  静态 scenario 仍判失败，不能宣称 exact gate 通过，但已排除本 WU 引入回归。
- Windows/x64 Release 配置、构建成功；全量 CTest 721/721 通过。

结论：DVM-31 的 EGL 开发范围已实现并有回归覆盖；任务单的端到端验收仍受范围外
缺口阻塞，状态保持未完全验收。
