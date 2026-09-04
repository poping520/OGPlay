# 当前状态

更新：运行入口自动使用内置 Android 4.4.4/API 19 guest 系统库。

## 当前阶段

- **运行与 GUI**：`run-apk` 按 Profile API 自动选择内置系统库，不再接受
  `--system-dir`；GUI-17/18 双栏界面支持选择、启动和删除。缺 Profile 条目仍可尝试
  generic 启动但不宣称兼容。Profile frame 日志已降为 `debug`，周期从 60 帧调至 600 帧。
- **API 19 系统库**：`data/android/19/lib` 内置从 AOSP
  `android-4.4.4_r2.0.1` clean tag 构建的 libc/libm/libdl/libstdc++/libz，随包携带来源、
  哈希、ELF 身份、构建记录和 NOTICE；构建、安装及 macOS bundle 均统一 staging。
  API 22/23 尚未纳入。
- **Java core**：File streams 公共 API（NIO/FileChannel 除外）、Resources XML 有界 pull、
  SimpleDateFormat API 19 最小层级以及 Observer/Observable 已闭合。Observable 复用
  `ArrayList`，按 changed flag 门控并在 receiver monitor 外按快照虚派发 `update`；覆盖
  回调删除、异常传播和 nested GC 强根保活。Object streams 已闭合 API 19 接口继承、
  primitive/block-data、默认 `Serializable` 字段图、引用、枚举及 `Date` 往返；数组、
  Externalizable/custom hooks 与默认 UID 计算仍 deferred。Locale `getLanguage()` 返回
  会话注入语言，`ENGLISH` 返回 `en`。`URLEncoder/URLDecoder` 已闭合 UTF-8 form codec；
  `URL` 的 http/https/file 构造与 getter 为纯值语义，`openConnection/openStream` 才按离线
  策略失败；其他 charset 与完整 XmlPullParser 长尾仍 deferred。
- **Android Context 文件流**：`openFileInput/openFileOutput` 已接入 app files 目录；
  `MODE_PRIVATE` 覆盖、`MODE_APPEND` 追加，Activity/ContextWrapper 继续委托进程 base Context，
  文件状态只存在于 core `IoRuntime` 与统一 VFS。APK 资源流现返回具体
  `ByteArrayInputStream`，不再实例化 API 19 抽象 `InputStream`。
- **Intent**：String/Int/`ArrayList<Integer>` extra 共享逻辑 key；DVM-97 已闭合
  action/type/data/scheme/categories/flags 与 action/MIME/scheme/authority/category 有界匹配。
  ContentProvider MIME、path/SSP、manifest resolver 与系统广播仍明确不支持。
- **基础架构**：DVM-92 teardown 及 DVM-94～96 的稳定 linker metadata、`MethodShape`、
  own-member intrinsic 与 owner-state trace/sweep 已完成；Dalvik access flag 与 Java reflection
  modifier mask 已集中到共享头；DVM-98 统一现有 8 个平台 enum；生命周期异常日志现含
  具体 Java 类型。解释执行仍由
  `VmExecutionLock` 串行，threaded 生产默认关闭。
- **Title 进展**：Tales 已越过 uniform sampler、`GL_OES_mapbuffer`、Context 路径和 thread
  context loader 缺口，两个 native 库完成 JNI 初始化；新首错为
  `android.location.LocationListener`。PvZ Profile 已绕过 COPPA/Terms 外壳；APK 自带
  `android.support.*` 按应用类链接，主 Looper 与 LocalBroadcastManager Intent 匹配链已闭合；
  新首错为 NetworkImpl 的 SMS/network action 边界。
  A6/DH exact、长运行 gate 与 threaded 默认裁决尚未闭合。见
  [DVM-47](../tasks/dexvm/DVM-47.md) 和 [WU-0231](../tasks/m5/WU-0231.md)。

## 最近验证

- 2026-09-04 Windows Release：URL 双后端 124/124、DVM-88 全组 9/9（252 断言）；PvZ 越过
  `URL.<init>`，新首错为 `Ljava/util/EnumSet;`。
- 2026-09-03 macOS Release：资源流与 Locale 定向测试通过；Asphalt 5 原 APK 3 帧烟测
  正常退出；title-flow 末帧 Main Menu 经人工检查并在两个 fresh sandbox 中稳定为
  `cb892db9…`，golden 更新后 6 个 checkpoint 全部通过并 clean shutdown。
- 2026-09-03 Windows `windows-msvc` Debug：`ogplay_tests` 构建通过；Context final
  `getString(int)` 继承/资源/异常与相邻定向检查 5/5 通过；此前 Locale/小写检查 12/12 通过。
- 2026-09-04 Windows `windows-msvc` Release：Android Support 应用所有权 10/10、相邻 lazy
  hierarchy/survey 11/11 通过；Context/ContextWrapper 主 Looper 身份及 scheduler 定向测试
  6/6、136 个断言通过；PvZ Profile 实跑停于 `Intent.resolveTypeIfNeeded(ContentResolver)`。
- 2026-09-03 Windows `windows-msvc` Release：Object input/output streams 的 API 19 层级、
  完整继承方法表、双后端 primitive/UTF/header/block-data 与 `null`/`String` 往返定向回归通过；
  后续对象图能力见上一条验证。
- 2026-09-03 Windows `windows-msvc` Release：`ogplay_tests` 构建通过；Context 私有文件流
  覆盖/追加/读取及异常、ContextWrapper 委托、Android catalog 定向回归通过。
- 2026-09-03 Release：Observer/Observable 4/4、相邻回归 8/8、账本与架构检查 4/4。
- 2026-09-04 Windows Release：DVM-98 及 enum 相邻回归 9/9，架构检查 1/1；现有
  8 个平台 enum 已迁移到统一生成器。

## 下一步

1. 处理 PvZ NetworkImpl 的 SMS/network action 边界，补 Tales `LocationListener`，完成 DH 主菜单 Scenario gate。
2. 执行 A6 bootstrap 三轮、gc_long 与 threaded title gate。
3. 出现可复用停滞 fixture 时，补 Diagnostics 外部触发子进程验收。

## 边界

- 根上下文 timed park 可推进确定性 uptime；worker 仅在 clock driver 阻塞时补到 deadline，
  不宣称 guest 时间与 wall clock 对齐。
- 键盘字符来自 SDL 当前宿主布局；不宣称完整 KeyCharacterMap、dead-key 或 IME。
- `dexvm.api19_capability_stack=complete` 仅表示 bounded 设计闭包，不代表完整 Android、联网、
  SQLite 或任意 title 全流程可玩；缺失能力继续记账并明确失败。
- 长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) · [DexVM](../tasks/dexvm/README.md) ·
[Layout UI](../tasks/layoutui/README.md) · [Diagnostics WU1](../tasks/diagnostics/WU-DIAG-01.md) ·
[Diagnostics WU2](../tasks/diagnostics/WU-DIAG-02.md) · [Playbook](../playbook/README.md)
