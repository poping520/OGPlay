# 当前状态

更新：DVM-89 AudioTrack native output sample rate 改为会话注入

## 当前阶段

- **根上下文 timed park 快进（DH 黑屏修复）**：`VmMonitorTable` 新增
  `SetClockAdvance` 快进钩子；根（lifecycle）上下文的 `Thread.sleep`、
  timed `join`、timed `Object.wait` 不再停泊在只有它自己会推进的确定性
  Clock 上——一个有界 peer 窗口（notify/interrupt 可先赢）后按停泊时长推进
  统一 Clock。DexVmBridge 在发布 uptime 时间源的同时接线
  `AdvanceAndroidClock`；未发布钩子的会话（测试接自走 wall clock）保持原
  停泊语义，worker context 永不快进。根因：DH `DungeonHunter.onCreate`
  在主线程轮询 Gameloft 授权 `while(!handle…) Thread.sleep(50)`，而
  `uptime_millis` 只由被阻塞的帧泵推进，第一句 sleep 即整会话黑屏。
- **DVM-89 AudioTrack notification**：新增 marker API 与 lifecycle 帧泵，按唯一 PCM
  mixer 的真实 playback head 投递 periodic/marker；跨多期补发，重设 marker 可再次触发，
  pause/stop/flush/release/null listener 静默。回调可重入 write，异常上浮，宿主音频线程不
  调 guest。
- **DVM-89 AudioTrack output rate**：`getNativeOutputSampleRate()` 不再持有私有 48 kHz
  常量；CLI 的 SDL 输出、混音泵与 DexVM context 共用一个输出 spec，并在会话构建时一次性
  注入 mixer rate。AudioTrack 自身 4–48 kHz 配置校验保持独立。
- **DVM-89 proc facts**：`GuestProcFacts` 由 app/session 请求显式传入 native process；
  `/proc/meminfo` 在启动时按 total/free 与固定派生规则生成只读快照，默认字节不变，非法
  facts 明确失败。没有宿主内存观测、动态刷新、MemAvailable、cpuinfo 或 Profile 覆盖。
- **Android 触摸 fallback**：无 listener target 时按 live UiTree 的 reverse-Z、deepest-first
  顺序调用命中点下的实际 guest `View.onTouchEvent` override；消费 DOWN 的 receiver 捕获
  MOVE/UP，detach 或 Activity switch 清除捕获。普通叶子 View 按 bounded MeasureSpec 取得
  默认尺寸，不再因 0x0 geometry 丢失输入。
- **BND-27**：fixed draw 将采样 stage 与 coordinate array 来源分开解析；优先 stage 自有
  array，单 stage 且全局只有一个有效 array 时受检回退，多 stage 禁止共享。GLES1/GLES2
  `active_texture` 仍按同一 EGL context 的 `SharedGlState` 有意共享，不复制第二份状态。
- **DVM-91**：core `IoRuntime` 逻辑 FileDescriptor `{vfs_path|apk_entry, source, base_offset,
  closed}`；`FileDescriptor.valid()`、`FileInputStream.getFD()`、PFD/AFD/openFd 与媒体
  FileDescriptor 数据源区间已完成。
- **DVM-90**：live UiTree attach/detach 驱动 per-holder Surface generation 已完成；
  visibility/format/size 重建、独立合成层与完整 WindowManager 仍 deferred。
- DVM-47 的 A6/DH exact/长运行 gate 与 threaded 默认裁决仍未闭合；解释执行继续由
  `VmExecutionLock` 串行，threaded 生产默认关闭。

## 本轮验证

- AudioTrack native output rate 注入、既有 AudioTrack 行为及阶段 catalog 定向 8/8、
  194 assertions 通过；macOS `dev` 的 `ogplay`/`ogplay_tests` 受影响目标构建通过。
- 根上下文 timed park 定向 4 用例（root sleep/timed join/timed wait 快进、
  worker 不快进对照）42 assertions 通过；`dev` 全量 ctest 1038/1038 通过。
- DH Release 实跑（用户原命令 + `--exit-after-frames 240`）：license 轮询越过，
  240 帧持续 presented，引擎 `GlobalResumingUpdate`/`APPPAUSE` 正常；MCP
  `frame_capture` 抓帧含完整主菜单画面（标题/NEW GAME/OPTIONS/MORE GAMES），
  黑屏消失，Ctrl-C 干净停止。
- Windows `windows-msvc` 配置与全目标构建通过；AudioTrack notification、既有 DVM-84、
  legacy media 与 mixer 定向 11/11、270 assertions 通过；完整 CTest 1034/1034 通过。
- PVZ Release 无 survey 进入标题画面并持续发布画面；用户实际键盘测试已越过
  `LoaderKeyboard.onKeyEvent()` 的 `getUnicodeChar()` 调用，可输入并提交自定义用户名。
- BND-27 Windows Debug/Release 全目标构建通过；解析策略、既有双 stage、真实 ANGLE
  `GL_TEXTURE31`→unit0 coordinate array 定向 3/3 通过。

## 下一步

1. 通用闭合 A6 DT_SONAME identity；DH 主菜单 Scenario gate 与 profile 长跑复验
   （授权轮询首次运行需 ~120 s guest 时超时后 `saveUnlockGame` 放行，二次起走
   preferences 秒过）。
2. 复验 DVM-47/threaded title gate；执行 Linux M9 严格出口复验。

## 阻塞与边界

- 根上下文快进推进确定性 uptime，故生命周期调用内的 guest 时钟不再严格等于
  16 ms×帧序；不影响仅按帧序 hash 的 exact gate，不宣称 wall-clock 对齐。
- 当前字符事实取自 SDL 当前宿主键盘布局；有参 `getUnicodeChar(metaState)` 的确定性映射覆盖
  常用字母、数字和标点，不宣称完整 Android KeyCharacterMap/dead-key/IME 能力。
- `dexvm.api19_capability_stack=complete` 只表示设计定义的 bounded 阶段闭包，不表示完整
  Android framework、联网、完整 SQLite 或任意 title 全流程可玩。
- 未实现能力继续记账并明确失败；长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) ·
[DexVM](../tasks/dexvm/README.md) ·
[Layout UI](../tasks/layoutui/README.md)；
操作手册见 [docs/playbook](../playbook/README.md)。
