# WU-PERF-12 · 实时窗口异步读回

目标：将窗口 GPU 回读等待与下一帧 guest CPU 重叠，保留原像素、逻辑和同步读取语义。
范围：Metal 上两块私有 PBO、完成 fence、内部共享 collector context；首帧同步，后续
只发布已完成数据；有界背压、最后一帧交付、来源/软件帧切换退役及错误传播。
依赖：既有 ANGLE PBO/map、EGL KHR fence、FrameService、SDL3 与统一 Clock。
设计：[ADR-0100](../../adr/media.md#adr-0100)。guest Context 仍对应唯一 native Context；
collector 是不可见的宿主服务 context，不执行 guest，不改其 GL 状态。

验收：当前同步读取与异步像素一致，pack 状态/方向受检；collector 阻塞时另一槽可提交，
首帧不重复、无下一次 swap 仍交付末帧、退役后无旧像素；手动步进不启用异步。
只跑受影响定向检查。Release 同沙盒/同关卡/同核数实际成功 present FPS 对照；
不使用重复帧、降低分辨率或时间倍率制造收益。完整游戏与跨平台验收独立。

状态：本方案实现及定向验证完成。
Release 29 项/1244 断言通过，包括原生窗口 swap、pack 状态、同步当前像素、末帧、
软件/Context 切换及 collector 阻塞下提交。3 项相邻 EGL 检查在旧 dev 二进制也相同失败：
分离 draw/read 像素与 EGL image 创建，未扩大为本次修复。
架构检查仍被 HEAD 已有 android_module.cpp 的 std::function 阻断；本次呈现路径使用
显式 owner/function-pointer filter，不新增该违规。
同初始沙盒、原 APK、2560×1600、3 核、MCP 无 diag、15 秒成功 present 计数：
对话 62.02→66.36 FPS，教学 57.59→60.13 FPS；画面和点击已检查。
采样中 guest GL 提交栈不再出现原 ReadRgba8/waitUntilCompleted，collector 承担完成等待。
性能采样先于最后的 Surface 身份查询修正及 owner/function-pointer 收窄；最终二进制
随后完成原生窗口/Context 回归和同路径实跑检查，不重复性能矩阵。
收尾仍有 NativeOnDestroy 读取故障；最终 smoke 在 surfaceDestroyed 后退出超时，
取证预算后强制关闭，退出握手未闭合。正式 title gate、全关卡、持续移动与跨平台未验收。
证据 `.local/tales-async-readback-20261006/`。
