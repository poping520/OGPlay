# WU-GLCTX-01 · 建立统一 Context 容器

目标：让同一 guest EGL context 的 GLES1/GLES2 共享 buffer binding、pixel-store 与
active texture 的权威状态。

验收：

- [x] 新增带稳定 identity 的 `GuestGlContext` 与可重置 `SharedGlState`。
- [x] GLES1/GLES2 transfer resolver 读取同一 `GlesTransferState`。
- [x] buffer、pack/unpack alignment 与 active texture 均在 native 调用成功后提交。
- [x] mixed draw 的 VBO offset 0 不再因两份 buffer binding 而误判为 null client pointer。
- [ ] GLCTX 阶段统一构建与测试。

非目标：texture object metadata、FBO/raster 与 fixed draw native state 由后续 WU 迁移。
