# BND-36 · GLES 审计缺陷修复

目标：修复 API19 GLES 复核中经当前源码确认的 EGL 终止、固定管线和超采样行为缺陷。
依据本地 AOSP 4.4.4，依赖 [BND-35](BND-35.md)。

## 已落地

- `eglTerminate` 不再解绑调用线程或全局关闭 GLES 路由；所有线程仍 current 的资源延迟到
  后续合法解绑再回收。有效 Display 重复终止成功，并可重新 initialize。
- Java EGL10/EGL14 在 terminate 后重新取得 native Display；destroy 成功立即退役 Java
  wrapper 映射，避免已销毁对象继续通过校验。
- GLES1 关闭 lighting 时不再计算无用 normal inverse，合法奇异 modelview 可继续绘制。
- fixed fragment shader 的 fog 只混合 RGB，保留 Alpha；线性反向区间保留分母符号。
- texture COMBINE 的 PRIMARY_COLOR 使用当前正/背面选择结果；二维纹理保留四维坐标并
  使用投影采样。
- `glDrawArrays` 及 flat triangle 展开直接使用无索引绘制，不再引入 `GLushort` 上限。
- 超采样只缩放默认 framebuffer 的 viewport/scissor，用户 FBO 保持 guest 像素尺寸。

## 验证

- 构建：`cmake --build --preset windows-msvc --target ogplay_tests -j 6`。
- 定向：`*supersample*,*GLES1*fixed*,*GLES1*texture*,*BND36*,*EGL*` 加 GLES1 core
  实际绘制用例，共 49/49 tests、2501/2501 assertions 通过。
- 未运行全量测试；未执行 CTS/Khronos 一致性套件。

## 后续

Java GLSurfaceView/GLU/复杂 GLES descriptor、光源设置时变换、线图元 flat shading、
GLUtils 格式、Java EGL error/extension string 以及 ES3 WRITE-only mapping 仍需独立工作单。
