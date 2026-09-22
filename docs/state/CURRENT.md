# 当前状态

更新：2026-09-22。

## 最近进展

- [DASH-02](../tasks/gui/DASH-02.md) / [DASH-04](../tasks/gui/DASH-04.md)：Windows 接通运行时有界快照、
  诊断面板、共享键联动和九类事件泳道；计数观测明确区别实际发生帧/时刻。
  Dashboard 23 项、模块 45 项、loader/lifecycle 8 项定向回归及前端 30 项、CTest 2 项通过；
  真实 APK 验证只读不推进帧、MCP step 同步、线程/能力/player 联动。
  扩展 BootDex/File VFS 检查存在失败，详见任务单；FFmpeg 解码与 Linux 未验收。

- [GUI-6](../tasks/gui/GUI-6.md)：运行实例/详情 Dashboard 入口、PID 核对、独立无 RPC 窗口、
  就绪后自动打开及退出回收完成。C++ 定向回归、26 项前端测试及 7 项 CTest 通过；
  真实 WebView 验证窗口隔离/重开/主窗口关闭，受控子进程验证启动器解除跟踪后继续执行。
  真实 APK 的启动器手动/自动打开全链路本次未重跑。

- [DASH-03](../tasks/gui/DASH-03.md)：Windows run-apk `/dash/` 与只读 RPC、来源生命周期、
  顶栏/拓扑/uPlot 帧采样/线程焦点完成。32 项 C++ 定向回归、26 项前端测试和 2 项 CTest
  通过；真实 APK 验证只读、MCP 步进后帧同步、Java 栈联动及断线显示。新增来源与面板已由 DASH-02/04 接通，支持边界见对应任务。

- [DASH-01](../tasks/gui/DASH-01.md)：agent 四个只读 Dashboard 方法、快照状态传播、
  限量事件游标与线程关联完成；Windows Release 构建及 56 项定向用例/1039 断言通过。
  运行进程来源装配、HTTP/页面与真实会话链路验证见 DASH-03。

- [GUI-5](../tasks/gui/GUI-5.md)：九组每实例设置、严格 schema 1 覆盖与继承、冲突检测及
  参数预览接通；正常/预检/诊断复用启动生成，实例错误隔离。61 项定向回归、17 项前端
  用例、4 项 GUI CTest 和原生覆盖保存/重进/恢复继承通过。无 CLI 入口字段及沙盒管理仍预留。

- [GUI-4](../tasks/gui/GUI-4.md)：十组全局设置、schema 2（兼容 schema 1）、配置校验与
  保存冲突检测完成；主题/密度、导入目录预填及已有 CLI 设置接通，运行时预留项只保存。
  57 项定向回归、14 项前端用例、4 项 GUI CTest 及 Windows 主题保存/重启保留交互通过。

- [GUI-3](../tasks/gui/GUI-3.md)：Windows APK 导入向导、原生文件/目录选择、
  后台分析与原子入库接通；支持同包新实例及自动选中。53 项定向回归、11 项前端测试、
  4 项 GUI CTest 和原生导入交互通过；跨窗口拖放手势未实测，分包格式明确拒绝。

- [GUI-1](../tasks/gui/GUI-1.md)：Windows WebView2 宿主与 RPC 骨架完成，
  替换 ImGui 并保留游戏库模型；48 用例/343 断言、前端检查及真实 WebView 空库/CJK
  非空库冒烟通过。导入/设置/Dashboard UI 待后续阶段；Linux 暂缓，macOS 宿主待接入。

- [VFS 专项](../design/vfs/README.md) VFS-01/02/03 已实现并完成定向交叉验收：节点/backing/
  打开状态与定位 IO、统一资源预算、APK/OBB range backing、音乐/视频 lease、安装实例 id
  和 schema 3 四根沙盒均已接入生产入口。VFS、沙盒、syscall/Java 文件、SQLite、归档、
  音频与 Fake VideoView 回归通过；本机缺 FFmpeg 7 DLL，custom AVIO 真实解码用例按契约
  跳过并保留为环境验收缺口，不影响来源接口和无宿主路径链的完成判断。
  裸 `run-apk` 已补齐实例解析：零实例分配 package 首实例、唯一实例复用，多实例明确要求
  `--installation-id`，不再把内部安装 id 转嫁给普通 CLI 用户。同期修复 custom AVIO 扩充
  后 FFmpeg 符号表长度多一项导致 macOS `dlsym(nullptr)` 崩溃；真实 APK 已进入 guest 生命周期。

- [DVM-189](../tasks/dexvm/DVM-189.md) 末轮五项修复已完成定向验证：worker 故障统一唤醒
  writer、有界可取消准备任务、显式 MediaPlayer transport 阶段、按 stream 输出增益、
  OGG/MP3/WAV 增量音乐解码；AudioTrack 同步回填不再丢弃其余已跨越 periodic 通知。
  每轨写入/消费/队列/欠载/periodic 事件诊断快照及 `buffer/8`、1024 帧设备块的 30 秒
  逻辑回归已补，回归中无欠载且事件生成/投递对齐；本轮未重跑游戏。
  MP3 后退 seek 仍为线性重解码；raw-resource 完整链、
  手动步进阻塞 write 闭环及完整游戏音频验收仍待。Windows Release 下 Angry Birds 2.3.0
  首界面背景音乐已由用户确认正常；AUD-03 不关闭。

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
