# DVM-90 · 动态 SurfaceView attach/detach 生命周期

## 目标（一句话）

让运行期加入或移除 live View 树的 `SurfaceView` 按 Android 4.4.4 语义获得独立的
SurfaceHolder generation，并准确分派 created/changed/destroyed 回调。

## 依赖

- DVM-18：dex_activity 与 managed surface 生命周期。
- DVM-89：稳定 SurfaceHolder、多 callback 与软件 Canvas 发布。
- LUI-3/LUI-5：guest View identity、UiTree attach 与布局尺寸事实。

## AOSP 4.4.4 依据

- `.local/aosp/framework/base/core/java/android/view/ViewGroup.java`：`addViewInner()` 仅在父
  ViewGroup 已有 `mAttachInfo` 时调用子树 `dispatchAttachedToWindow()`；`removeViewInternal()`
  对已 attach child 调用 `dispatchDetachedFromWindow()`。
- `.local/aosp/framework/base/core/java/android/view/SurfaceView.java`：attach 注册 traversal
  listener；可见且拿到有效 Surface 后先置 `mSurfaceCreated=true` 并依次回调
  `surfaceCreated`、`surfaceChanged`；detach 令 requested visibility 为 false，经
  `updateWindow()` 在回调前清除 `mSurfaceCreated` 并调用 `surfaceDestroyed`。
- `SurfaceView.getSurfaceCallbacks()` 在一次事件前取得 callback 数组快照；
  `SurfaceHolder.addCallback/removeCallback` 修改后续快照，重复 add 去重、remove 幂等。

OGPlay 不复制 ViewRootImpl、WindowSession 或 SurfaceControl；其等价边界是 live UiTree、
唯一 managed host surface 和每个 holder 的 active-generation 状态。

## 范围

- context 明确记录 managed surface 是否存在，以及当前 active holder 闭集。
- managed host surface 的 open/close 只由 Activity lifecycle 改变；动态子树 remove 仅结束
  该 holder generation，不能把进程/窗口级 surface 事实一并关闭。
- lifecycle 初次 created 只激活已连接到 live UiTree 的 holder；changed/destroyed 只分派给
  active holder，重复创建或销毁不重复回调。
- `ViewGroup.addView()` 在 attach 后遍历新增子树；父树 live 时为其中已有 holder 的
  SurfaceView 同步分派 created→changed，父树 detached 时不伪造事件。
- `getHolder()` 无论调用时机都先保留 view→holder identity；holder 是否 active 只在其
  SurfaceView 已接入 live UiTree 且 host surface open 时决定，避免构造时机决定后续命运。
- `removeView/removeViews` 在 UiTree detach 前遍历子树，对 active holder 分派 destroyed。
- 运行期 `Activity.setContentView` 替换同样走旧子树 detach、新子树 attach。
- callback 分派使用稳定快照；发布 API 19 `removeCallback`，null/未注册移除为无操作。
- 保留 view→holder 和 callback guest identity；generation 状态只记 holder handle，不建立
  第二套 Surface 或像素存储。
- 同一 live UiTree 同时作为无 listener 触摸 fallback 的命中事实：dirty 时先 layout，随后按
  reverse-Z、deepest-first 枚举 visible/enabled/attached View；实际 guest override 消费
  DOWN 后才捕获后续 MOVE/UP。普通叶子 View 采用 bounded MeasureSpec 默认尺寸，避免动态
  自定义 View 因 0x0 geometry 永远无法命中。

## 不做

- 不引入 ViewRootImpl、WindowManagerService、Binder、SurfaceFlinger 或多宿主窗口 surface。
- 不模拟独立 SurfaceView 合成层、Z-order、transparent region、fixed-size/format 重建。
- visibility、window visibility、format/size mutation 导致的 generation 重建留待真实 reached
  行为；本 WU 只闭合 attach/detach 与已有 managed surface 尺寸事实。
- 不加入游戏名、包名、调用栈或 title 专属分支。

## 故障可诊断性补强

本 WU 的 title 复跑曾只报告会话级 `Profile GLSurfaceView failed`，同时把 A32
stop reason、fault access/reason 和全部地址以无标签十进制输出；已解析的
`RegisterNatives` 目标失败时，调用层虽然持有 class/name/descriptor，也没有把它们带入
异常。对于运行时解密、无法直接静态符号化的 guest 映像，这不足以区分 Java 调用边界和
真实 native 故障点。

- `TryInvokeRegisteredNative` 的失败现在包含规范 JNI class、method、descriptor 与 guest
  process thread id；DexVM Java 工作线程失败同时包含 Java name 与稳定 thread record id。
- A32 非正常停止统一输出 execution state、stop reason、fault access/reason 的名字和数值，
  PC、fault、r0-r3/r12/SP/LR 使用固定宽度十六进制，并尽力附加 PC-8 起的 24 字节指令窗口。
  指令窗口不可读时保留其余结构化上下文，不以二次 memory fault 覆盖原错误。
- text 输出把 Java thread、JNI class/method/descriptor/thread/cause 和 A32
  stop/fault/thread/registers/code 分层缩进；Logger 对多行 field 只在 text sink 增加展示
  缩进，JSONL 保留原始字段值。
- SurfaceHolder created/changed/destroyed 属于 generation 事件，不使用自动日志限流；连续
  remove→add 时即使 callback 数量相同，也必须逐代留下可见记录。
- `run-apk` 的启动提示与最外层失败标签改为与组件无关的 `APK guest execution`；具体
  JNI/CPU 原因继续原样向上保留，不再把任意 guest 会话或故障误称为 GLSurfaceView
  profile。
- 本补强只改善 host-side failure report，不实现 guest 内存转储、解密映像符号化或异步
  stack unwind，也不改变 SurfaceView lifecycle 与 guest 执行语义。

## 验收

- 初始 lifecycle 不向 detached SurfaceView 投递回调。
- live parent 动态 add 触发一次 created/changed，尺寸等于 managed surface；remove 触发一次
  destroyed，重复 generation 不串扰。
- distinct replacement 的旧 holder 只收到一次 destroyed，新 view 在 `getHolder()` 后接入
  同一 live parent，必须收到 created→changed；全过程 host surface 保持 open。
- 先在 detached parent 中构造子树不会提前回调；整个 parent 接入 live tree 后递归激活。
- `removeCallback` 幂等，且移除后不接收后续 destroyed。
- Windows Debug 增量构建及 SurfaceHolder/ViewGroup/架构定向测试通过；按要求不跑全量测试。
- listener-free 深层 View、reverse-Z fallback、平台默认实现跳过、DOWN capture 与无参数
  嵌套层级测量均有机器可判定回归。
- 注册 JNI 目标故障测试必须机器判定 class.method descriptor、guest thread、具名+数值
  memory fault 和十六进制 PC；A32 runner 独立测试覆盖寄存器与指令窗口格式。

## 验证结果

- Windows Debug `ogplay`/`ogplay_tests` 增量构建通过。
- SurfaceHolder identity、初始 generation、动态子树 attach/detach、late callback 与既有
  ViewGroup mutation 定向 4/4、183 assertions 通过。
- architecture 定向 6/6 通过。
- 后续对报告所述 remove→add 路径加入逐 JNI/EGL 临时追踪后，确认第二 holder 实际已经收到
  created/changed，`LoaderGL.startGL()` 也完成 create-window-surface、make-current 与
  `getGL()`；此前正常日志缺少第二组事件，是相同文本被自动限流隐藏，并非 callback 缺失。
  因而“remove 把 host surface 永久置 false”和“新 holder 永不激活”不符合当前实现。
- context 字段改名为 `managed_host_surface_open`，明确其 lifecycle ownership；distinct
  remove→new view/getHolder/add 回归 1/1、90 assertions 通过，并机器判定新 holder active、
  host surface 全程 open、三代相同 created/changed/destroyed 日志均可见。
- Windows Release `ogplay`/`ogplay_tests` 增量构建通过；SurfaceHolder 定向 3/3 与
  architecture 6/6 通过。
- Java `getGL()` 返回后 Thread-2 仍在 `0x6045be18` 发生相同 NULL AddRef fault；这是独立的
  native/JNI 后续问题，本 WU 仅记录，未扩展范围处理。该运行不构成 title 进入游戏验收。
- 按要求未执行全量测试。

- 触摸修复前，Release 标题画面点击入口后画面与会话均无变化，复现了输入静默丢失。
- 修复后 Windows Debug `ogplay_tests` 定向构建通过；深层触摸与既有 listener/click 回归
  6/6、296 assertions 通过；用户使用最终 Release 实测标题入口可以正常点击。未将单点测试
  扩大为全量测试。

- 可诊断性/排版定向 7/7、52 assertions 通过；architecture 6/6 通过；Windows Debug
  `ogplay_tests` 与 Release `ogplay` 增量构建通过。
- 按给定 PVZ Release 命令复跑两次，均报告 `Thread-2 (id 2)`、
  `LoaderThread.runNative(Ljava/lang/String;Ljava/lang/String;)V`、guest thread 16384、
  `pc=0x6045be18`、`state=thumb`、`memory_fault(4)`、NULL `read(0)/unmapped(0)`、寄存器与
  指令窗口；外层标签为 `APK guest execution failed`。最终 text 输出按 Java→JNI→A32
  stop/fault/thread/registers/code 分层对齐。该结果验证诊断链，不表示 title 已进入游戏。

状态：已完成（报告内 lifecycle 粒度与可诊断性已补强；独立 native fault 仅记录）。
