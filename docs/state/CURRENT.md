# 当前状态

更新：DVM-89 `/proc/meminfo` 虚拟设备 facts 显式注入完成

## 当前阶段

- **DVM-89 proc facts**：`GuestProcFacts` 由 app/session 请求显式传入 native process；
  `/proc/meminfo` 在启动时按 total/free 与固定派生规则生成只读快照，默认字节不变，非法
  facts 明确失败。没有宿主内存观测、动态刷新、MemAvailable、cpuinfo 或 Profile 覆盖。
- **Android 触摸 fallback**：无 listener target 时按 live UiTree 的 reverse-Z、deepest-first
  顺序调用命中点下的实际 guest `View.onTouchEvent` override；消费 DOWN 的 receiver 捕获
  MOVE/UP，detach 或 Activity switch 清除捕获。普通叶子 View 按 bounded MeasureSpec 取得
  默认尺寸，不再因 0x0 geometry 丢失输入。
- **Android 键盘输入**：HAL 发布物理 scancode、当前布局 key symbol、左右 modifier 与
  repeat；session 映射 API 19 keyCode/metaState/Unicode/repeatCount/eventTime，未知键成为
  `KEYCODE_UNKNOWN`。DexVM `KeyEvent` 发布无参/有参 `getUnicodeChar`、meta/repeat 查询，
  生命周期不再把 SDL scancode 直接作为 Activity keyCode。
- **BND-27**：fixed draw 将采样 stage 与 coordinate array 来源分开解析；优先 stage 自有
  array，单 stage 且全局只有一个有效 array 时受检回退，多 stage 禁止共享。GLES1/GLES2
  `active_texture` 仍按同一 EGL context 的 `SharedGlState` 有意共享，不复制第二份状态。
- **DVM-91**：core `IoRuntime` 新增不含宿主/native fd 的逻辑 FileDescriptor
  `{vfs_path|apk_entry, source, base_offset, closed}`；发布 `FileDescriptor.valid()` 与
  `FileInputStream.getFD()`。PFD.open 只接受真实存在的只读 VFS 文件；AFD ctor/getter/close
  保留 API19 区间语义；AssetManager.openFd 只接受 STORED entry，发布受检 ZIP payload
  offset，DEFLATED 明确抛 FileNotFoundException。
- **媒体消费**：`EncodedAudioSource` 统一 resid、APK entry、VFS path 与纯字节区间；
  MediaPlayer 两个 FileDescriptor overload 在边界规范化 offset，prepare 真实加载/解码，
  start/pause/stop/release/volume/reset 驱动进程唯一 mixer。原 resid/SoundPool/AudioTrack
  路径保持不变。
- **DVM-90**：live UiTree attach/detach 驱动 per-holder Surface generation 已完成；
  visibility/format/size 重建、独立合成层与完整 WindowManager 仍 deferred。
- **DVM-89**：JNI↔DEX instance/static 字段统一存储与 native 失败保真已完成；专用 field
  acceptance 已补跑。KeyEvent 的 scancode→Android keyCode/Unicode deferred 项已闭合；
  OnKeyListener、DispatcherState tracking/long-press 仍 deferred。
- DVM-47 的 A6/DH exact/长运行 gate 与 threaded 默认裁决仍未闭合；解释执行继续由
  `VmExecutionLock` 串行，threaded 生产默认关闭。

## 本轮验证

- Windows `windows-msvc` 配置与全目标构建通过；proc facts 默认逐字节、自定义来源/派生、
  只读、既有快照幂等与两类非法配置定向 5/5、28 assertions 通过，capability/architecture
  门禁 5/5 通过。按要求未运行全量 CTest。
- 修复前以当前 Release 实跑 PVZ，标题画面点击入口后画面与会话均无变化，确认问题仍可复现。
- Windows Debug `ogplay_tests` 定向构建通过；深层 View 捕获、reverse-Z fallback、平台默认
  跳过、无参数嵌套 View 测量，以及既有 listener/click 路径定向 6/6、296 assertions 通过。
  按要求未执行完整 CTest；用户使用最终 Release 实测 PVZ 标题入口可以正常点击。
- Windows Debug `ogplay`、`ogplay_tests` 构建通过；SDL 事件规范化、session Android 输入
  映射、KeyEvent API 19 Unicode/meta/repeat 与既有 View dispatch 定向 6/6 通过。
- Android intrinsic catalog 与 architecture capability/platform/documentation/intrinsic-layout
  门禁 5/5 通过；未执行完整 CTest。
- PVZ Release 无 survey 进入标题画面并持续发布画面；用户实际键盘测试已越过
  `LoaderKeyboard.onKeyEvent()` 的 `getUnicodeChar()` 调用，可输入并提交自定义用户名，未再
  出现 method resolve fault。
- BND-27 Windows Debug/Release 全目标构建通过；解析策略、既有双 stage、真实 ANGLE
  `GL_TEXTURE31`→unit0 coordinate array 定向 3/3 通过。
- PVZ Release 无 survey 实跑取得 sequence=5976 的 800×480 presented PNG：标题、角色与
  UI 纹理正常显示，黑屏消失；继续运行无 guest fault。Ctrl-C 后仍停在 `App Suspend`，
  本轮按精确 PID 终止，沿用已记录的独立 teardown 观察。
- Windows Debug/Release 全目标构建通过，包含 `ogplay`、`ogplay-gui` 与 `ogplay_tests`。
- FD/AFD/openFd、FileInputStream.getFD、MediaPlayer 第二段 Ogg 区间、MP3、SoundPool、
  AudioTrack、legacy Java audio 与 OpenSL 定向 16/16 通过。

## 下一步

1. 通用闭合 A6 DT_SONAME identity 与 DH 当前启动阻断，复验 DVM-47/threaded title gate。
2. 执行 Linux M9 严格出口复验；后续 framework 长尾只按无 survey reached gap 排序。

## 阻塞与边界

- 当前字符事实取自 SDL 当前宿主键盘布局；有参 `getUnicodeChar(metaState)` 的确定性映射覆盖
  常用字母、数字和标点，不宣称完整 Android KeyCharacterMap/dead-key/IME 能力。
- DVM-91 不包含 pipe/socketpair、dup/fcntl 数值互操作、AFD Parcelable、网络 Uri、listener
  回调或宿主 ffmpeg；这些入口不得伪造成功。
- `dexvm.api19_capability_stack=complete` 只表示设计定义的 bounded 阶段闭包，不表示完整
  Android framework、联网、完整 SQLite 或任意 title 全流程可玩。
- 未实现能力继续记账并明确失败；长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) ·
[DexVM](../tasks/dexvm/README.md) · [Layout UI](../tasks/layoutui/README.md)；
操作手册见 [docs/playbook](../playbook/README.md)。
