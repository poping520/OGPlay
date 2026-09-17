# 当前状态

更新：2026-09-17。

## 最近进展

- [DVM-180](../tasks/dexvm/DVM-180.md)：`getPackageInfo` 支持 `GET_ACTIVITIES`，从当前
  APK Manifest 发布 Activity/activity-alias 元数据。未请求时 `activities` 保持 null。
  不宣称完整 PackageManager、组件启动或广告可用。
- [DVM-174](../tasks/dexvm/DVM-174.md)：DVM-174..179 统一归档。无 Profile Angry Birds
  插屏路径越过 AnimationListener、`setOnClickListener`、`getStackTraceString`、
  LinearLayout 空 AttributeSet、ViewGroup clip 与 View clickable。
- DVM-173 TLS-01/02 与 Session 边界已验收；TLS-03 未完成。KeyStore DVM-172 约定范围闭合。
- Angry Birds 无 Profile 兼容链已越过 location、文件路径、旧 JNI、AudioTrack、Settings、
  权限、GLSurfaceView、Mac、runOnUiThread、KeyStore、DVM-174..179 以及当前包
  `GET_ACTIVITIES` 查询。
- [DVM-161](../tasks/dexvm/DVM-161.md) 至 [DVM-170](../tasks/dexvm/DVM-170.md) 对应首错已闭合。
- BND-34..39 已闭合本轮 EGL/GLES 核心审计；不等同 CTS/Khronos 完整认证。

## 当前边界

- **VM/Java**：DexVM 使用受审 API 19 BootDex；普通 Java 状态归字段/数组，JNI 使用真实 VM
  类型关系；targetSdk 1..13 单独启用 AOSP 旧 JNI direct-reference 兼容并警告。文件 IO 通过
  Libcore Posix 进入唯一 VFS。完整 mmap/lock、系统 CA 和 Java 长尾仍明确失败。
- **Android**：只覆盖当前 APK 直接需要的 Context、Activity、资源、文件、设置及有限服务；
  `getPackageInfo` 可查询当前包权限与 Activity 元数据。不运行 Binder system_server、Play
  服务或完整 Android 系统。
- **网络**：socket 仍受 NetworkRuntime policy/transport 控制。loopback TLS/HTTPS 已闭合，
  默认网络关闭。TLS-03 未完成。`URLEncodedUtils` 尚未纳入。
- **图形/UI**：ANGLE GLES 与 SDL3 窗口输入已接通；完整 framework 排版、Dialog/Web 展示、
  传感器、动画执行和系统 UI 不在当前范围。

## 验证快照

- Windows Release 仅构建受影响目标；BootDex 1638 类，DEX
  `506da0c2323946e53852affe2bfb9b3b585ae3d1c2a20cb345093f6296fe97bd`。
  DVM-180 双解释器与 Manifest exported 夹具通过；`architecture.dexvm_intrinsic_layout` 通过。
- Angry Birds 无 Profile、关闭 survey：插屏路径已越过 `getPackageInfo` flags=4097。
  下一独立首错为 `Class.getAnnotation`（Jackson `VisibilityChecker$Std.<clinit>`）。
- guest JNI
  `e2d4b5de0f1c02b2d84c1e37d7d0561495b2ea1165648f2a01b5a80201a1a7f0`。
- `data/android/19/framework/` 为本地生成产物，不纳入版本控制。
