# DVM-39 · Asphalt 6 启动期边界闭合

## 目标（一句话）

沿无 survey exact 路径闭合 Asphalt 6 实际命中的 Android/JNI/VFS 启动边界，直到
游戏稳定发布可见开场画面，同时不伪造平台事件或设备事实。

## 依赖

- DVM-31（EGL façade 已完成，exact 运行首次停在本入口）。

## 范围

- `registerReceiver(receiver, filter)` 在 receiver 非 null 时，按调用 Context 实例记录
  receiver dispatcher 身份；null receiver 仍只查询 sticky broadcast。
- `unregisterReceiver(receiver)` 只移除同一 Context 注册的 receiver。
- 未注册、重复注销、null 或跨 Context 注销按 AOSP `LoadedApk` 语义抛
  `IllegalArgumentException`。
- 本 WU 不实现广播匹配、排队或 `onReceive` 派发；当前单进程平台仍无广播来源。
- `Intent.removeExtra(name)` 参考 AOSP 4.4.4：删除现有 key；key 或 extras 不存在时
  无操作；String/Int 分表均删除，空分表释放 Intent 外层记录。
- `VideoView.setOnErrorListener(listener)` 保存或替换外部 listener，传 null 清除。
  当前宿主视频路径没有具体异步错误事件，因此不伪造 `onError` 回调。
- `VideoView.canPause/canSeekBackward/canSeekForward` 只在宿主 player 已成功打开时
  回答 true；无 player 时保持未 prepared 的 false。fallback completion 延迟到视频
  pump，禁止从 `start()` 重入 guest listener。
- Activity 替换时销毁旧 `SurfaceHolder` generation，并把新 Activity 注册的 holder
  接到仍存活的 managed surface；旧 callback 不得累积到新 Activity。
- 无连接的 `WifiInfo.getMacAddress()` 返回 null，不泄露或编造宿主 MAC；
  `getApplicationContext()` 返回跨 Activity 替换保持稳定的 application Context。
- JNI local frame 的声明容量是保证值，不是固定上限；frame 可自动增长到线程总上限。
  native `AudioTrack` 类声明与既有 handler 共用唯一安装入口。
- Android 4.4 主外置存储的 `/storage/emulated/0` 与 `/sdcard` 在 VFS 中规范到同一
  节点/overlay；A6 Profile 的 external 根与 guest 实际 `Android/data/<pkg>/files`
  路径一致。差异只存在于 Profile，不进入 `src/` title 分支。

## 验收

- 机器测试覆盖 receiver 生命周期、removeExtra、VideoView listener/control/fallback、
  Activity surface generation、application Context、Wifi、JNI local frame、AudioTrack
  类安装与 VFS 路径别名。
- Windows Release 构建及相关 intrinsic/catalog 测试通过。
- 不带 survey 的 `asphalt6.bootstrap` 三轮均发布 sequence 6 的同一可见画面
  `4f8e4bf1…`，无 guest fault 且 clean shutdown。

## 2026-08-14 实施结果

- 依次越过 receiver/Intent/VideoView 控制、Activity surface generation、Wifi、
  application Context、JNI local 引用、native AudioTrack 声明与 external 路径边界。
- A32 未处理停止诊断补充 fault access/reason 与 r0-r3/r12/sp/lr，曾据此确认 native
  `addObfuscationFileMap` 的空返回来自 external 路径不一致，而不是 EGL 或数据缺失。
- Windows/x64 Release 相关测试 17/17、全量 CTest 728/728 通过；正式
  `asphalt6.bootstrap` 三轮均
  `passed`，可见检查点 sequence 6、SHA-256 `4f8e4bf12e2c4bba…` 完全一致。
- 额外无 survey 长运行推进 3000 个宿主步进、1155 次 present，画面进入车辆开场
  动画，过程中无 guest fault 并 clean shutdown；该探索证据不宣称主界面 gate。

结论：DVM-39 完成，Asphalt 6 已稳定运行到可见开场动画；Profile 仍为 `partial`，
主界面与可游玩闭环尚未验收。广播派发和无具体错误来源的视频 error callback 仍未伪造。

## 追加（2026-09-03 Intent Integer ArrayList extra）

- 对照 AOSP 4.4.4 `Intent`/`Bundle`，补齐
  `putIntegerArrayListExtra(String, ArrayList<Integer>)` 与
  `getIntegerArrayListExtra(String)`：命中返回原 guest `ArrayList` 身份，缺失或显式 null
  返回 null，不复制列表也不检查擦除后的泛型元素。
- String、Int、Integer ArrayList 三类 extra 现共享单一逻辑 key 空间，后一次 typed put
  覆盖旧类型；`removeExtra` 同步清理三类分表及空 owner 记录。
- guest list 引用已进入 `android.owner-attached` trace，Intent 存活时保留 child，Intent
  死亡时 sweep 状态。行为、类型覆盖、null、remove 与 GC trace/sweep 均有机器测试。
