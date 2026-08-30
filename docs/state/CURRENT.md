# 当前状态

更新：补齐 FileInputStream 公共 API 与 InputStream 继承语义

## 当前阶段

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

- **DVM-94～96 已完成**：linker metadata 改为追加地址稳定存储；调用解析按
  `InvokeKind` 隔离并消费链接期 `MethodShape`。Intrinsic declaration 只含 own members，
  `ContextWrapper` 用显式 override 委托 `mBase`，Application/Activity/Service 直接继承。
  Android 4.4.4 protected callbacks 已校准，真实 DEX protected override 受门禁保护。
  新增 `OwnedStateTable` 与 Android owner-state trace/sweep，删除 map-key 兼容根。
  双后端共用 invoke target 和参数/返回验证，默认后端与
  profile 不变。见 [DVM-94](../tasks/dexvm/DVM-94.md)、[DVM-95](../tasks/dexvm/DVM-95.md)、
  [DVM-96](../tasks/dexvm/DVM-96.md)、ADR-0028/0029。

- **A6 AudioTrack 与 RGBA8 首错已修复**：MODE_STREAM 按真实 PCM 字节回压并可被
  release/teardown 中断；共享 ANGLE context 发布真实 `GL_OES_rgb8_rgba8`。用户实听音频
  正常，Release 手动步进进入主菜单并稳定到 frame 10932；shutdown 卡点仍独立保留。见
  [DVM-93](../tasks/dexvm/DVM-93.md)、[BND-26](../tasks/boundary/BND-26.md)。
- **Tales Context 首错已修复**：uniform reflection 接受 API19
  `GL_SAMPLER_3D_OES` 单值 shape；GLES1 extension 目录和 boundary 真实实现
  `GL_OES_mapbuffer` 三入口。Context 新增 `getPackageCodePath/getCacheDir/getApplicationInfo`；
  code/resource/sourceDir 共用只读 guest APK，cache 经 VFS 建目录，ApplicationInfo 为进程
  稳定身份且 wrapper 仅委托 base。Thread context loader 补齐后 Tales 两个 native 库均
  完成 JNI 初始化，新首错为 `android.location.LocationListener` 类层级缺口。见
  [DVM-47](../tasks/dexvm/DVM-47.md)、[WU-0231](../tasks/m5/WU-0231.md)。
- **DVM-92 已完成并通过 title 验收**：teardown 单向退役 Java/native 图形入口，独立取消
  中断 blocking wait 与新建 futex；契约见 [DVM-92](../tasks/dexvm/DVM-92.md) 和 ADR-0025。
- **近期兼容性闭合**：Android View fallback 已支持 reverse-Z、deepest-first 触摸路由；
  BND-27 修复 GLES1 coordinate array 来源；DVM-91 完成 FileDescriptor/PFD/AFD 媒体
  区间能力；DVM-90 完成动态 SurfaceView holder generation。
- **仍未闭合**：DVM-47 的 A6/DH exact/长运行 gate 与 threaded 默认裁决；解释执行仍由
  `VmExecutionLock` 串行，threaded 生产默认关闭。

## 最近验证

- 2026-08-30 macOS release `ogplay_tests` 增量构建通过；File 家族 18/18（783 assertions）、
  InputStream 相关 5/5（153 assertions）通过，覆盖自定义子类继承语义、FileInputStream
  双后端/API shape/FD 共享游标/借用关闭/null/缺失文件；core catalog 与 core-only java.io
  契约另 2/2 通过（496 assertions）。
- 2026-08-30 macOS dev `ogplay_tests` 增量构建通过；OutputStream 自定义子类双后端虚分派、
  FileOutputStream 双后端/API shape/null/read-only descriptor、wrapper 双 close、core-only
  java.io 与 Java/native VFS 定向 7/7 通过（259 assertions）；core/android catalog 契约
  2/2 通过（3619 assertions）。
- 2026-08-30 macOS release 增量构建通过；binary XML 6/6（74 assertions，含本地 exact），
  `Resources.getXml` 1/1（35 assertions），core/android catalog 契约通过。关闭 survey 的
  clean-sandbox PvZ 短跑先被既有 `java.util.Observer` 层级缺口阻断，未到 XML 路径。
- 2026-08-30 macOS release `ogplay_tests` 重建通过；Thread 28/28（133189 assertions），
  File 14/14（531 assertions），GC filter、无 VFS、timed/root/worker Clock 与 core/android
  intrinsic catalog 契约定向通过。
- 2026-08-29 macOS dev 受影响目标通过；GLES1 扩展 token 与真实 RGBA8 FBO 定向
  2/2 通过（119 assertions），相关 architecture 4/4 通过。A6 Release 手动步进进入
  主菜单并稳定到 frame 10932，无 guest fault。
- 2026-08-28 macOS `dev` 全量 CTest 1066/1066 通过。
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
