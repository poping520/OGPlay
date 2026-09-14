# BND-34 · EGL/GLES 当前审计修复

目标：修复当前审计的查询内存边界、对象生命周期、版本搬运和扩展一致性问题。
依赖：[BND-33](BND-33.md)；依据 [审计](../../design/boundary/05-egl-gles-current-audit.md)
和 [ADR-0063](../../adr/media.md#adr-0063)。

## 已落地

- WU-1：UBO 索引数组真实宽度、sampler 单值、64 位向量、共享 program uniform 宽度；
  未发布的 vector texture/sampler 查询明确失败，info-log 可空 length 保持合法。
- WU-2：独立 display/context/surface 所有权；eager native share，真实 draw/read binding；
  共享源提前删除、逆序首次绑定、线程释放和最终 share group 退役。
- WU-3：GLES1 fixed/legacy/client-array/palette Context 隔离，texture/VBO 共享元数据；
  VAO bind/delete 与固定绘制内部 VAO、整数属性/divisor/常量恢复；OES/ES3 map 分组和
  native 指针身份校验，空闲区复用、显式 flush 范围及释放线程保留其他映射。
- WU-4：ES3 core buffer targets、2D/3D PBO offset、pack/unpack row/skip；GLES1/2 readback
  只提交像素行并保留 padding，内部读回和压缩 fallback 恢复 guest pixel-store 状态。
- WU-5：8 个 KHR sync/image handler 与两个 OES image target thunk；按 ANGLE 能力发布；
  真实 texture pbuffer/bind/release/mipmap、native swap/interval；窗口读取 draw 默认 FBO
  后恢复 read FBO/surface。matrix palette 补齐四入口和真实加权绘制。

## 验证

只构建 `cmake --build --preset windows-msvc --target ogplay_tests`，未运行全量测试。

- EGL/GLES/ANGLE/BND34 定向集合：100/100、4515 assertions 通过。
- 上述最终集合包含新增 BND34、OES mapbuffer、shader/uniform、未发布 vector query
  防护及 ES3 LOD/WRAP_R 参数；最后修改后已完整重跑此定向集合。
- 7 项 gates：capabilities_monotonic、boundary_hot_path、IDL self-test 与四个 catalog
  current gates 通过。日志位于 `.local/bnd34-{build,focused,gates}.log`。
- Windows D3D11 ANGLE 实测支持 reusable sync、GL texture EGLImage、texture pbuffer；
  不支持 EGL_KHR_fence_sync，因此未宣告 fence 扩展。
- MSVC 曾出现单个 guest_gl_context.obj 的 LNK1163 COMDAT 错误；仅移除该中间对象并
  重编译后恢复，未清空工作区或执行全量构建。

## 仍未完成的整体目标

本任务不能等同于“完整模拟 Android 4.4.4 GLES”。Android native-buffer/native-fence FD/
presentation-time、Pixmap/OpenVG、任意厂商扩展全集未提供；完整 CTS/Khronos 一致性验证
未执行。窗口仍走兼容层 pbuffer→SDL 路径，native swap interval 不保证桌面合成器实际时序。
本轮也未取得用户所述 Android GLES SO 压缩包，无法声称已完成该包全部 ELF dynsym/ABI 比对。

状态：核心审计缺陷已修复并完成定向回归；完整 Android GLES 兼容目标仍未完成。
