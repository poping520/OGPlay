# 当前状态

更新：2026-09-17。

## 最近进展

- DVM-185：有限 `WebView.destroy()` 生命周期与按实例 Settings。
- DVM-184：View 保存构造/膨胀 Context，`getContext()` 返回同一 guest 引用。
- DVM-183：从固定 API 19 `core.jar` 精确选入 `java.sql.Date`/`Time`/`Timestamp`
  值类型。不引入 JDBC 或其余 `java.sql` 包。
- [DVM-182](../tasks/dexvm/DVM-182.md)：`Class.getEnumConstants` 按 API 19 返回共享枚举
  常量数组的浅克隆。非枚举为 null；空枚举为非 null 空数组。复用 `isEnum` 与
  `SharedEnumConstants`。不宣称完整 Jackson 或游戏兼容。
- [DVM-181](../tasks/dexvm/DVM-181.md)：Class 运行时注解查询与受限注解成员执行。DEX 递归
  encoded value、AnnotationDefault、`@Inherited` 超类继承与每 VM 实现类走普通接口分派。
  Field 改用同一后端；Method.getDefaultValue 读取声明默认值。
- [DVM-180](../tasks/dexvm/DVM-180.md)：`getPackageInfo` 支持 `GET_ACTIVITIES`。
- [DVM-174](../tasks/dexvm/DVM-174.md)：DVM-174..179 统一归档。插屏路径越过
  AnimationListener、点击、stackTrace、LinearLayout 空 AttributeSet、clip 与 clickable。
- DVM-173 TLS-01/02 已验收；TLS-03 未完成。KeyStore DVM-172 约定范围闭合。
- Angry Birds 无 Profile 兼容链已越过 location、文件路径、旧 JNI、AudioTrack、Settings、
  权限、GLSurfaceView、Mac、runOnUiThread、KeyStore、DVM-174..179、`GET_ACTIVITIES`、
  Class 注解查询、`getEnumConstants`、SQL 日期值类型、`View.getContext()` 与
  `WebView.destroy()`。
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

- Windows Release 仅构建受影响目标；BootDex 1641 类，DEX
  `2758237a501a736e6c58cef79c413a4d36147301d657bf2279a0a333fc1a6747`。
  DVM-185 双解释器 WebView.destroy 定向用例 1/186 通过；
  `architecture.dexvm_intrinsic_layout` 通过。
- Angry Birds 无 Profile、关闭 survey：插屏路径越过 `WebView.destroy()`。
  下一独立首错为 `SQLiteDatabase.rawQuery`（Burstly `Cookie init thread` /
  `SQLiteCookieStorage`）。不宣称 User-Agent、浏览器内核或游戏兼容。
- guest JNI
  `e2d4b5de0f1c02b2d84c1e37d7d0561495b2ea1165648f2a01b5a80201a1a7f0`。
- `data/android/19/framework/` 为本地生成产物，不纳入版本控制。
