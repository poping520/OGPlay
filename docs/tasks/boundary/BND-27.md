# BND-27 · GLES1 单 stage 纹理坐标数组解析

## 目标

当唯一启用的 fixed-pipeline 纹理 stage 与唯一有效的 client texture-coordinate array
不在同一 unit 时，通用地解析坐标来源，同时拒绝多 stage 的歧义复用。

## 根因与设计

- `active_texture` 属于同一 EGL context 的 `SharedGlState`，GLES1/GLES2 入口有意共享；
  任一 API 留下的高位 active unit 都可成为后续 GLES1 server-state 写入游标。
- draw 仍按实际启用的纹理 unit 选择 sampler、binding、texture matrix、base format 与
  environment；坐标数组来源单独解析，不能再假定两者 unit 相同。
- 每个 stage 优先使用自己的 coordinate array。仅当本次恰有一个 stage、该 stage 没有
  array、且所有 unit 中恰有一个有效 coordinate array 时，才使用这个唯一数组。
- 多 stage 缺少各自 array 时明确失败，禁止把一个数组复制给多个 stage；没有任何数组时
  继续使用该 stage 自己的 current texture coordinate，保留 GLES1 常量坐标语义。
- `AndroidBoundaryOptions::allow_gles1_single_stage_texcoord_fallback` 默认开启；关闭后命中
  上述错位形状会明确失败，用作关闭即失败的回归门禁。

## 验收

- `GLES1 single-stage texture coordinate fallback is unique and optional`：覆盖 own-first、
  unit31→唯一 unit0 回退、多 stage 拒绝共享和策略关闭即失败。
- `GLES1 high sampled unit uses the unique unit-zero coordinate array`：GLES2 留下
  `GL_TEXTURE31`，GLES1 在该 unit 采样、只启用 unit0 coordinate array，真实 ANGLE
  readback 取得纹理红色像素；移除解析会退化为 `(0,0)` 黑色采样。
- 既有 `Android boundary publishes GLES1 core without silent handlers` 保持通过，其中
  双 stage 分别消费 unit0/unit1 坐标数组。
- Windows Debug/Release 全目标构建通过；GLES1 定向 3/3 通过。
- PVZ Release 无 survey 实跑取得 sequence=5976 的 800×480 presented PNG：标题画面、
  角色和 UI 纹理均正常显示，黑屏消失；随后继续运行，无 guest fault，人工停止。

状态：已完成，未提交。
