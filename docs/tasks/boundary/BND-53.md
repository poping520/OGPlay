# BND-53 · 有实际 backing 的 RGB565 EGL 配置

状态：有界完成。日期：2026-10-08。依赖：ANGLE、EGL registry、HAL 原生存储。
决策：[ADR-0106](../../adr/media.md#adr-0106)。

支持范围：同一配置目录中发布真实 RGBA8888/RGB888/RGB565；最低选择条件不改写
实际属性。RGB565 优先复用宿主精确 native config；Metal 无此配置时使用 HAL 创建
B5G6R5 16 位纹理，以 ANGLE metal texture EGLImage 导入为私有默认 framebuffer backing。
导入后实际色位/量化/alpha/扩展由临时 Context 探测通过才发布。

Metal fallback 仅 GLES1 compatibility/GLES2、颜色 5/6/5/0、无深度/模板/MSAA、无 texture
pbuffer binding。EGL carrier pbuffer 不作为 guest 的颜色存储；不改 ANGLE SDK，不改 chooser。
Surface 独立拥有 native texture/image，非共享 Context 导入相同存储；Context 仅持有私有
FBO 附着。guest framebuffer 0 映射到 draw/read backing，FBO names 独立映射，私有
renderbuffer names 在 attachment 保活后删除。绑定/查询/删除/默认附件修改共用此事实。

继承 deferred destroy/current 与 terminate 生命周期，不切换软件后端，不引入 Skia 或
GLES→桌面 GL 转译。GLES3 fallback、纹理绑定、深度/模板/采样及完整驱动枚举不支持。
新增格式必须有实际 backing 与 query/readback 判据，不可只改报位数或用 RGBA 替代。

验收：配置查询/精确颜色 chooser、actual GLES bit 查询与低位颜色读回、alpha=1、
跨 Context/独立 draw/read、guest FBO 隔离/删除/禁止修改默认附件、present/deferred destroy，
既有 RGB888/Context/surface 回归；原 APK 无 Profile/无 survey reached-fault。

证据：`.local/wb-rgb565-fix/`；macOS 构建恢复既有 warnings-as-errors=OFF 缓存设置。
Windows/Linux、故障注入矩阵及 title gate 未验收。

验证（2026-10-08）：macOS Release ogplay/ogplay_tests 构建通过；新增 RGB565 及相邻
RGB888/current/terminate/swap/share group 定向 7 项/399 断言通过。另运行既有
BND34 regular EGL separate surface 回归仍失败 2 断言，与旧 baseline/preexisting 日志
完全一致（green vs red），不记录为通过，也不修复该独立问题。

原 APK/外部数据、无 Profile/无 survey、隔离沙盒越过 chooser-null 首错，GLThread
进入 GPUInstallerRenderer.onDrawFrame；下一首错为 GL10.glClear 在 GL10$Impl 上的
virtual dispatch failed，退出 1，进程已关闭。此 reached-fault 不是完整游戏验收。

Windows 后续（2026-10-09）：ANGLE 已有原生 RGB565 D24S8，但旧代码取第一个
D0/S0，导致带深度/模板要求的 chooser 返回 null。现优先模板位数、再深度位数，
查询仍来自真实 native config；Metal fallback 的原支持边界不变。
Windows Release 构建及 DVM-229/UI/EGL 定向 8 项/429 断言通过，新增深度/模板
跨非共享 Context 的实际绘制回归；原颜色量化范围兼容 D3D11 的向下量化。
原 APK 成功 createContext/createSurface/makeCurrent，进入 renderer.onSurfaceCreated；
随后子 View 尺寸回调缺失导致 f=0 停滞，180 秒超时结束进程。macOS/Linux 本轮未复跑。
证据 `.local/drawable-egl-complete-{build,tests}.log`、
`.local/drawable-egl-game-20261009-190837/`；资源链与下一阻塞详见 DVM-229。
