# WU-GLCTX-03 · 统一 framebuffer / raster / common capability

目标：把 FBO/RBO binding、viewport/scissor、clear state 与共有 capability 纳入统一
guest-visible context state。

验收：

- [x] FBO/RBO bind/delete 事务更新 shared binding。
- [x] GLES1/GLES2 viewport 与 scissor 经任一入口设置后可由另一入口查询。
- [x] clear color/depth/stencil 在 native 成功后提交 shared shadow。
- [x] blend/depth/stencil/cull/scissor 等共有 capability 双向可见。
- [x] GLCTX 阶段统一构建与测试；最终 `windows-msvc` full CTest 506/506。

非目标：fixed translator 临时 native mutation 的保护由 WU-GLCTX-04 完成。
