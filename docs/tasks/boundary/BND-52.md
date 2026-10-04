# BND-52：原生 packed depth/stencil 发布与调用错误诊断

状态：实现、macOS 定向回归及两种尺寸的原 APK 菜单复验完成。
依赖：[BND-51](BND-51.md)、现有 ANGLE framebuffer/renderbuffer 与 FrameService。

目标：按当前 ANGLE 完整扩展 token 发布 GL_OES_packed_depth_stencil，使 guest 能选择
已有 D24S8 路径；边界捕获的 GL 错误按调用进入计数/trace，并接入 run-apk Dashboard。
不修改 APK、不伪造 FBO complete、不绕过深度/模板测试，不发布整个原生扩展列表。

验收：无扩展/相似 token 不发布，重复 token 去重；复用 ES2/3 字符串与数量查询一致回归。
真实 D24S8 双附件 complete，索引绘制回读红色，深度/模板拒绝绘制回读黑色；
slow/fast/managed/proc slow/proc fast 的无附件 FBO clear 都记录 0x0506，合法调用
不继承错误，glGetError 首错优先/读取清除且不重复记账，try-trace 保留精确错误。

macOS Release ogplay/ogplay_tests 构建通过；上述回归及既有负 draw/trace ring 共
7 用例/8546 断言通过。附加 BND-29 回归在原有 eglTerminate 后应抛 runtime_error 的
断言失败（其余断言通过）；本次未改异常转换和终止语义，留作独立退出问题。
诊断只统计边界已捕获 GL 错误调用，不额外消费 GL 状态，不声明完整 GLES 验证。
Windows/Linux 未实跑；原 APK 验证不是完整游戏/title gate。

分析证据：`.local/tales-menu-black-analysis/`；实施证据：`.local/packed-depth-stencil-fix/`。

原 APK 复验：无 Profile 的 800×480 与仅尺寸 Profile 的 1280×720，均在开场结束后
显示真实菜单。采样分别 f=27467/presentedFrame=3830、f=24535/presentedFrame=3509，
无运行期 guestFault，VideoView 列表为空，边界已捕获 GL 错误计数为 0。
PNG 尺寸及非黑像素阈值有机器判定，截图人工确认菜单。没有验收完整游戏。
两次 MCP shutdown 均退出 1，仍为 guest=1、pc=0x3864d27c、读取 0x315f0001 的
独立 teardown 内存故障；菜单裁剪也尚未修复。结束检查确认 OGPlay 进程全部关闭。
