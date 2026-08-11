# WU-GLCTX-02 · 统一 texture / buffer object namespace

目标：让两套 API 入口在同一 ANGLE context 中共享 object lifecycle、texture unit binding
与 texture metadata。

验收：

- [x] GLES1 生成的 texture 可由 GLES2 bind/query，反方向同样成立。
- [x] GLES1/GLES2 cross-delete 同步清除所有 unit binding 与 texture metadata。
- [x] level-0 base format 与 `GL_GENERATE_MIPMAP` metadata 由 shared state 唯一拥有。
- [x] buffer binding/delete 已复用 WU-GLCTX-01 的统一 transfer state 与 ANGLE namespace。
- [ ] GLCTX 阶段统一构建与测试。

非目标：framebuffer/raster/common capability 由 WU-GLCTX-03 迁移。
