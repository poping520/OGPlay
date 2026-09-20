# 当前状态

更新：2026-09-20。

## 最近进展

- [DVM-189](../tasks/dexvm/DVM-189.md) 末轮五项修复已完成定向验证：worker 故障统一唤醒
  writer、有界可取消准备任务、显式 MediaPlayer transport 阶段、按 stream 输出增益、
  OGG/MP3/WAV 增量音乐解码。MP3 后退 seek 仍为线性重解码；raw-resource 完整链、
  手动步进阻塞 write 闭环及游戏复现/听测仍待验收，AUD-03 不关闭。

- `SetIntrinsicStaticRef` 写静态引用前先 `EnsureClassLinked`，不跑 `<clinit>`。
  冷路径 `Security.getProperty` 能给已注册但未链接的 BootDex `Engine.door` 赋值。
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
  `SetIntrinsicStaticRef` 定向 4 用例 109 断言通过（冷 VM、JCA 先序、
  后台 guest 线程、helper 校验）。
- `angry-bird-v.1.0-android_port` 无 Profile、关闭 survey：Flurry
  `Engine.door` 原错消失，启动越过 `onSurfaceCreated`/`onSurfaceChanged`。
  随后 `Failed to open data/FONT_BASIC_N900.dat`，`nativeUpdate` 返回 false
  于 1067 presented frames 退出。不宣称资源包或游戏兼容。
- DVM-186 已切换到 API 19 原版数据库 Java 栈和固定 SQLite/VFS 后端。BootDex 1729 类
  build/check 与全类链接通过；DVM-186 26 用例/336 断言、SQLite 回归 12 用例/204 断言通过，
  覆盖规范路径锁、journal、空 BLOB、零字节库、打开标志、异常子类、有界 CursorWindow、
  ENOSPC、提交中断恢复、双解释器/guest 线程 teardown，以及
  独立 SQLite 双向互操作及 GC/关闭竞争。Angry Birds 2.3.0 原始类级路径的空库建表、
  过期行删除、有效行 rawQuery 均通过。SQLite 提交误将 LocaleData、日期 pattern 与货币入口
  替换为不完整宿主实现，现已全部恢复 guest ICU；真实 guest JNI 验证 Calendar 周规则及
  SimpleDateFormat，完整进程越过 Jackson 初始化 NPE。SQLite 后续清理已删除旧格式解析、
  影子状态和文件头预检；API 19 `EventLog` 四个写入重载及默认损坏处理链已闭合。
  BootDex 1731 类 build/check 通过；空沙盒真实运行创建 16384 字节标准 SQLite `cookiedb`，
  越过 Cookie 初始化与 EventLog 首错。下一独立首错为
  `Utils.encryptedDeviceId` 对空设备标识调用 `String.length()`。DVM-186 完成。
- `AES/CBC/ZeroBytePadding` 已由 guest Java CipherSpi 接入现有 Conscrypt
  `AES/CBC/NoPadding`/guest OpenSSL 后端。独立向量、分段/一次性、空输入、整块输入、
  解密去尾零、重复初始化及短缓冲共 2622 断言通过；BootDex 1732 类 build/check 通过。
  Angry Birds 2.3.0 空沙盒运行 4000 presented frames，原 `encryptedDeviceId()` 二次调用 NPE
  未再出现，本轮未触发新的致命首错。
- `View.setScrollBarStyle/getScrollBarStyle` 已按 API 19 保存逐实例样式掩码，`WebView`
  继承解析、四种样式、默认值、实例隔离及其他 flags 保持在双解释器 124 断言中通过。
  Angry Birds 2.3.0 空沙盒运行 5000 presented frames，原方法解析错误未再出现，本轮未触发
  新的致命首错；滚动条绘制及 inset/padding 变化仍未实现。
- [DVM-187](../tasks/dexvm/DVM-187.md)：View 焦点 owner、三个 requestFocus 重载、监听器、
  scroll-container/双轴滚动条状态，以及当前 APK 使用的 WebSettings、RenderPriority、
  client 默认回调与 GC 引用已完成定向验证。页面/JavaScript 执行仍明确失败；按用户安排
  未复跑游戏，真实初始化链结果待用户测试。
- [DVM-188](../tasks/dexvm/DVM-188.md)：默认禁用网页策略已完成。WebView 加载通过主 Looper
  异步报告 `ERROR_UNSUPPORTED_SCHEME`，JS 只记录，stop/destroy 可取消；外部 HTTP/HTTPS
  ACTION_VIEW 不启动浏览器。严格诊断模式保留原异常。Windows Release 与两个解释器后端
  定向验证通过；按用户安排未复跑游戏。
- guest JNI
  `e2d4b5de0f1c02b2d84c1e37d7d0561495b2ea1165648f2a01b5a80201a1a7f0`。
- `data/android/19/framework/` 为本地生成产物，不纳入版本控制。
