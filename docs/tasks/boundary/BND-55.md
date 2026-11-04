# BND-55 · GLES 查询按当前 Context 路由

目标：消除混合链接 GLES1/GLES2 时 ELF 依赖顺序对 glGetString 查询结果的影响。
依赖：现有 EGL 线程版本 registry、稳定 proc 路由和 GLES1/2 查询实现。

## 根因与方案

Asphalt 6 的 libasphalt6.so 同时依赖 GLES1/GLES2，GLES1 排在前面。CG 后创建 ES2
Context，却从 GLES1 同名入口取得 `OpenGL ES-CM 1.1`。原生引擎版本检查失败，
createDevice 返回 null，Game 构造在 SO `0x3e2f5c` 读取 `[r0,#0x34]` 崩溃。
相同命令已复现，数据包主体命中，不是 CG 解码故障。

AOSP 4.4.4 `frameworks/native/opengl/libs/GLES_CM/gl.cpp` 与 `GLES2/gl2.cpp`
均通过当前线程 EGL hooks 查询。复用现有 GlApiRouting：两个 glGetString 导出统一按
调用线程 current client version 路由；覆盖所有 pname，保留各查询实现的受检扩展集合。
fast/slow/managed/proc 共用绑定；无 current 返回零；隐式托管路径保留原入口语义。
不改游戏、不伪造设备版本、不扩大 GL 能力集合。

## 验证

证据：`.local/analysis/asphalt6-fix/`。
- 基点：原 library run 命令复现 `pc=0x31112f5c/read 0x34`。
- Release 仅构建 ogplay-cli / ogplay_tests。图形定向 43/43、11045/11045 断言通过，
  覆盖双库、fast/slow/managed/proc、ES1↔ES2 切换、线程隔离与无 Context。
  恢复按导出库选择查询实现后，新用例 19 条断言失败；恢复修复后全绿。
- 实际游戏：CG 跳过后 nativeInit 正常返回，经中文标题、主菜单、自由竞速、拿索、
  竞速赛、MINI 进入实际比赛。车辆持续行驶，HUD 速度从 10 到 47 km/h、场景推进，
  暂停按钮显示菜单。两次 racing-state 均为 running、guestFault=null，呈现序号递增。
- 原 `library run com.gameloft.android.GAND.GloftAPHP` 命令另跑完整 CG 自然结束路径，
  同样出现 nativeInit END / setFullyLoaded，无原 fault。
- capabilities check、git diff --check 通过；未运行全量测试或正式三轮 title gate。

## 退出路径的附带修复

比赛正常运行后的 shutdown，以及自然播完后 SIGINT，均暴露 AudioTrack.write Java
回调的 thread_stopped 被 native clone 当成异步失败，退出码 1。JNI 解释调用桥现仅将
该枚举转换为携原 JNI 线程身份的 A32GuestCallCancelled，由已有 clone owner 完成退出；
其他 DexVmError 不吞掉。不修改 AudioTrack 算法或停止次序。

双解释器新增 JNI 停止回调回归，复用现有 clone 取消/真实错误用例；相关 10/10 用例、
675/675 断言通过。修复后二次启动越过 CG 至标题，SIGINT 退出 0，日志无 teardown 错误。
证据：cancel-candidate.log、cancel-tests.log、cancel-regression.log。

最终格式化后重建两个目标，五个直接相关测试文件合计 53/53 用例、11720/11720 断言通过
（closeout-tests.log）。proof.json 机器核对原错、自然 CG、比赛帧推进、反证及正常退出，
并保存截图/构建/测试/退出日志 SHA-256。修改文本 UTF-8、任务与 CURRENT 链接受检。

状态：完成本次 macOS 首错及退出复现；全赛程、音频听感、Windows/Linux 未验收。
