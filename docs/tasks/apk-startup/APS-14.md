# APS-14 · SurfaceTexture 原版 Java 与 Surface 端点闭环

## 目标与依赖

依赖 APS-13，将原版 SurfaceTexture 接入同一队列、图形与生命周期，并复现启动下一首错。
MediaPlayer 纹理生产者和前端后端设置留到下一阶段，不顺带接入 Camera。

## AOSP 锚点与实施选择

- `framework/base/graphics/java/android/graphics/SurfaceTexture.java`：原版类族/Handler/WeakReference、
  构造、矩阵参数验证、事件、public 异常包装全部保留 BootDex。
- `framework/base/core/jni/android/graphics/SurfaceTexture.cpp`：metadata/nativeInit/release/finalize；
  API19 遗留 nativeGetQueuedCount 不在注册表，显式记账 UOE，不伪造实现。
- `framework/native/libs/gui/GLConsumer.cpp`：构造保存 GLuint（含 -1）、不要求 current；
  attached/display/context 独立、attach/detach、源 timestamp/matrix 和 release debug bitmap。
- `framework/base/core/java/android/view/Surface.java`：在已有有界 Java 客户端机制中保留公开
  mLock/构造/释放/代际算法；private native 显式携带 owner 供 GC 归属，替代 compositor 指针。
  只支持 holder/纹理 producer 引用，Canvas/Parcel/SurfaceControl 不在本次准入范围。

## 实现与边界

- 固定 framework.jar 选入 SurfaceTexture 四类及 Surface.OutOfResourcesException；Surface 为
  有界 guest Java 客户端，普通锁与 ownership 协议不迁回 C++。
- SurfaceTexture 的三 native int 字段存逻辑 token；最多 64 个消费者、256 个 producer endpoint。
  Java/JNI 使用同一 BootDex 类/字段/方法。Surface release 不 abandon 消费者；consumer release
  幂等 abandon；GC/退出清 native 记录，weakSelf 包装不强根保活 Java consumer。
- 原版 Handler 交付真实 enqueue 事件，worker 不入 VM/GL；异步替换不重复通知。
  abandon 撤销尚未投递的 native 通知；已进入 Handler 的消息沿原版 Java 生命周期。
- 真实 update 成功才锁存图像与 metadata，无新帧保留图像；releaseTexImage 归还 slot 并按 AOSP
  debug bitmap 替换 external backing。图像来源仅为显式合成/后续 producer，不制造播放进展。
- MediaPlayer 对新 producer endpoint 明确 UOE/记账，避免将合法新端点误报为无效 holder。
  ANativeWindow/EGL window producer 尚未接入纹理端点，不将其映射成屏幕 window。

## 验收

- `SurfaceTexture*` 双解释器：原版构造、事件排队、Surface→队列→external pixels、matrix/PTS、
  空更新、detach/attach、无 current/失配、释放与弱引用 GC；复用 JNI framework 身份检查。
- `APS-11*`、直接相关 SurfaceHolder 定向回归保留 destroyed 回调期间有效、退役后无效。
- BootDex build/check、payload、capabilities、文档布局与 diff 检查。
- 同一原 APK ephemeral-sandbox 启动一次，机器断言旧错消失与下一首错；不是 title gate。

## 结果（已达成，支持范围保持 partial）

- 原版 SurfaceTexture 四类与运行时分配异常、Surface 有界 Java 客户端已生成；所有新增 native
  均有明确 handler。BootDex build/check、逐类 native 审计及 payload 校验通过；证据
  `.local/aps14-bootdex-{build,check}.log`、`aps14-bootdex-proof.json`、`aps14-payload-check.log`。
- 新 Java 源列表改变构建器指纹，使用本地固定 NDK r25c 执行 guest JNI 两次确定性重建，
  二进制 SHA-256 与原制品相同，仅维护 generator fingerprint；`aps14-guest-jni-build.log`。
- Release 仅构建 `ogplay-cli ogplay_tests`，最终构建记录 `.local/aps14-build-acquisition.log`；
  `aps14-run.py tests` 对所有当前 staged payload 作哈希核对后运行定向过滤，退出 0，
  **27 cases / 4077 assertions**，3.993 秒。包含 switch/threaded、真实像素/矩阵/PTS、
  Handler、弱 owner GC、Surface 代际/释放、single-buffer、并发 acquire/commit/abort 通知、PBO/row 恢复、失配 Context、
  cold shared binding 及原 SurfaceHolder/真实 ARM Java→NDK 回归。
- `architecture.boundary_hot_path`、capabilities check、文档布局、UTF-8、diff 检查通过；
  未跑全量测试、未运行已知独立失败的全 BootDex/媒体清单门禁。
- 同一 APK `run-apk --ephemeral-sandbox` 最终二进制实跑，退出 1、3.726 秒；第 0 帧越过
  SurfaceTexture 构造并交付一个 holder 的 surfaceCreated，UnityMain（guest thread 2）运行。
  新首错 **`method cannot be resolved: Landroid/app/Activity;->isFinishing()Z`**，位于
  UnityPlayer.isFinishing（dex_pc=16）→UnityPlayer$a.run（dex_pc=29）。没有修复下一独立问题。
  `.local/aps14-tml-run.log/json`、`aps14-tml-proof.json` 保存 reached-fault 证据，不是 title gate。
  生命周期及并发修正后各复核相同路径，首错保持一致；未扩展为三轮 Scenario 验收。
- 真实 title 尚未证明传感器队列和纹理图像消费路径；完整链路的像素证据来自真实 ANGLE
  SwiftShader 上的合成 producer。默认 Metal 仍明确不支持 external，不切换后端。
  Windows/Linux、MediaPlayer/NDK/EGL producer、Camera、Canvas/Parcel、context-loss 与前端
  renderer 偏好接线不在本阶段验收范围。
- Activity 首错局部修复：isFinishing/isDestroyed 仅读 Java 实例字段；nativeFinish 正常返回才置
  mFinished，实例销毁改经 final performDestroy 先置 mDestroyed 再虚派 onDestroy，其余顺序不变；
  进程关闭不合成 finish/onDestroy，未改 finishing_activity 或扩充其他 Activity 方法。
  BootDex build/check、Activity 两个既有 native 的逐类签名审计、payload 校验通过，证据
  `.local/aps14-activity-bootdex-{build,check}.log`、`aps14-activity-bootdex-proof.json`、
  `aps14-activity-payload-check.log`。Release 仅构建 ogplay-cli/ogplay_tests；过滤两个 Activity
  主题文件中的 `APS-14 Activity* / DVM-223* / DVM-250* / Activity.isTaskRoot* / DVM-219*`，
  **7 cases / 706 assertions** 全过（`aps14-activity-build.log`、`aps14-activity-tests.log`）。
  新增双解释器回归覆盖未 finish、成功后及 pause/stop/destroy 内外状态、实例隔离、
  进程关闭不置位/不调用 onDestroy、process_stopping 时 finish 抛 ISE 且不置位。
  原 APK `run-apk .local/games/tml/TML_v1.0.apk --ephemeral-sandbox` 运行一次，4.466 秒、退出 0，
  surfaceCreated 已交付，推进 378 逻辑帧后 guest_exit，呈现 0 帧；机器断言原解析错消失，
  无新方法解析错。下一原生 fatal 为 Mono `mini-posix.c:382` 的 `sigaction (...) != -1` 断言，
  同次还记录 Unity 创建 surface 失败及 `EGL_BAD_ATTRIBUTE`，暂不判定二者因果。
  再运行一次相同 ephemeral 命令并加 `--exit-after-frames 120 --survey-gaps .local/tml_survey.json`，
  3.133 秒、退出 0，358 逻辑帧/呈现 0 帧时 guest 自退，未达 120 呈现帧上限。
  按 hits 汇总的 Java 缺口队列为空：missing_classes=0、missing_members=0、stub_hits=0；
  survey 不覆盖上述 native/EGL 错误，未据此修复其他缺口。两次日志/退出元数据分别见
  `aps14-activity-tml-{run,survey}.log/json`，机器断言见 `aps14-activity-tml-proof.json`。
  本次只提供 reached-fault 证据，不是 title gate；剩余 native/EGL、无呈现及真实消费路径待查。
- EGL/退出提示局部修复：window 属性最多读取 64 words，接受 RENDER_BUFFER=BACK_BUFFER，
  保留 NULL/空表；single buffer、其他属性/值与未终止表 BAD_ATTRIBUTE 并记现有能力账本，
  未改 native window 校验、尺寸及后续创建。前端停止前保存 Java/native/信号退出事实：
  exit(n) 返回 n，信号返回 128+n，finish 正常返回 0；首帧前自行结束写结构化 error，
  非零 guest 码保留，否则返回具名启动失败码 1；宿主关闭/帧上限/MCP shutdown 返回 0。
  NativeExitCode 仍只报告 exit/exit_group；另加只读信号查询，处理 native 调用包装后的退出异常，
  未改变停止顺序、SessionExitRequested 或 MCP/Scenario 控制。runtime_failure 仍传播原错。
  GUI 既有非零退出事件已带 ReadLogTail，WebUI 将 log_tail 放入“操作提示”；仅补日志尾部回归。
  Release 构建 ogplay-cli、ogplay_tests 及受影响的 ogplay_app_tests/ogplay；定向
  `APS-14 EGL* / APS-14 native termination* / DVM-218 native process exit* / run-apk exit status* /
  Android EGL query strings* / Android EGL owns distinct*` 为 **6 cases / 162 assertions**，
  GUI tracker/log tail 为 **2 cases / 16 assertions**，全部通过。原生 SIGABRT 用例同时确认
  NativeExitCode 未改变；首次测试暴露异常包装后已修正并复验。证据
  `.local/aps14-egl-exit-{build,rebuild,tests,gui-tests}.log`。
  实跑一次原 APK `run-apk .local/games/tml/TML_v1.0.apk --ephemeral-sandbox --exit-after-frames 600`，
  5.762 秒、退出 **1**；机器断言原 `[EGL] Failed to create surface` 与 EGL_BAD_ATTRIBUTE 消失，
  Unity context=3 已建立。下一停止首错为逻辑帧 **1225** 的 UnityMain（guest thread 2）
  `UnityPlayer.nativeRender()Z` → `guest memory is unmapped`；runtime_failure 路径未输出最终
  呈现计数，不据此声称已呈现或达到 600 呈现帧。零呈现自行退出非零码由定向回归验证。
  `.local/aps14-egl-exit-tml.log/json` 与 `aps14-egl-exit-proof.json` 为 reached-fault 证据，
  不是 title gate；Mono mini-posix.c:382 sigaction 断言仍在，未处理原生内存/调度等下一缺口。
