# 会话、Profile 与持久沙盒

返回 [ADR 索引](README.md)。本文件按编号保留决策沿革；后续记录的 `Supersedes`
只替代其明确指出的旧条款，其余结论继续有效。

- [ADR-0020 · 每游戏持久沙盒（可写命名空间跨会话持久化）](#adr-0020)
- [ADR-0022 · Profile 启动作用域与 v2-only 迁移](#adr-0022)
- [ADR-0071 · VFS backing、资源 lease 与安装实例沙盒](#adr-0071)
- [ADR-0074 · CLI 数据输入独立于 Profile 挂载声明](#adr-0074)
- [ADR-0086 · 跨宿主输入与 Android guest 快照](#adr-0086)
- [ADR-0133 · 通用操控原语与分级操控配置](#adr-0133)
- [ADR-0134 · 按键编辑器由游戏进程提供、GUI 打开，键位由用户配置](#adr-0134)
- [ADR-0141 · Title Profile 仅保留 quirk 与输入模板的精确身份覆盖](#adr-0141)
- [ADR-0142 · 输入与显示归用户数据，Profile 只含 identity 与 quirks](#adr-0142)
- [ADR-0143 · 外部数据挂载归安装实例，cwd 与看门狗归通用实现](#adr-0143)
- [ADR-0144 · 导入的数据包默认归游戏库所有，引用原目录为显式选项](#adr-0144)
- [ADR-0145 · 重新导入沿用残留存档的安装 id](#adr-0145)

<a id="adr-0020"></a>

## ADR-0020 · 每游戏持久沙盒（可写命名空间跨会话持久化）

- 状态：Accepted（2026-08-12 评审通过并启动实施；任务单见
  [`docs/tasks/sandbox/`](../tasks/sandbox/README.md)）
- 日期：2026-08-12
- 关联：[docs/design/sandbox/](../design/sandbox/README.md)（方案设计全文）、
  [roadmap 06 · §2](../roadmap/06-user-experience.md)（"每个游戏一个独立的虚拟
  sdcard，存档互不干扰"）、[roadmap 01 · §4.6](../roadmap/01-architecture.md)
- Supersedes：`src/runtime/vfs/MODULE.md` 中 "external 修改只存在于会话内，
  不反写宿主目录" 的契约条款，以及 `docs/tasks/m5/WU-0301.md` 的
  "不持久化 guest 修改" 非目标。二者在当时是正确的范围裁剪，本 ADR 将其
  升级为受控持久化；实施时同步修订 MODULE.md。

### 背景

游戏存档不保存，每次启动都是全新状态。根因不是缺陷而是既有设计：guest 的三条
写入通道全部止于进程内存——

1. native `open/write` 经 syscall 进入 `VirtualFileSystem`，写入内存节点，
   契约明确 "external 修改只存在于会话内，不反写宿主目录"；
2. DexVM 的 `FileOutputStream`/`FileWriter` 落在 `DexVmAndroidContext::memory_files`
   会话 map；`File.mkdirs` 恒返回成功但不建目录，属伪成功，违反记账原则；
3. `SharedPreferences`（framework HLE 与 DexVM 两条线）均为进程内 map，
   从不落盘。

同时 `mkdir/unlink/stat64/getdents64/rename` 等存档流程必经的文件 syscall
仍走默认 `-ENOSYS`，真实游戏的存档代码路径尚未被完整接住。

### 决定

- 每个游戏获得一个以 **package name 为键**的宿主沙盒目录（默认位于
  [roadmap 08](../roadmap/08-naming.md) 约定的用户数据目录下，CLI 可覆盖）。
  guest 的**可写命名空间**（`/data/data/<package>/`、`/sdcard/` 及其别名）
  由该目录持久背衬：写入跨会话保留，下次启动原样可读。
- 采用**文件粒度 overlay**：APK/OBB/external 保持只读原地懒挂载（不复制大文件，
  维持 roadmap 06 §2 与现有 lazy backing 架构不变），guest 写入进入沙盒覆盖层；
  读取按 覆盖层 → 只读底层 顺序解析，删除以 tombstone 表达。
- 持久化实现在 **`runtime/vfs` 一层**（新增 `SandboxStore` 宿主存储 +
  VFS 挂接），syscall/framework/dexvm 三条写入通道全部收敛到同一 VFS，
  不在上层各自造持久化：DexVM `memory_files` 废除改走 VFS descriptor，
  `SharedPreferences` 持久化为 guest 可见的 `shared_prefs/*.xml` 文件。
- 落盘语义为**确定性 flush 点 + 同目录临时文件原子替换**（close/fsync/
  lifecycle pause/clean shutdown）；宿主 IO 失败向 guest 返回真实 errno
  并结构化记账，禁止伪造成功。
- 沙盒内容属用户数据，**不入库**；自动化（scenario runner、CTest）默认使用
  一次性临时沙盒，保证既有 golden gate 的确定性不被持久状态破坏。
- `src/` 保持零游戏名分支：沙盒键、可写根、配额均为通用机制或 Profile 纯数据。

### 后果

- "存档不保存" 结构性消失；存档随沙盒目录可整体备份/迁移，为 roadmap 07 的
  "存档管理" 功能提供存储基础（即时存档/快照仍属独立 backlog，不在本 ADR）。
- `runtime/vfs` 职责从纯内存会话文件系统扩展为 "内存语义 + 受控宿主持久"，
  模块契约、`capabilities.toml` 新增条目与既有测试需按设计文档同步演进；
  `runtime.vfs` 等既有条目状态不后退。
- 文件 syscall 缺口（mkdir/unlink/rename/stat64/getdents64/ftruncate/fsync 等）
  必须补齐为真实语义，DexVM `File.mkdirs` 伪成功被消除。
- 引入宿主文件名兼容面（Windows 保留名、大小写、路径长度）与沙盒配额两类
  新失败模式，全部按 "明确失败 + 记账" 处理，见设计文档 03 章。

<a id="adr-0022"></a>

## ADR-0022 · Profile 启动作用域与 v2-only 迁移

- 状态：Accepted
- 日期：2026-08-12
- 关联：[启动作用域裁剪设计](../design/entry-scope/README.md)、
  [ADR-0017](dexvm.md#adr-0017)
- Supersedes：ADR-0017 中“Profile v1 冻结并与 v2 并行”的过渡决定，以及
  Title Profile v1 的 `native_call` / `[[java.class]]` 人工重放路线。

### 背景

DexVM 已用 Asphalt 5 exact gate 证明：解释执行真实 Activity 生命周期可以完全替代
Profile v1 的手工 JNI 调用序列和 Java handler 映射。继续保留 v1 会形成两套启动语义，
并诱导新 title 通过补商业外壳 Java 面而不是收敛到游戏引擎作用域。

Asphalt 6 的 manifest launcher 会转入首启下载、DRM、推送和分析外壳；在 external
数据已经 provisioned 时，这条 store 路径不是游戏引擎运行所需的作用域。启动作用域
必须由通用机制表达，游戏差异只进入 `data/profiles/`，且数据前提不成立时明确失败。

### 决定

- Title Profile 只接受 schema v2；删除 v1 schema、解析、手工 Java 装配、
  `native_call` 解析/解析符号/生命周期重放代码及其专属测试。
- 所有精确 Profile 使用 `dex_activity`，应用类与方法只来自 APK DEX；平台面只来自
  intrinsic 目录，缺失能力继续记账并明确失败。
- v2 增加可选 `[runtime.entry]`：`launch_activity` 覆盖 manifest launcher；
  `[[runtime.presets]]` 在目标类真实完成初始化后写入受检静态字段。每条 preset 必须
  提供非空 `reason`，且只允许基元或 `java.lang.String`。
- 声明 entry/preset 的 Profile 必须有至少一个 required data manifest 事实；run-apk
  在启动 DexVM 前经统一 VFS 验证这些事实。缺失时不实例化 Activity。
- 方法中性化不是本 ADR 的首批出口；只有 exact 测试证明入口/事实预设仍不足，且方法
  符合设计中的非目标与返回类型红线时，才以独立 WU 引入。

### 后果

- Profile 启动语义只剩一条 DexVM 路线；旧 title 必须迁移到 v2 才能继续进入生产目录。
- 入口覆盖和预设属于结论级配置，必须有 schema 负例、运行时类/字段存在性检查及
  “关闭即失败”exact 证据；gap survey 仍仅是诊断工具。
- `src/` 不出现游戏名、厂商名或包名。Asphalt 6 的入口、字段和理由只存在于其 Profile。

<a id="adr-0071"></a>

## ADR-0071 · VFS backing、资源 lease 与安装实例沙盒

- 状态：Accepted
- 日期：2026-09-20
- 关联：[VFS 资源读取、寿命与安装实例沙盒](../design/vfs/README.md)
- Supersedes：ADR-0020 中“package name 为沙盒键”、`fs/` 镜像布局以及旧 schema
  可迁移的条款；ADR-0020 的 overlay、tombstone、配额、原子替换和 flush 语义继续有效。

### 背景

原 VFS 在首次读取时持全局锁物化整文件，媒体又各自复制编码源；同 package 多份安装
共享一个沙盒和设备身份。大资源、慢 backing、FD 捕获及多安装因此没有统一寿命边界。

### 决定

- 节点、打开状态与 backing 分离；定位读取和耗时校验/解压不持 VFS 全局索引锁。
  普通 FD 保持独立游标，资源 lease 固定节点版本和窗口。
- 宿主文件与 stored 归档按区间读取；压缩来源采用有界增量解压与总预算。完整性校验
  仍是发布归档窗口前的硬门槛，不能用局部读取跳过 CRC。
- 音频和视频只依赖消费者模块定义的窄读取接口，由 integration 适配同一 VFS lease；
  不再以宿主路径或整文件复制作为生产资源协议。
- library 与 sandbox 使用同一个安装 id。首份为 package，后续为 `-2`、`-3`；从两侧
  已占用编号并集取最小空缺，版本号不参与身份。
- 新沙盒 schema 只接受 `internal/`、`external/`、`obb/`、`sdcard/` 四根；旧 `fs/`、
  缺失或冲突 meta、新旧混合布局明确拒绝，不迁移、不删除、不降级为空沙盒。

### 后果

- 小窗口读取、慢来源隔离、关闭后的 lease 寿命和多安装数据隔离成为可测试契约。
- 可写源捕获需要受总预算约束的快照；压缩后退仍允许线性重启，本轮不承诺 mmap、WAL、
  跨进程锁或所有同步宿主 IO 可立即取消。

<a id="adr-0074"></a>

## ADR-0074 · CLI 数据输入独立于 Profile 挂载声明

- 状态：Accepted
- 日期：2026-09-24
- 关联：[VFS-04](../tasks/vfs/VFS-04.md)
- Supersedes：WU-0302 中“无 Profile external 声明时拒绝 `--external-dir`”的条款。

### 背景

`run-apk` 把宿主数据输入和游戏特殊 guest 路径合为 Profile mount。无匹配 Profile 时，
显式 `--external-dir` 和 `--obb` 因缺少声明被拒绝；OBB 还只以 ZIP 条目形式挂载，
guest 看不到标准路径下的原 `.obb` 文件。

### 决定

- `--external-dir` 无 Profile external 声明时挂到 `/sdcard`；有声明时沿用唯一声明路径，
  无声明时可用 `--external-guest-dir` 指定 `/sdcard` 内的 guest 根；required mount 与
  manifest 校验仍生效。
- `--obb` 始终按原文件名只读挂到 `/sdcard/Android/obb/<package>/`，通过宿主文件
  backing 定位读取。Profile OBB 声明额外挂载归档条目，required 规则不变。
- 原始输入挂载在沙盒 overlay 前完成；路径冲突和非法输入明确失败，不静默覆盖底层。

### 后果

无 Profile 的 APK 可使用显式外部目录和标准 OBB 路径。Profile 保留特殊数据布局职责；
本决定不为任意外部目录猜测游戏专属路径，也不扩大到多个 OBB 的 CLI 输入。

<a id="adr-0086"></a>

## ADR-0086 · 跨宿主输入与 Android guest 快照

- 状态：Accepted
- 日期：2026-09-30
- 关联：[BND-46](../tasks/boundary/BND-46.md)
- 范围：按用户明确要求，允许 Android/iOS 作为 OGPlay 宿主；替代原“禁止手机端运行能力”
  在宿主移植上的限制。guest 仍为 Android 游戏进程，不扩展为完整 Android 系统。

### 决定

SDL3 采集宿主输入，经 HAL 的平台无关事件和 input 的坐标/手势状态进入 session。
Android action/source、设备目录及时间单位投影属于 Android 适配层；Java 和 NDK
消费同一不可变快照。宿主 Android 对象或 UIKit 对象均不得直接传给 guest。

队列拥有 pending/inflight 事件；opaque guest 句柄不复用，finish 或队列退役使事件失效。
NDK getter 完整读取快照，不读取实时设备状态。ARM32 的 int64 返回必须同时写 r0/r1。
宿主事件泵保留在 SDL 所需线程，guest UI/GLThread 按现有独立线程契约消费事件。

### 验证边界

移动输入适配不等于移动运行时、图形、打包或整机兼容完成。Windows 定向测试、游戏
交互复现和 Android/iOS 真机验收分别记账；不得互相替代。

<a id="adr-0133"></a>

## ADR-0133 · 通用操控原语与分级操控配置

- 状态：Accepted
- 日期：2026-10-13
- 关联：[Input 设计](../design/input/README.md)、[ADR-0086](#adr-0086)
- 范围：键盘、鼠标、手柄到 guest 触屏/按键（及预留传感器）输入的映射层；不改变
  ADR-0086 确定的 Android 输入投影。

### 背景

OGPlay 的目标游戏几乎都为手机触屏设计。现有输入链路只能把鼠标主键或一个宿主触摸
来源转为触摸，其余物理键直通为 Android KeyEvent；手柄事件无人消费，MCP 手势与本地
触摸互斥，Profile 只有一个未被使用的模板 id。逐款编写映射代码会违反“`src/` 不含游戏
专属内容”的规则，也无法覆盖用户自行导入的游戏。

### 决定

1. **通用原语，游戏只给数据。** `src/input` 提供封闭的操控原语集合：`direct`、`button`、
   `stick`、`look`、`swipe`、`pinch`、`key`、`mode`，并预留 `tilt`。游戏差异只以区域、
   绑定和参数的数据形式存在，代码中不出现游戏坐标、键位或分支。新增原语需更新设计
   文档与 input MODULE。
2. **只经现有 guest 输入通道输出。** 原语按帧输出 `TouchSnapshot`、Android 按键和预留的
   传感器样本，经 session 现有投影进入 guest；不直接调用 guest 代码，不读取 guest 状态、
   内存或画面来驱动映射。
3. **统一触点分配。** 宿主触摸、鼠标、合成触点与 MCP 手势共用一个分配器，最多 16 个
   触点，存续期间 pointerId 稳定，溢出明确失败。取代当前 MCP 与本地触摸互斥的做法。
4. **帧对齐与统一取消。** 合成发生在统一 Clock 的 guest 帧边界；按下与抬起至少相隔一帧。
   `input_reset`、失焦、后台、Activity 切换与 surface 尺寸变化统一取消全部触点。
5. **四级配置与合并。** 内置默认（鼠标直通、滚轮缩放、Esc→Back、其余键直通）→
   `data/input/templates` 类型模板 → Profile `[input]` → 用户覆盖；按“模式 + 输入源”
   覆盖，可显式解除绑定；同一模式内源冲突装载时失败。用户覆盖存放在安装实例目录的
   独立文件中，不并入 GUI 的 `settings.toml`。Profile 的 `input.profile` 字段由新的
   `[input]` 结构替代。
6. **直通保底。** 未被当前模式绑定的键继续作为物理键直通；鼠标左键默认始终是直接触摸，
   保证任何界面都可操作。
7. **坐标与布局。** 区域使用 guest 逻辑坐标，以九宫格锚点加偏移描述；装载时解析为
   绝对区域，越界明确失败。
8. **提示叠加层归 frontend。** 按键提示在 guest 画面合成后由宿主绘制，不进入 guest 帧和
   默认的 MCP 截图。

### 后果

- 新游戏的操控适配退化为选择模板并填写区域，用户也可在 GUI 中自行配置。
- hal 需要补充滚轮与相对鼠标接口；session 负责装载合并配置；Profile schema 与校验工具
  需要更新。
- 宏、脚本、自动瞄准和基于画面识别的映射不在本决定范围内，扩展前需新 ADR。
- 倾斜输入依赖虚拟加速度计。发布虚拟传感器会改变 guest 可见的设备事实，需另立 ADR；
  在此之前 `tilt` 只保留接口位置。
- 文本输入依赖输入法会话能力；落地后另行定义输入法激活期间的直通规则。

<a id="adr-0134"></a>

## ADR-0134 · 按键编辑器由游戏进程提供、GUI 打开，键位由用户配置

- 状态：Accepted
- 日期：2026-10-13
- 关联：[Input 设计](../design/input/README.md)、[ADR-0133](#adr-0133)、
  [ADR-0073](development.md#adr-0073)、[INP-4..7](../tasks/input/README.md)
- Supersedes：ADR-0133 决定 5 中“Profile 作为项目维护的游戏配置层”的部分；其余合并规则不变。

### 背景

键位与游戏版本、分辨率和个人习惯相关，项目逐款维护不可持续，玩家也不应手写配置。
最佳编辑体验需要真实游戏画面并即改即试；而 GUI 是 WebView 前端、游戏是独立的 SDL
进程，macOS 的 GUI WebView 宿主尚未接入。合并进程或跨进程嵌入窗口会破坏崩溃隔离、
多实例并行，且 macOS 不支持跨进程窗口嵌入。

### 决定

1. **键位由用户配置。** 项目只发布内置默认与类型模板，不随 Profile 发布具体游戏的区域和
   键位；Profile `[input]` 的解析保留供本地实验，不作为发布数据。合并顺序为
   内置默认 → 类型模板 → Profile（如有）→ 用户配置（实例目录 `input.toml`）。
2. **编辑器是 Web 应用，由游戏进程提供。** run-apk 在既有 loopback HTTP 服务上提供编辑器
   页面与 `control.*` JSON-RPC（读取配置与模板、冻结/恢复、取当前帧、校验并热应用与保存），
   沿用 Dashboard 模式。写操作需要每次启动生成的令牌。
3. **GUI 负责打开，不嵌入游戏。** GUI 在实例详情中以独立 WebView 窗口打开编辑器，与
   Dashboard 窗口相同：加载子进程的 loopback 页面，主 GUI 外壳仍只加载本地静态页面。
   GUI 与游戏保持独立进程，GUI 继续监管子进程。
4. **编辑时宿主冻结 guest。** 冻结只停止 guest 推进与输入投递，不触发 Android 生命周期回调；
   恢复后需重新按键才会产生触点。
5. **游戏内只保留轻量入口。** F1 切换宿主绘制的键位提示叠加层；F2 请求打开编辑器，有 GUI
   时由 GUI 打开，无 GUI 时用系统浏览器打开同一地址。F1、F2 及带 Cmd/Ctrl 的组合键保留给
   宿主，不传入 guest。
6. **第一版不含配置分享。** 导入导出与社区预设另行决定。

### 后果

- 编辑器与 GUI 共用 Web 技术栈；进程隔离与多实例不变；macOS 在 GUI 宿主就绪前可用浏览器
  编辑。
- 编辑需要游戏运行；离线编辑（保存参考截图）留待后续。
- loopback 服务新增写接口，需令牌鉴权并继续拒绝非 loopback Origin。
- 需要把 `ControlLayer` 写回 TOML、`ControlMapper` 运行中重载，以及宿主快捷键过滤。

<a id="adr-0141"></a>

## ADR-0141 · Title Profile 仅保留 quirk 与输入模板的精确身份覆盖

- 状态：Accepted
- 日期：2026-10-19
- 关联：[APS-15](../tasks/apk-startup/APS-15.md)
- Supersedes：ADR-0022 的 runtime/data/audio/entry/presets 与旧 schema 条款，
  ADR-0074 的 Profile 挂载与清单条款；输入合并及 ADR-0134 发布策略继续适用。

### 决定

Title Profile 重新定位为“仅 quirk + 输入模板的精确身份覆盖层”。v4 只包含
identity（package、可选 versionCode/SO SHA-256 约束）、quirks（enabled 与 parameters）、
input。移除 name、ABI 与全部 runtime/data/audio。只接受 v4，旧版本直接失败，
不迁移、不留兼容分支；身份未命中继续通用启动，交叠范围失败。

运行固定 API 19，ABI 交给 APK inventory resolver，入口由 Manifest MAIN/LAUNCHER
（含 alias）推导。逻辑表面默认 800×480，VM 与 guest call 使用各自默认值；
宿主显式运行选项继续表达用户控制，不从 Profile 注入预算或静态 Java 字段。

`--obb` 给出时按 Android 标准目录 `/sdcard/Android/obb/<package>/` 挂载原文件，
游戏自行解析；不再展开归档到 Profile 自定义路径。外部目录保留显式运行选项，
guest cwd 为 `/`；无资源清单预检或工作目录覆盖。音频只经标准 MediaPlayer /
SoundPool（以及既有标准底层音频 API），删除 Profile 封面音乐与资源编号映射。

### 后果

普通 APK 的名称、启动、ABI、资源与媒体行为回到标准能力。需要偏离 AOSP 的差异须作为
独立注册 quirk，并有“关闭即失败”的测试，禁止用资源预检或 Java 字段预置模拟安装状态。
前端只显示身份覆盖、quirk 与输入模板，不再声称 Profile 指定了必需数据包。
删除能力路径同步删除专用测试、CMake 源项和账本条目，标准能力回归继续保留。

<a id="adr-0142"></a>

## ADR-0142 · 输入与显示归用户数据，Profile 只含 identity 与 quirks

- 状态：Accepted
- 日期：2026-10-19
- 关联：[INP-8](../tasks/input/INP-8.md)
- Supersedes：ADR-0133 的类型模板/Profile 输入层与 ADR-0141 保留 input 的条款；
  ADR-0134 的用户键位与编辑器归属继续适用。

### 决定

Profile v4 只允许 identity + quirks，`[input]` 明确失败。输入与显示属于用户数据，
存放于库条目或用户设置中；应用不内置官方键位。删除类型模板、模板目录和目录路径、
ControlTemplate API、编辑器模板列表/选择/重置，不保留兼容分支。

输入仅“内置默认 + 用户 input.toml”两层。内置只含鼠标/触摸直通及 Esc→Back，
没有官方区域。编辑器从空布局开始，由用户添加并摆放区域、绑定按键；已有用户文件
重新加载。用户 schema 1 只允许 zone/binding，旧 template 字段在 TOML/JSON 中都报错。
库条目/input.toml、显式 --input-config、sandbox 旁 <installation_id>.input.toml 的
位置和优先级不变，保存及热应用沿用现有机制。

显示设置 surface_width/surface_height 默认 800×480，每边 1..4096；supersample 为
1..4。三项均由 EffectiveGameSetting 按“游戏 > 全局 > 默认”解析，经 GameRunOptions
和 CLI 对应参数传入同一运行服务。GUI 全局/游戏设置页编辑同一字段定义；零值、越界
和非整数明确失败。不新增窗口缩放、宽高比或其他偏好。

### 后果

Profile 只表达偏离 AOSP 的注册 quirk；输入/显示不随游戏身份覆盖或 bundled data 发布。
旧带 template 的用户文件须由用户自行修改，不静默忽略或迁移。旧 input_template 用户
设置同样按未知键明确失败；用户数据不由任务自动改写。删除专用模板代码和测试，保留
用户文件往返、RPC 保存/重载、设置继承及实际 F2 路径验证。

<a id="adr-0143"></a>

## ADR-0143 · 外部数据挂载归安装实例，cwd 与看门狗归通用实现

- 日期：2026-10-19
- 关联：[VFS-09](../tasks/vfs/VFS-09.md)，延续 ADR-0142/0074；Profile 仍只含 identity + quirks。

### 决定

external_guest_dir 是每个安装实例的用户数据，导入记录默认 /sdcard，游戏设置可覆盖；
GUI 导入与设置、ResolveLaunch 和 CLI 共用校验，仅允许 /sdcard 或其子树。
它只选择外部目录的 guest 挂载位置，不隐式改变 cwd，不扫描资源清单或修改宿主布局。
VFS 初始 cwd 固定 /，guest 的 chdir/fchdir/getcwd 操作同一进程状态；错误经 ARM syscall
返回真实 errno。沿用 VFS 的 ASCII case-fold 索引；宿主是否区分大小写不影响 guest 查找。

原生单次调用的 watchdog 默认值由底层常量统一为 100 亿 tick，session 不再缩减为
2 亿；它是有限的防挂死额度，不是 Android 设备事实，也不进入 Profile 或用户游戏设置。
已有 observable-progress 续期机制保持。改为仅按“进展”停机需要先定义可被信任的进展：
反复查询或无意义 IO 可能让死循环无限续期；墙钟还受宿主负载影响。此次保留已有机制和
有限计算额度，避免同时改变调度/取消协议；显式小预算继续供失败边界测试使用。

### 后果与边界

用户可以适配不同外部数据包布局，无需官方 Profile。目录 cwd 支持点段与父目录逐段
校验；非 cwd 的旧 traversal 限制不在此次扩张，任意 dirfd 的 *at 查找仍明确 ENOTSUP。
不恢复 Profile runtime/data/audio，不把表面尺寸或标题身份当作文件系统兼容条件。

<a id="adr-0144"></a>

## ADR-0144 · 导入的数据包默认归游戏库所有，引用原目录为显式选项

- 日期：2026-10-20
- 状态：Accepted
- 关联：[GUI-12](../tasks/gui/GUI-12.md)，延续 ADR-0143；挂载位置与只读语义不变。

### 背景

导入时 APK 复制入库，数据包只记录宿主绝对路径。原目录移动、删除或外置盘断开后游戏
无法启动，库也无法整体迁移，与“入库”的用户预期不一致。

### 决定

导入数据包有两种模式，默认 copy：

- copy：数据包复制到 `library/<id>/data/`，由库拥有；meta.toml 写 `external_data = "library"`，
  不记录来源路径。复制先在库锁外落到库根 `import-staging/`（每个暂存目录持有同名 OS 锁，
  无持有者的残留由后续导入清理），完成后在库锁内随条目原子发布。逐文件优先使用宿主
  copy-on-write 克隆（macOS clonefile、Linux FICLONE），不支持时回退流式复制；需要回退时
  先检查剩余空间。只接受普通文件与目录，符号链接等明确失败并提示改用引用模式。
  复制可报告字节进度并在发布前取消。
- link：保持 ADR-0143 前的行为，只记录绝对路径 `external_dir`；缺失时库状态与启动明确报错。
  无 `external_data` 键的旧 meta 均按 link 读取，不迁移。

不提供“移动”原目录。运行时两种模式都只读挂载到 external_guest_dir，写入仍进 sandbox
overlay；游戏设置里另选数据包目录即为 link 覆盖。移除条目会删除库内 `data/`，
保留沙盒存档与 link 模式的原目录。

### 后果与边界

导入后原始下载可删除；同卷 APFS 等支持克隆的文件系统几乎不额外占用空间，其他情况占用
一份完整副本。Windows 暂无克隆，回退流式复制。不支持 zip/obb 归档自动解压，
不做同包多份间的数据去重，也不提供已导入条目在两种模式间的转换。

<a id="adr-0145"></a>

## ADR-0145 · 重新导入沿用残留存档的安装 id

- 日期：2026-10-20
- 状态：Accepted
- 关联：[GUI-12](../tasks/gui/GUI-12.md)
- Supersedes：[ADR-0071](#adr-0071) 中“新安装总取 library 与 sandbox 并集最小空缺”的条款，
  以及 GUI 移除契约中“重新导入不自动挂接旧存档”。

### 背景

移除条目默认保留沙盒存档，而编号分配把残留沙盒视为占用，同一游戏每次“移除→导入”
都会得到 `-2`、`-3` 等新编号；保留下来的存档也无法通过界面找回。

### 决定

导入时先在库锁内查找残留存档：sandbox 下属于该 package 编号规则、没有同名库条目，且
SandboxStore::Open 校验 meta 中 package 一致的目录，取编号最小者直接作为新条目 id，不新建
沙盒。存档、ANDROID_ID 与 guest 身份随之恢复。meta 不符或缺失的目录不采用，也不改动。
没有可沿用存档时，仍按 ADR-0071 的并集最小空缺分配新 id 并原子创建沙盒。

移除新增可选“同时删除存档”（GUI 勾选、RPC `delete_saves`、CLI `--delete-saves`），默认保留。
删除时先删存档再删条目，失败时条目仍在可重试，不留下半删的存档供后续沿用。

### 后果与边界

“移除→导入”保持原 id 和存档，类似 Android 保留数据的卸载重装。另外添加的第二份若存在
残留存档，同样沿用编号最小的那份。不提供存档选择、合并或跨 package 迁移；版本号不参与。

