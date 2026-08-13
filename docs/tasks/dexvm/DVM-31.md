# DVM-31 · 解释执行的 EGL10/GL10 façade

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
