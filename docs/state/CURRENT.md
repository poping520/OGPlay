# 当前状态

更新：2026-09-24。

## 运行状态

- **Dead Trigger 1.1.0**：无 Profile 的 APK/OBB 启动已越过 [VFS-04](../tasks/vfs/VFS-04.md)
  挂载、`Resources.getAssets()` 和 [DVM-190](../tasks/dexvm/DVM-190.md)
  `PackageManager.getReceiverInfo`。关闭 survey 的当前首错为
  `PackageManager.getServiceInfo(ComponentName,int)`；这只是 reached-fault，未通过游戏验收。
- **Angry Birds 2.3.0**：无 Profile、空沙盒运行 5000 presented frames，
  `View.setScrollBarStyle/getScrollBarStyle` 原方法解析错未再出现，未触发新的致命首错。
  滚动条绘制和完整游戏兼容仍未验收。此前 SQLite/guest ICU、EventLog、
  `AES/CBC/ZeroBytePadding` 及 `SetIntrinsicStaticRef` 首错均已越过；证据见
  [DVM-186](../tasks/dexvm/DVM-186.md) 与相关任务单。

## 已交付范围

- **Android/DexVM**：受审 API 19 BootDex 与 intrinsic 提供游戏直接调用的能力；
  PackageManager 查询仅覆盖当前 APK。
  receiver 声明、启用状态和独立元数据经 Manifest→session→DexVM 传递；
  `getReceiverInfo` 支持 0、GET_META_DATA、GET_DISABLED_COMPONENTS。VM 状态由字段/数组
  与统一对象模型持有，文件 IO 经 Libcore Posix 进入 VFS。
- **VFS**：[VFS-01..04](../design/vfs/README.md) 的资源 backing、定位 IO、预算、
  APK/OBB range、媒体 lease、安装实例沙盒和无 Profile external/OBB 挂载已接入。
  受影响目标及定向回归通过；多实例选择要求显式 `--installation-id`。
- **Windows GUI/Dashboard**：[GUI-1..8](../tasks/gui/README.md) 与
  [DASH-01..04](../tasks/gui/README.md) 的库、导入、设置、移除、只读诊断和独立窗口
  已完成定向验证。[GUI-7](../tasks/gui/GUI-7.md) 机型预设仍是纯数据，未接入运行时。
- **音频**：[DVM-189](../tasks/dexvm/DVM-189.md) 的 worker、MediaPlayer、增量解码和
  AudioTrack 通知修复完成定向验证；Angry Birds 首界面背景音乐已获用户确认。
- **WebView**：[DVM-187](../tasks/dexvm/DVM-187.md) 的视图/设置基础行为与
  [DVM-188](../tasks/dexvm/DVM-188.md) 的默认禁用网页策略已完成定向验证。

## 未闭合边界

- Dead Trigger 下一独立缺口是 `PackageManager.getServiceInfo`；不运行 Binder、
  system_server、外部包数据库、广播投递或 Play 服务。
- GUI 的真实 APK 导入→设置→启动→Dashboard→退出→移除全链路验收按用户安排延后；
  GUI 的 Linux/macOS 宿主未完成。WebView 页面/JavaScript 执行仍不支持。
- [DVM-189](../tasks/dexvm/DVM-189.md) 的 raw-resource 音频完整链、手动步进写入闭环、
  游戏音频验收未完成；本机缺 FFmpeg 7 DLL，custom AVIO 真实解码未验收。
- TLS-03、完整 mmap/lock、系统 CA、Java 长尾及完整 Android framework 不在已验收范围。
  `architecture.dexvm_intrinsic_layout` 仍因既有媒体源码未列入检查清单而失败，
  详见 [DVM-190](../tasks/dexvm/DVM-190.md)。
