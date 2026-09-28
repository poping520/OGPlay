# 当前状态

更新：2026-09-28。

## 运行状态

- **Dead Trigger 1.1.0**：无 Profile 的 APK/OBB 启动已越过 [VFS-04](../tasks/vfs/VFS-04.md)
  挂载、`Resources.getAssets()`、[DVM-190](../tasks/dexvm/DVM-190.md)
  `getReceiverInfo`、[DVM-191](../tasks/dexvm/DVM-191.md) `getServiceInfo`、
  [DVM-192](../tasks/dexvm/DVM-192.md) `getPermissionInfo` 与
  [DVM-193](../tasks/dexvm/DVM-193.md) `queryBroadcastReceivers` 及
  [DVM-194](../tasks/dexvm/DVM-194.md) SQLite 系统配置资源读取，以及
  [DVM-195](../tasks/dexvm/DVM-195.md) NativeActivity 类链接与 Activity 切换。
  原版 Camera$PreviewCallback、XML Pull 工厂/完整接口与 KXml 已纳入 BootDex。
  XML DEX 闭包核对、build/check、双解释器定向 2 用例/882 断言通过；实际 APK 的
  settings.xml 经 AssetManager/InputStream 正确解析 BOM、gles_mode=2、useObb=True，
  畸形 XML 抛原版异常，原有 AXML 调用及未支持方法记账失败受检。
  无 Profile、关闭 survey 的同路径启动已进入 native 加载；下一致命首错为
  JNI_OnLoad 返回不支持的版本 0，前置日志报告 libmono.so 段扩展失败。
  全 BootDex 类链接遍历完成，但仍因既有 MediaPlayer overlay 检查与过期类数断言（1729）
  整体失败。证据为 `.local/xmlpull-*.log` 与 `.local/xmlpull-dependencies.json`；
  游戏仍只是 reached-fault，未通过验收。
- **Angry Birds 2.3.0**：无 Profile、空沙盒运行 5000 presented frames，
  `View.setScrollBarStyle/getScrollBarStyle` 原方法解析错未再出现，未触发新的致命首错。
  滚动条绘制和完整游戏兼容仍未验收。此前 SQLite/guest ICU、EventLog、
  `AES/CBC/ZeroBytePadding` 及 `SetIntrinsicStaticRef` 首错均已越过；证据见
  [DVM-186](../tasks/dexvm/DVM-186.md) 与相关任务单。

## 已交付范围

- **Android/DexVM**：受审 API 19 BootDex 与 intrinsic 提供游戏直接调用的能力；
  PackageManager 查询仅覆盖当前 APK。
  receiver 声明、启用状态、独立元数据与逐过滤器事实经 Manifest→session→DexVM 传递；
  `queryBroadcastReceivers` 支持 flags=0、无 data/type 的有界 action/category 查询，
  返回真实 BootDex 列表并按 API 19 去重/排序；
  service 同样传递查询字段与自身元数据。`getReceiverInfo/getServiceInfo` 支持
  0、GET_META_DATA、GET_DISABLED_COMPONENTS。VM 状态由字段/数组
  与统一对象模型持有，文件 IO 经 Libcore Posix 进入 VFS。
  `getPermissionInfo` 只查当前 APK 的权限定义，定义与请求/授权集合分离。
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

- Dead Trigger 下一独立缺口是 native 库加载（JNI_OnLoad 返回 0/libmono.so 段扩展失败）；
  不运行 Binder、
  system_server、外部包数据库、广播投递或 Play 服务。
- GUI 的真实 APK 导入→设置→启动→Dashboard→退出→移除全链路验收按用户安排延后；
  GUI 的 Linux/macOS 宿主未完成。WebView 页面/JavaScript 执行仍不支持。
- [DVM-189](../tasks/dexvm/DVM-189.md) 的 raw-resource 音频完整链、手动步进写入闭环、
  游戏音频验收未完成；本机缺 FFmpeg 7 DLL，custom AVIO 真实解码未验收。
- TLS-03、完整 mmap/lock、系统 CA、Java 长尾及完整 Android framework 不在已验收范围。
  `architecture.dexvm_intrinsic_layout` 仍因既有媒体源码未列入检查清单而失败，
  详见 [DVM-190](../tasks/dexvm/DVM-190.md)。
