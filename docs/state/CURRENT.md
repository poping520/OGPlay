# 当前状态

更新：2026-09-17。

## 最近进展

- [DVM-174](../tasks/dexvm/DVM-174.md)：DVM-174..179 统一归档。无 Profile Angry Birds
  插屏路径越过 AnimationListener 类型、`setOnClickListener` 可覆盖、`getStackTraceString`、
  LinearLayout 空 AttributeSet、ViewGroup clip 与 View clickable。不宣称完整动画、XML 构造、
  ViewGroup 或手势系统。
- DVM-173 TLS-01/02 与 Session 边界已验收；TLS-03 未完成。KeyStore DVM-172 约定范围闭合。
- Angry Birds 无 Profile 兼容链已越过 location、文件路径、旧 JNI、AudioTrack、Settings、
  权限、GLSurfaceView、Mac、runOnUiThread、KeyStore 以及 DVM-174..179 插屏 View/Log 缺口。
- [DVM-161](../tasks/dexvm/DVM-161.md) 至 [DVM-170](../tasks/dexvm/DVM-170.md) 对应首错已闭合。
- BND-34..39 已闭合本轮 EGL/GLES 核心审计；不等同 CTS/Khronos 完整认证。

## 当前边界

- **VM/Java**：DexVM 使用受审 API 19 BootDex；普通 Java 状态归字段/数组，JNI 使用真实 VM
  类型关系；targetSdk 1..13 单独启用 AOSP 旧 JNI direct-reference 兼容并警告。文件 IO 通过
  Libcore Posix 进入唯一 VFS。完整 mmap/lock、系统 CA 和 Java 长尾仍明确失败。
- **Android**：只覆盖当前 APK 直接需要的 Context、Activity、资源、文件、设置及有限服务；
  `setOnClickListener` 可覆盖并登记 listener；`setClickable`/`isClickable` 接入 UiNode 与
  触摸分派，不宣称长按或完整手势系统。不运行 Binder system_server、Play 服务或完整
  Android 系统。
- **网络**：socket 仍受 NetworkRuntime policy/transport 控制。loopback TLS/HTTPS 已闭合，
  默认网络关闭。TLS-03 未完成。`URLEncodedUtils` 尚未纳入。
- **图形/UI**：ANGLE GLES 与 SDL3 窗口输入已接通；完整 framework 排版、Dialog/Web 展示、
  传感器、动画执行和系统 UI 不在当前范围。

## 验证快照

- Windows Release 仅构建受影响目标；BootDex 1638 类，DEX
  `506da0c2323946e53852affe2bfb9b3b585ae3d1c2a20cb345093f6296fe97bd`。
  DVM-174..179 双解释器与定向像素/输入夹具通过；`architecture.dexvm_intrinsic_layout` 通过。
- Angry Birds 无 Profile、关闭 survey：插屏路径已越过 `BurstlyView.setClickable`。
  下一独立首错为 `PackageManager.getPackageInfo` flags=4097。
- guest JNI
  `e2d4b5de0f1c02b2d84c1e37d7d0561495b2ea1165648f2a01b5a80201a1a7f0`。
- `data/android/19/framework/` 为本地生成产物，不纳入版本控制。
