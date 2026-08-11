# ADR-0019 · 桌面呈现管线与零拷贝方向

- 状态：Accepted
- 日期：2026-08-11

## 背景

当前每帧路径是 ANGLE pbuffer 渲染 → `glReadPixels` 全帧回读 CPU → 窗口上传显示。
WU-PERF-04..06 之后的稳定期采样（Dungeon Hunter，macOS Release）显示主线程约 50%
耗在 `glReadPixels`（ANGLE Metal `waitUntilCompleted` + `getBytes` CPU 拷贝），
呈现上传约 10%，guest 执行仅 15%。回读是结构性瓶颈：它强制 CPU/GPU 每帧串行，
且随超采样倍率平方放大。

同时，MCP `frame_capture`、golden SHA-256 断言与 Scenario 证据链都依赖 CPU 像素，
任何零拷贝方案必须保留机器可判定的回读路径。

## 决定

1. 桌面窗口呈现统一经 SDL_Renderer 流式纹理上传与 GPU 缩放合成（WU-PERF-06 已
   落地）；禁止回到 CPU surface blit。
2. 长期方向：ANGLE EGL window surface 直接渲染，普通帧不再经过 CPU；回读收敛为
   按需路径（MCP capture、golden 断言、`--exit-after-frames` 证据），保持机器可
   判定测试不变。该工作量为里程碑级，需解决 SDL3 窗口与 EGL surface 的生命周期
   绑定、超采样 blit 与 resize，单独立项排期。
3. 在 2 落地前允许的中间优化：PBO/fence 异步回读（一帧延迟换取解除每帧
   `waitUntilCompleted` 串行）、按 present 需要降频回读。任何中间优化不得改变
   capture/golden 语义。

## 后果

- 呈现语义（黑边、等比缩放、present 计数）由 hal 契约固定，与实现路径解耦。
- 回读成为显式的证据路径而不是呈现路径的一部分，为窗口 surface 迁移铺平接口。
- 在 2 未完成期间，回读仍是帧率上限的主要因素，性能基准应分别记录
  「含回读」与未来「直接呈现」两组数据。
