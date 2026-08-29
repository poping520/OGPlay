# BND-26 · GLES1 OES_texture_cube_map 与 guest 语义契约

## 目标

为 GLES1 固定管线边界补上 `GL_OES_texture_cube_map`（绑定、六面上传、派生状态与固定
管线 cube 采样），并同步修正两项被该阻断顺带暴露的契约偏差：ES1 查询字符串透传 ES3
后端结果、guest 非法枚举以宿主异常终止进程。目标是让按真机 GLES1 语义使用立方体贴图
与查询能力的 title 与设备行为一致。

## AOSP 与 reached-path 依据

- API 19 `framework/native/opengl/include/GLES/glext.h` 定义
  `GL_TEXTURE_CUBE_MAP_OES 0x8513` 与 `GL_TEXTURE_CUBE_MAP_{POSITIVE,NEGATIVE}_{X,Y,Z}_OES
  0x8515..0x851A`；KitKat 软件兜底 libagl 无任何 cube 实现，真机能力来自硬件 GLES1
  驱动（reached 设备普遍支持），因此 OGPlay 在 GLES1 门面层自建而非移植。
- libagl 的状态/错误模型作参照：纹理派生状态按对象记账，非法 target 走
  `ogles_error(GL_INVALID_ENUM)` 继续执行而非终止。
- 真机 GLES1 报 `OpenGL ES-CM 1.1` 与 ES1 扩展列表；透传 ANGLE ES3 后端的版本/扩展串
  会误导按版本解析管线的引擎。
- pvz reached run：`glBindTexture(GL_TEXTURE_CUBE_MAP, tex)` 在启动资源上传阶段
  （`draws=0 clears=0`）触发 `GLES1 texture target must be GL_TEXTURE_2D, got 0x8513`
  致命异常。

## 范围

- GLES1 门面层接受 `GL_TEXTURE_CUBE_MAP` 绑定/参数/mipmap 目标；TexImage 族 face
  target 经共享层 `TextureBindingTargetForMetadata` 归一到 cube 对象，绑定路径仍拒绝
  face target。
- `glEnable/glDisable/glIsEnabled` 接受 `GL_TEXTURE_CUBE_MAP`，按 unit 记账，与
  `GL_TEXTURE_2D` 同键控模型；`EnabledTextureUnits` 同时计入 cube 使能的 unit；绘制按
  unit 实际使能目标采样。
- 固定管线 uber shader 按 stage 采样目标生成 `samplerCube`/`textureCube(vec3)` 变体
  （顶点 varying 提升 vec3），程序按 (stage0,stage1) 目标惰性选择，未启用 stage 的
  sampler 挂无绑定 unit（避免同 unit 混用采样器类型的 `INVALID_OPERATION`）；ANGLE 侧
  原生执行。
- `glGetString(GL_VERSION)` 合成 `OpenGL ES-CM 1.1`，`GL_EXTENSIONS` 合成恰为已实现
  能力的列表（cube/ETC1/PVRTC/mapbuffer/RGB8-RGBA8，ATC 无解码支持不宣告）；
  VENDOR/RENDERER/
  SHADER_BINARY_FORMATS 保持后端透传。
- guest 非法枚举类 `std::invalid_argument` 在 `Gles1Module::Invoke` 收口转为
  per-context 错误锁存（`SharedGlState` 持有，首错保留、`Reset` 清零），`glGetError`
  先排空锁存再查 ANGLE；`logic_error`/`runtime_error`/`GuestTransferError`/`MemoryFault`
  仍显式失败——它们标记宿主契约破坏而非 guest 非法用法。
- 不实现：面级别不一致的 `GL_INVALID_OPERATION` 校验、cube 的 ETC1/PVRTC 特例路径
  差异、`INVALID_VALUE` 与 `INVALID_ENUM` 的细分（统一映射 `INVALID_ENUM` 为近似）；
  GLES2 维持直达后端的严格语义（其错误由 ANGLE 自有 `glGetError` 报告）。

## 验收

- [x] `glBindTexture`/`glTexParameter*` 接受 0x8513；face target 用于绑定时按 spec
  报 `GL_INVALID_ENUM` 锁存；
- [x] `glTexImage2D` 族接受 0x8515..0x851A，base format/mipmap 状态归一到 cube 对象；
- [x] `glEnable(GL_TEXTURE_CUBE_MAP)` per-unit 生效并参与 draw 的 unit 枚举；
- [x] cube 采样的固定管线 draw 输出正确像素（方向选面不串面），程序变体按目标选择；
- [x] `glGetString(GL_VERSION)` 为 `OpenGL ES-CM 1.1`，`GL_EXTENSIONS` 恰为已实现
  能力列表；末项保留分隔符，使只在空格处提交 token 的旧解析器也能读到最后一项；
- [x] guest 非法枚举返回 0 并锁存 `GL_INVALID_ENUM`，`glGetError()` 消费后清空，后续
  调用不受影响；
- [x] 定向回归（状态层 + 边界级像素断言，含“关闭即失败”形态）通过；按要求不执行
  完整测试。

## 验证记录

- Windows Debug/Release 构建通过。新增边界级用例
  `GLES1 cube map textures bind upload and sample the fixed pipeline`
  （62 assertions：版本/三扩展串、0x8513 绑定、face 绑定转锁存、六面 1x1 上传、方向
  `(1,0,0)` 采样到 +X 面且未串面）与状态层 cube 断言（42 assertions）通过；10 处既有
  THROW 断言按锁存契约改写；`*GLES1*/*GLES2*/*texture*/*boundary*` 46 用例、10542
  assertions 无回归；按要求未执行完整测试。
- 实现期间修复共享层两处既有缺陷：`SharedGlState` texture metadata 键未归一 face
  target（写入/读取键不一致，此前未被 GLES1/GLES2 任一路径触发）；cube 变体中未启用
  stage 的 sampler2D 与启用 stage 的 samplerCube 同指 unit 0 触发 GLES
  `INVALID_OPERATION`。
- pvz Release 实跑越过 cube 绑定阻断，guest 日志依次出现完整
  `LoadTask::STARTING..MAIN_MENU` 序列，持续到 `f≈4447`；新停止点为
  `A32 guest call exhausted its tick budget (consumed≈4.48e9)`，属独立问题另行记账。

## 2026-08-29 · RGBA8 RenderTarget 后续闭合

- A6 混合链接由 GLES1 `glGetString(GL_EXTENSIONS)` 初始化共享 driver 能力表；缺少
  `GL_OES_rgb8_rgba8` 时，内部 RGBA8 格式 14 被降级为 RGBA4 格式 7，
  `createRenderTarget` 返回空对象，随后在 `libasphalt6.so+0x7f2c14` 被无检查虚调用。
- GLES1 合成扩展串补入 `GL_OES_rgb8_rgba8` 并以空格结束；生产代码没有 title/package
  分支。机器测试同时验证完整扩展 token、真实 ANGLE `GL_RGBA8` renderbuffer identity
  与 framebuffer completeness，禁止只宣告字符串。
- macOS dev 定向 2/2、119 assertions 与相关 architecture 4/4 通过；Release exact
  手动步进点击“触摸继续”进入主菜单，到 frame 10932、draw 64991 仍为
  `guestFault=null`。shutdown 在 `teardown.guest_callbacks` 未完成，作为独立生命周期
  问题保留，不回退本项图形结论。

状态：完成。
