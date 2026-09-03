# 当前状态

更新：运行入口自动使用内置 Android 4.4.4/API 19 guest 系统库

## 当前阶段

- **API 19 Android guest 系统库发行集已内置**：`data/android/19/lib` 收录从 AOSP
  `android-4.4.4_r2.0.1` clean tag、`aosp_arm-user` 构建的 libc/libm/libdl/libstdc++/libz；
  精确源码 revision、构建记录、文件哈希、ELF 身份和逐库 NOTICE 随包保存。构建目录、
  安装目录与 macOS bundle 均通过统一 data staging 携带该发行集；`run-apk` 按 Profile
  API 自动选取，不再接受 `--system-dir`，GUI 也不再保存或呈现外部系统库设置；API
  22/23 尚未纳入。
- **GUI-17/18 双栏主界面已完成**：主面板改为左侧列表、右侧详情，单击选择，ready
  条目双击或详情按钮启动；Profile、external 两项 title 条件明确呈现。系统库属于完整
  bundled runtime，不再作为逐条目设置；选择刷新稳定、删除相邻回退且不落盘。没有加入搜索、筛选、替换导入、
  常驻日志或游戏设置占位控件。
- **缺 Profile 可通用启动**：仍显示橙色未匹配和 external 无法判断，条目未运行时允许
  进入既有 generic `run-apk`；不把可尝试启动表述为已验证兼容。
- **DVM-87 SimpleDateFormat 最小层级已增加**：core catalog 发布抽象 `Format`、抽象
  `DateFormat` 与具体 `SimpleDateFormat`，严格保持 API 19 父链；`Format` 同时实现
  Serializable/Cloneable。指定 pattern/Locale 构造器校验参数并保存 pattern；format/parse
  仍未实现。沿用 DVM-87、未新建 WU。
- **DVM-79 File streams 公共 API 已补齐（NIO 除外）**：File/String/FD 构造、读写、
  available/skip/flush/close 与共享 FD 游标/所有权已闭合；Input/OutputStream 抽象契约及
  ByteArray/Filter/Buffered/Data 子类已校准。`getChannel` 依赖完整 FileChannel，继续 deferred。
- **DVM-18 Resources XML 补充已完成**：`Resources.getXml(int)` 经唯一 ARSC→sealed APK→
  strict AXML 链发布 `XmlResourceParser`，闭合 `getEventType/next/getName/getText/close`；
  缺失/畸形资源抛 `Resources.NotFoundException`。合成 APK 与本地 exact 两份 `res/xml`
  交叉验证通过，沿用 DVM-18、未新建 WU；完整 AttributeSet/XmlPullParser 长尾 deferred。
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

- 2026-09-03 Windows `windows-msvc` Release 的 `ogplay_tests`/`ogplay-gui` 受影响目标构建
  通过；内置 Android 库解析、GUI 配置迁移/LaunchPlan/视图、CLI 参数、GUI 实际冒烟、
  payload 校验与 scenario runner 自检 21/21 通过。
- 2026-09-03 Windows `windows-msvc` Release 的 `ogplay_tests`/`ogplay-gui` 受影响目标构建
  通过；API 19 Android guest 系统库、profile 路径与构建暂存测试 3/3 通过，临时
  `cmake --install` 产物再次通过五库、哈希、ELF、来源与 NOTICE 完整性校验。
- 2026-08-31 Windows `windows-msvc` Release 的 `ogplay-gui`/`ogplay_tests` 受影响目标构建
  通过；GUI 模型 7/7（61 assertions）、设置 1/1、LaunchPlan 2/2、删除 1/1、popup FIFO
  1/1 及 GUI options/空库/非空库 ANGLE 3/3 通过。1280×720 真实窗口完成两轮视觉检查。
- 2026-08-30 macOS release `ogplay_tests` 增量构建通过；DVM-87 6/6（85 assertions）与
  core catalog 1/1（475 assertions）通过，覆盖 SimpleDateFormat 父链和 pattern 构造。
- 2026-08-30 macOS release `ogplay_tests` 增量构建通过；File/InputStream 23/23（936 assertions）及
  core/java.io 契约 2/2（496 assertions）通过，覆盖 API shape、FD 游标与借用关闭。
- 2026-08-30 macOS release 增量构建通过；binary XML 6/6（74 assertions，含本地 exact），
  `Resources.getXml` 1/1（35 assertions），core/android catalog 契约通过。关闭 survey 的
  clean-sandbox PvZ 短跑先被既有 `java.util.Observer` 层级缺口阻断，未到 XML 路径。
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
