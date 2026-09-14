# BND-35 · API19 GLES 完整性反例修复

目标：闭合复核报告中可在兼容层范围内修复的 ES3 共用入口、Java GLES30 桥接与 GLES1
OES framebuffer object ABI 缺口。依据
[完整性复核](../../design/boundary/06-gles-api19-completeness-review.md)，依赖 BND-34。

## 已落地

- ES3 Context 的 `glVertexAttribPointer` 接受 HALF_FLOAT、INT/UNSIGNED_INT 与两种 packed
  2_10_10_10 类型；packed 类型限定 size=4，ES2 白名单不扩张。
- Java GLES30 专门桥接 indexed String、mapped direct Buffer、sync long 与 String[]；
  GLES20 string 查询接受 SHADING_LANGUAGE_VERSION。
- GLES1 扩展目录新增 GL_OES_framebuffer_object 全部 15 个入口，发布独立 ELF thunk，
  复用 ANGLE framebuffer/renderbuffer 与唯一 share-group 元数据，并同步发布扩展字符串。
- EGL 配置集合、Android native window/BufferQueue 与未选扩展仍受 ADR-0063 范围约束，
  本任务不引入 Android 系统对象，也不把子集实现宣称为完整设备 GLES。

## 验证

- 构建：`cmake --build build/dev --target ogplay_tests -j 6`。
- 定向：GLES1 扩展 ABI/对象生命周期、BND34 ES3 集合、DVM-83 Java GLES surface。
- 门禁：`tools.gles1_extensions_catalog_current`。

状态：实现完成，定向验证见本次工作记录。
