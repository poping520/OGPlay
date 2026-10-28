# 会话、Profile 与持久沙盒

返回 [ADR 索引](README.md)。本文件只记录现行决定；旧文由 Git 保存。

- [ADR-0020 · 安装实例沙盒、资源 lease 与外部数据](#adr-0020)
- [ADR-0133 · 通用操控映射与游戏进程编辑器](#adr-0133)
- [ADR-0142 · Profile 仅 identity/quirks，输入与显示归用户](#adr-0142)
- [ADR-0163 · 受管 Activity 的窗口回调附着](#adr-0163)
- [ADR-0164 · 有界应用附加窗口](#adr-0164)
- [ADR-0165 · 宿主呈现帧与 Choreographer 时间基](#adr-0165)

<a id="adr-0020"></a>

## ADR-0020 · 安装实例沙盒、资源 lease 与外部数据

- 状态：Accepted
- 日期：2026-08-12

### 背景

存档要跨会话持久且多安装隔离，大资源也不能靠整文件复制和短期 FD 维持寿命。

### 决定

VFS 是 syscall、Java 文件、Preferences、数据库及媒体的唯一存储语义；APK/OBB/外部底层只读，
写入进入本安装实例 sandbox overlay，读取覆盖层优先，删除用 tombstone，不反写素材。
library 与 sandbox 使用同一 installation id，版本不参与身份；新安装先沿用无库条目且
meta package 匹配的最小残留沙盒，否则从两侧占用并集取 package、package-2 等最小空缺。
沙盒 schema 3 只接受 internal/external/obb/sdcard 四根和匹配 meta，每实例 ANDROID_ID 持久；
旧 fs/、错配/混合布局不迁移或降为空。语义根不可删除，缺根可重建并告警。
配额、文件名编码、大小写索引、原子替换与 flush 集中在 VFS/SandboxStore；真实 IO 失败传播。
node/backing/open-state 分离，耗时读取/CRC/解压不持全局索引锁，FD 各自游标；
lease 固定来源版本和窗口，独立于 Close/unlink/rename，可写捕获受统一快照预算约束。
音视频只经消费者定义的窄 ReadAt 接口使用同一 lease，不反查 host 路径或复制完整编码源。
external_guest_dir 属于实例用户数据，只允许 /sdcard 子树，不改变初始 cwd=/。
数据包导入默认 copy 入 library/<id>/data，先库锁外暂存，发布时加锁；link 为显式绝对路径引用。
只复制普通文件/目录，可用宿主 CoW 克隆，不支持时有界流式复制；旧记录按 link 读，不移动原目录。
移除默认保留沙盒与引用素材，库内数据随条目删除；显式 delete_saves 先删存档再删条目。
运行缺失路径报告只观察真实 ENOENT，不改变 guest 结果；库层比对实际数据包提出挂载建议，
GUI 在退出后提示，须用户确认才改实例设置，不在运行中自动重挂。
自动化使用显式临时沙盒保证隔离，持久沙盒是用户数据，不入版本库。

### 后果

存档、平台身份与资源寿命按实例隔离；不提供旧沙盒迁移、数据去重、自动解压或任意同步 IO 抢占。

<a id="adr-0133"></a>

## ADR-0133 · 通用操控映射与游戏进程编辑器

- 状态：Accepted
- 日期：2026-10-13

### 背景

游戏触屏布局与玩家习惯各异，需要通用映射和即改即试，不能逐游戏写代码或合并 GUI/游戏进程。

### 决定

src/input 提供已实现的 direct/button/stick/key/mode 原语，差异只用用户区域、绑定与参数表达。
原语只输出既有触摸/按键通道，不直接调 guest 或读取其内存、状态、画面来驱动映射。
宿主触摸、鼠标、合成与 MCP 共用至多 16 个稳定触点，溢出明确失败；合成 down/up 至少隔一帧。
坐标只换算一次，区域按 guest 逻辑坐标/锚点解析；冲突、未知区域和越界在装载时失败。
按统一 guest 帧边界输出，reset/失焦/后台/切换/尺寸改变闭合 CANCEL 与按键释放。
输入只有内置直通/Esc 与用户 input.toml 两层，具体格式和优先级按 ADR-0142。
编辑器由游戏进程 loopback 服务提供，GUI 打开独立 WebView，无 GUI 时系统浏览器打开。
写 RPC 使用每次启动令牌并校验 loopback Origin；GUI 不跨进程嵌入游戏窗口。
编辑冻结 guest 推进与输入，不合成 Android pause/resume，恢复后重新按键才产生触点。
F1 提示由 frontend 在 guest 合成后绘制，F2 请求编辑；保留快捷键不传 guest，默认截图不含提示。
当前不提供宏/脚本/自动瞄准、画面识别映射、虚拟传感器、配置分享或完整输入法会话。

### 后果

键位由用户维护，编辑依赖游戏运行；未实现 look/swipe/pinch/tilt 不应因旧规划获得支持信用。

<a id="adr-0142"></a>

## ADR-0142 · Profile 仅 identity/quirks，输入与显示归用户

- 状态：Accepted
- 日期：2026-10-19

### 背景

入口、资源、预算与个人键位混入精确游戏 Profile 会使通用启动依赖项目维护的游戏配方。

### 决定

只接受 Profile v4，root 除 schema 外仅 identity 与 quirks；按 package 和可选 versionCode/SO hash
精确匹配，不命中走 generic，身份范围交叠失败。旧 schema、input 和未知字段明确拒绝，不迁移。
API19、VM/调用默认预算归通用实现；入口取 Manifest MAIN/LAUNCHER（含 alias），ABI 取 APK inventory。
不恢复 native_call、Java handler 映射、入口/静态字段预置、资源清单预检或 Profile 音乐映射。
OBB 按原文件名只读挂 Android 标准目录，不展开到 Profile 自定义路径；external 挂载由显式实例/
运行选项表达，cwd 固定初始 /，不依赖 Profile。音频只经标准 guest API。
偏离 AOSP 的行为必须注册 quirk 并有“关闭即失败”测试，src 中不含精确游戏分支。
输入只合并内置鼠标/触摸直通、Esc→Back 与用户 input.toml，不提供类型模板或官方游戏键位。
用户 schema 1 只接受 zone/binding；template/input_template 明确失败，不静默忽略或改用户文件。
库实例 input.toml、显式 --input-config 与 sandbox 旁实例配置按既有解析入口选择并保存/热应用。
显示 surface_width/height 默认 800×480、每边 1..4096，supersample 1..4；按游戏 > 全局 > 默认
由共享设置与运行选项解析，GUI/CLI 共用校验，不从 Profile 注入。

### 后果

无 Profile APK 仍可通用启动；旧配置须用户自行修改，发布 quirk 不等于发布操控或显示偏好。

<a id="adr-0163"></a>

## ADR-0163 · 受管 Activity 的窗口回调附着

- 状态：Accepted
- 日期：2026-10-27

### 背景

API19 主窗口在 resume 后首次 traversal 通知 Window.Callback 附着，销毁 Activity 后
通知分离；缺少此回调会遗漏应用在其中初始化的状态。

### 决定

session 以每个 ActivityRecord 的窗口附着位为唯一事实，onResume 返回后仅向未 finishing、
将可见的窗口虚派一次 onAttachedToWindow，先于首次窗口焦点。pause/stop/resume 或隐藏
不重复派发。实例退役在 performDestroy 后、UI 子树退役前向已附着窗口虚派一次
onDetachedFromWindow；进程关闭不合成分离。异常按既有生命周期失败协议传播。
只实现受管 Activity 的 Window.Callback 层，不构造 AttachInfo/ViewRootImpl，不向
View 树派发 dispatchAttachedToWindow；Dialog Callback 附着未支持。

### 后果

应用覆写可按主窗口生命周期初始化/释放状态；不因此扩大 View 附着、子窗口或动画支持范围。

<a id="adr-0164"></a>

## ADR-0164 · 有界应用附加窗口

- 状态：Accepted
- 日期：2026-10-27

### 背景

应用会用 Activity WindowManager 添加独立于内容树的全屏 View，需要独立投影、输入
与焦点，不能把它附加到 Activity 内容容器或套用 Dialog 的纯宿主树。

### 决定

Activity 拥有稳定 manager/app token，getWindowManager 与 WINDOW_SERVICE 同实例。
仅支持 type 1..99、MATCH_PARENT、flags NOT_FOCUSABLE/NOT_TOUCHABLE/NOT_TOUCH_MODAL/
KEEP_SCREEN_ON/LAYOUT_IN_SCREEN/FULLSCREEN；format 保留，其他尺寸/位置/type/flags
明确记账拒绝。与 Dialog 共用最多 8 个附加窗口名额。View 自身为稳定 Java 根，
mParent 保持 null，不构造额外 ViewGroup/ViewRootImpl/AttachInfo，不派发 View 树附着。
owner traversal 使用 ROM measure/layout，独立投影与主窗口一起发布到共同快照，按添加序
叠于 owner 主窗口之上；可见性随 owner，viewport/代次不完整不发布。输入按快照选择
最上层可触窗口，全屏阻断下层；按键只到焦点子窗口，未消费不回落。集合变化取消捕获并吞尾。
生命周期独占焦点写入：最上层可见且可获焦窗口胜出，先失焦后获焦；主窗口通知 Activity
及其根，附加窗口只通知根。销毁/Callback 分离后 closeAll 并 warn，进程关闭只退役资源。

### 后果

支持普通全屏应用附加窗口；系统/面板子窗口、透明 Activity、WMS/Binder 服务、Dialog
Callback 附着、窗口动画及属性动画均未因此实现。WU-3 单独推进。

<a id="adr-0165"></a>

## ADR-0165 · 宿主呈现帧与 Choreographer 时间基

- 状态：Accepted
- 日期：2026-10-27

### 背景

原版 Choreographer 的动画回调需要一次性 vsync；OGPlay 已有统一 Clock 和确定性 owner
遍历，不能引入宿主墙钟定时线程或依赖 Android 显示服务。

### 决定

保留 API19 ROM Choreographer/DisplayEventReceiver Java。三个 native 登记/请求/撤销
逻辑 token，无宿主指针；receiver 弱关联、Java/queued work 保活，dispose/GC/停止撤销。
每实际 StepFrame 在 guest owner traversal/projection 前向有请求的主 Looper receiver
投递一次 dispatchVsync 并泵送；请求先冻结/清除，回调中新请求延至下一宿主帧。
时间戳=统一 Clock uptime×1e6，与 System.nanoTime 同基；displayId=0、frame 单调。
MCP/测试单步每步最多一次；Start/Resume/输入后布局与只读呈现查询不额外派发。
不注入 debug.choreographer 属性、不使用 vsync=false 分支。非主 Looper 明确拒绝，
无 hotplug/系统同步屏障；异步消息只保存标志，普通 FIFO 与显式队首 LIFO 独立。
刷新率使用可注入逻辑显示事实（默认 60），无效明确失败；不读取宿主刷新率冒充 guest 时钟。

### 后果

可复用原版帧回调与动画调度协议，owner 遍历仍由 session 屏障负责；不因此实现属性动画、
View.setAlpha、完整 DisplayManager/WMS 或显示热插拔。WU-3b 复用此帧源。
