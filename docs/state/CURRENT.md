# 当前状态

更新：修复 macOS GUI bundle 大小写冲突并补 GUI 冒烟验证

## 当前阶段

- **DVM-87 SimpleDateFormat 最小层级已增加**：core catalog 发布抽象 `Format`、抽象
  `DateFormat` 与具体 `SimpleDateFormat`，严格保持 API 19 父链；`Format` 同时实现
  Serializable/Cloneable。指定 pattern/Locale 构造器校验参数并保存 pattern；format/parse
  仍未实现。沿用 DVM-87、未新建 WU。
- **DVM-79 FileOutputStream 公共 API 已补齐（NIO 除外）**：增加 File/String append
  构造、`FileDescriptor` 构造、`getFD` 与两个自有 write override；路径构造即时创建/
  截断，逻辑描述符与流共享输出状态并保持借用关闭不误关原描述符。`getChannel` 依赖完整
  FileChannel，按用户指定暂不处理且未伪造返回值。同步恢复抽象 `OutputStream` 的公共构造、
  Closeable/Flushable、bulk-write 虚分派与 no-op flush/close，并校准 ByteArray/Filter/
  Buffered/Data 输出子类 own override 和 DataOutputStream 父类；沿用 DVM-79、未新建 WU。
- **DVM-79 FileInputStream 公共 API 已补齐（NIO 除外）**：增加 File/String/
  FileDescriptor 构造、`available/close/getFD/read/read(byte[],int,int)/skip`；路径流拥有
  描述符，借用流关闭不误关原 FD，共享 FD 的流共用读取位置。同步恢复抽象 `InputStream`
  的公共构造、Closeable、bulk-read/skip 虚分派、默认 mark/close/available 与 reset 异常，
  并校准 ByteArray/Filter/Buffered/Data 输入子类。`getChannel` 继续 deferred；沿用 DVM-79、
  未新建 WU。
- **DVM-18 Resources XML 补充已完成**：`Resources.getXml(int)` 经唯一 ARSC→sealed APK→
  strict AXML 链发布 `XmlResourceParser`，闭合 `getEventType/next/getName/getText/close`；
  缺失/畸形资源抛 `Resources.NotFoundException`。合成 APK 与本地 exact 两份 `res/xml`
  交叉验证通过，沿用 DVM-18、未新建 WU；完整 AttributeSet/XmlPullParser 长尾 deferred。
- **DVM-48 Thread 第三优先级已完成**：补齐四个带 ThreadGroup 参数的 Thread 构造器、存活枚举、
  dump/count/check、Thread.State、park/unpark 单许可与 interrupt action；统一 Clock、
  switch/threaded 行为及 API19 shape 均有定向测试。完整 ThreadGroup family 继续 deferred，
  deprecated stop(Throwable) 只补 shape 并明确失败。未新建 WU。
- **DVM-48/DVM-79 最终检查已修复**：intrinsic 调用参数及跨 nested guest call 的新对象
  使用 execution-local 临时强根；File filter 显式 GC 回调不再访问已清扫引用，`compareTo`
  的左路径同样保活。未装配 VFS 与 ENOENT 已分离，File 查询/变更明确抛 Java 异常。

- **基础架构状态**：DVM-94～96 的稳定 linker metadata、`MethodShape` 调用解析、
  own-member intrinsic 声明和 owner-state trace/sweep 已完成；DVM-92 teardown 已通过 title
  验收。契约见对应 DVM 任务单与 ADR-0025/0028/0029。
- **Tales Context 首错已修复**：uniform reflection 接受 API19
  `GL_SAMPLER_3D_OES` 单值 shape；GLES1 extension 目录和 boundary 真实实现
  `GL_OES_mapbuffer` 三入口。Context 新增 `getPackageCodePath/getCacheDir/getApplicationInfo`；
  code/resource/sourceDir 共用只读 guest APK，cache 经 VFS 建目录，ApplicationInfo 为进程
  稳定身份且 wrapper 仅委托 base。Thread context loader 补齐后 Tales 两个 native 库均
  完成 JNI 初始化，新首错为 `android.location.LocationListener` 类层级缺口。见
  [DVM-47](../tasks/dexvm/DVM-47.md)、[WU-0231](../tasks/m5/WU-0231.md)。
- **仍未闭合**：DVM-47 的 A6/DH exact/长运行 gate 与 threaded 默认裁决；解释执行仍由
  `VmExecutionLock` 串行，threaded 生产默认关闭。

## 最近验证

- 2026-08-30 macOS release `ogplay_tests` 增量构建通过；DVM-87 6/6（85 assertions）与
  core catalog 1/1（475 assertions）通过，覆盖 SimpleDateFormat 父链和 pattern 构造。
- 2026-08-30 macOS release `ogplay_tests` 增量构建通过；File/InputStream 23/23（936 assertions）及
  core/java.io 契约 2/2（496 assertions）通过，覆盖 API shape、FD 游标与借用关闭。
- 2026-08-30 macOS release 增量构建通过；binary XML 6/6（74 assertions，含本地 exact），
  `Resources.getXml` 1/1（35 assertions），core/android catalog 契约通过。关闭 survey 的
  clean-sandbox PvZ 短跑先被既有 `java.util.Observer` 层级缺口阻断，未到 XML 路径。
- 2026-08-28 macOS `dev` 全量 CTest 1066/1066 通过。
- 2026-08-30 macOS release `ogplay-gui` 构建并启动成功；bundle layout、GUI options 与两项 smoke 共 4/4 通过。
- DH Release 越过 license 轮询并稳定到主菜单，240 帧持续 presented，Ctrl-C 干净停止；
  PVZ Release 已进入标题画面、可输入并提交用户名。

## 下一步

1. 补 clean-sandbox PvZ `java.util.Observer/Observable` 与 Tales `LocationListener` 层级；
   完成 DH 主菜单 Scenario gate。
2. 执行 A6 bootstrap 三轮、gc_long 与 threaded title gate。
3. 首次出现可复用停滞 fixture 时，补 Diagnostics 外部触发子进程验收。

## 边界

- 根上下文 timed park 可推进确定性 uptime；worker 仅在 clock driver 已阻塞时补到
  deadline。生命周期内 guest 时间不再严格等于 16 ms×帧序，不宣称 wall-clock 对齐。
- 键盘字符来自 SDL 当前宿主布局；不宣称完整 Android KeyCharacterMap、dead-key 或 IME。
- `dexvm.api19_capability_stack=complete` 只表示 bounded 设计阶段闭包，不表示完整
  Android framework、联网、完整 SQLite 或任意 title 全流程可玩。
- 未实现能力继续记账并明确失败；长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) ·
[DexVM](../tasks/dexvm/README.md) · [Layout UI](../tasks/layoutui/README.md) ·
[Diagnostics WU1](../tasks/diagnostics/WU-DIAG-01.md) ·
[Diagnostics WU2](../tasks/diagnostics/WU-DIAG-02.md) · [Playbook](../playbook/README.md)
