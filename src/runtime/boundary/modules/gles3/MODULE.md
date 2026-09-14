# 子模块：runtime/boundary/modules/gles3

## 职责

- 承载 API 19 GLES3 相对 GLES2 的 104 项新增 core handler；导出仍属于
  `libGLESv2.so`，只有 current EGL Context client version 为 3 时允许执行。
- 标量调用通过 `AngleFrame` 进入真实 ANGLE；guest 指针在 ANGLE 调用前完整预检，输出
  成功后一次提交。64 位参数按 A32 AAPCS 偶数字槽对齐，sync/map 返回值不得暴露 host 指针。
- 复用 EGL registry 当前 Context、`GuestGlContext` error 和执行锁，不拥有第二套 Context、
  Surface、对象表或 Java 状态。

## 当前边界

104/104 项均已进入真实 ANGLE。普通/压缩 3D texture 按 ES3 unpack 状态计算 guest 范围；
多指针 query 先预检全部输出再提交；transform-feedback 与 uniform 名称数组逐项受界读取；
map-buffer 通过 `0x78000000` guest arena 隔离 host 指针，并在 flush/unmap 时回写。
