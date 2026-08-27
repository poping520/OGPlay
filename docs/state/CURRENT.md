# 当前状态

更新：通用 Android 键盘输入链闭合（未提交）

## 当前阶段

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
- **MP3**：固定 CC0 minimp3 commit `ea99364f61c14656440e8d77e9c233ccf3124633`，薄适配器
  支持 ID3/帧对齐、mono/stereo PCM16、稳定采样率/声道与 64 MiB 输入/128 MiB PCM 上限；
  来源、license、hash 与 fixture 记于 `third_party/minimp3/README.md`。
- **DVM-90**：live UiTree attach/detach 驱动 per-holder Surface generation 已完成；
  visibility/format/size 重建、独立合成层与完整 WindowManager 仍 deferred。
- **DVM-89**：JNI↔DEX instance/static 字段统一存储与 native 失败保真已完成；专用 field
  acceptance 已补跑。KeyEvent 的 scancode→Android keyCode/Unicode deferred 项已闭合；
  OnKeyListener、DispatcherState tracking/long-press 仍 deferred。
- **BND-26**：GLES1 cube-map 固定管线、API19 VERSION/EXTENSIONS 与 guest GL error 锁存已
  完成。PVZ 已越过此前 cube-map 和 tick-budget 阻断。
- DVM-47 的 A6/DH exact/长运行 gate 与 threaded 默认裁决仍未闭合；解释执行继续由
  `VmExecutionLock` 串行，threaded 生产默认关闭。

## 本轮验证

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
- core/Android catalog 2/2、architecture 6/6 通过；初跑唯一失败为旧 CURRENT 9659-byte 超过
  6144-byte rolling limit，本文件重写后已复验通过。
- PVZ 无 survey 分层实跑：
  - L1+L2 越过 `AssetFileDescriptor.<init>(PFD,JJ)`，新阻断为
    `MediaPlayer.setDataSource(FileDescriptor,J,J)`（f≈6692）。
  - L4 独立接入后停止点不变，符合尚无消费端的预期。
  - L3 后越过 `LoadTask::FINISHED` 与所有 FD/prepare/start 路径，持续到
    f=14081、presented=3745，无 guest fault；prepare 仅在实际区间读取与 MP3 解码成功后
    返回，start 已建立循环 voice，SDL audio output 已启动，媒体链验收成立。
- 最终实跑由人工 Ctrl-C 停止；lifecycle 输出 `App Suspend` 后 10 秒内未自行退出，随后按
  精确 PID 终止。这是独立 teardown 观察，仅记录，未扩展 DVM-91。

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
