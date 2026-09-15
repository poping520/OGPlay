# BND-38 · GLES 审计剩余缺口闭合

目标：闭合 BND-36/BND-37 后仍确认存在的 API19 GLU、Java GLES/EGL/GLUtils、
GLSurfaceView 调度及 GLES1/GLES3 native 行为缺陷。依据本地 AOSP 4.4.4。

## 已落地

### GLU 与 Java GLES

- GLU 不再错误进入 GLES symbol 通用转发：实现 error string、ortho、perspective、look-at、
  project/unproject；数组 offset、W=0、奇异矩阵和零 viewport 均受检。
- Java GLES 的 info log、shader source、active attrib/uniform、transform-feedback varying、
  uniform-block name 与 `glGetUniformIndices(String[])` 使用专用返回/数组适配。

### GLSurfaceView、EGL 与 GLUtils

- `requestRender` 形成逐 View 请求；WHEN_DIRTY 只消费一次请求，continuous 每帧绘制；
  `queueEvent` 无需绘帧也会在持有 current Context 的 GL 线程执行。
- EGL10/EGL14 extension string 复用 native EGL 发布清单；Java wrapper 参数失败锁存到同一
  per-thread native EGL error，未知 Display/Context/Surface 不再被静默接受。
- Bitmap 创建保留真实 Config；GLUtils 支持 ALPHA_8、RGB_565、ARGB_4444、ARGB_8888 的
  API19 format/type 兼容组合及对应像素编码。

### GLES1 与 GLES3 native 行为

- `GL_POSITION` 在 `glLightfv/glLightxv` 设置时乘当前 modelview；`GL_SPOT_DIRECTION`
  使用当时 modelview 的 inverse-transpose 三阶矩阵。
- `GL_FLAT` 对 LINES、LINE_STRIP、LINE_LOOP 展开成独立 GL_LINES，并按每段末顶点复制
  color/normal；闭环段以首顶点作为 provoking vertex。
- ES3 WRITE-only 且无 invalidate 的 mapping 内部使用 READ|WRITE host access 预取旧内容，
  guest-visible access 保持 WRITE-only，局部更新不会破坏未修改字节。

## 验证

- 构建：`cmake --build --preset windows-msvc --target ogplay_tests -j 6`。
- 合并定向回归：23/23 tests、1229/1229 assertions 通过；覆盖真实 ANGLE shader 查询、
  GLU、WHEN_DIRTY、native EGL extension/error、Bitmap.Config、GLES1 fixed/flat、ES3 mapping
  及 BND-36 相关图形回归。
- `architecture.capabilities_monotonic`、`architecture.boundary_hot_path` 2/2 通过。
- API19 Java GLES AOSP 生成清单检查与 `git diff --check` 通过。
- 未运行全量测试或 CTS/Khronos 一致性套件。
