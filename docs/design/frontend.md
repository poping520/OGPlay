# 前端、游戏库与会话宿主

前端把同一运行内核提供给桌面 GUI、CLI 与 Android 入口。
共享 app 外观隔离内核类型，library 管导入、设置和实例，game 管实际运行。
各入口拥有自己的交互与呈现，兼容行为仍来自同一 session 和 runtime。
app 外观是面向入口的 C++ 公共接口，不是另一套运行内核。

可以跟随一次“导入 APK、修改尺寸、启动、退出后查看结果”理解设计。
导入与设置在游戏库领域完成，启动计划转成受检参数，游戏子进程持有运行租约，
结果报告回到库列表。这样 GUI 与脚本可以共享规则，
用户也不会因为换入口而得到不同沙盒或启动行为。

## 共享外观与可执行入口

```text
ogplay 桌面入口 ─→ GUI / 子进程管理 ─┐
ogplay-cli ─────→ 命令与 stdio ─────┼→ ogplay::app → library / game → session
Android SDL_main ──────────────────┘
```

ogplaycore 只导出同构建的 ogplay::app，公开类型使用 std/app 值或 Pimpl。
Pimpl 把实现细节留在库内，避免 EXE 直接依赖 runtime 头文件。
GUI 拥有 WebView、RPC、向导和子进程管理，CLI 拥有语法、stdio 与退出码，
RunGame 自行完成 SDL bootstrap、日志和运行租约，再进入 game。

[共享外观决定](../adr/development.md#adr-0138)让入口共用库领域和内核状态，
避免 EXE 又静态链接一份内核而产生两套全局对象。
代价是共享库与 EXE 需要同次构建、同工具链交付，公开 C++ 接口不承诺跨版本 ABI。
标准异常可跨同构建边界，SDL C callback 则先捕获，返回安全 C++ 边界后恢复。

## 导入得到的是安装实例

library 检视单体 APK，读取 Manifest、图标、名称、ABI 清单与精确 Profile 匹配结果。
package 是 Android 身份，installation id 是本地安装实例身份，二者分开。
同包副本因而可以有不同设置和存档，Profile 未命中也仍可通用启动。
Profile 当前只覆盖 identity，不提供入口序列或资源路径配方。

库条目保存导入事实、设置、可选库内数据包与最近运行报告，
sandbox 保存 guest 持久写入。移除卡片默认保留存档，明确请求删除存档时才处理对应沙盒。
导入数据包时“复制”和“引用”使用显式归属：复制得到库内自有数据，移除条目可以清理；
引用保留外部素材，删除条目只移除关系，原始素材不随卡片删除。
这样用户选择直接决定数据寿命，而不是由路径碰巧在什么目录下推断删除权限。

ClearSaves 清空覆盖层但保留 meta、实例身份、APK、设置与日志。
同包重导入可以沿用符合条件的残留实例，
用户数据的生命周期因此不依赖卡片是否存在。
库与沙盒的实例分配、残留识别和删除检查因此要共享规则，
依据见[安装实例决定](../adr/session.md#adr-0020)。

## 为什么使用两级锁

库根锁保护导入、移除和设置等共享元数据，实例运行锁表达某个游戏是否真实运行。
运行租约是持有实例锁的一段生命周期，从 RunGame 开始持续到返回或异常。
租约归运行中的游戏进程，启动器关闭不会结束它；游戏还在运行时，
真实实例锁阻止另一个入口删除或重复占用。
删除检查与启动获取使用同一 OS 文件锁机制，
而不是只看列表缓存、GUI 内存中的子进程列表或上次写入的 PID。

查询使用共享锁或有界尝试，竞争返回 busy，不能把暂时忙碌归类为条目损坏。
锁文件保持稳定，不主动 unlink；否则已有进程仍锁着旧文件，
另一个进程可能建立同名新文件并取得第二把互不相见的锁。
精确互斥与等待规则见 [library 契约](../../src/frontend/library/MODULE.md)。

导入在发布前保持暂存，失败只清理本次新建内容。
后台 job 和快照归服务 owner，也就是负责停止与回收任务的对象，不 detach。
取消因此需要与发布阶段协调，已经完成发布的导入仍要返回真实结果。
这样多窗口与 CLI 并用时不会制造半个库条目。

## 设置怎样形成唯一启动计划

共享 schema 定义游戏、全局和默认设置，按此顺序解析有效值。
读取可跳过兼容范围内未知键，写请求仍使用 closed schema，只接受声明字段。
schema 与继承由 library 维护，GUI 和 CLI 共用。

GUI RPC 用 revision 表示读取时的配置视图，保存前核对它，防止旧窗口覆盖新设置。
设想两个启动器窗口同时编辑同一实例：两者最初读取相同 revision，其中一个先保存，
另一个再提交旧 revision 时，GUI RPC 拒绝这次旧写入。
CLI 共用 schema，并在库锁内修改设置，不使用 GUI 的 revision 协议。

```text
GUI 编辑 / CLI 参数 → 共享 schema 校验 → GUI revision / 库锁
                                              ↓
                                    ResolveLaunch
                                              ↓
                        FormatGameRunArgs / RunGame → session
```

ResolveLaunch 生成受检运行选项，FormatGameRunArgs 是统一参数格式化入口。
GUI 预览与真实启动都使用这条路径，解析和格式化保持往返。
例如用户修改密度后，预览、子进程参数与直接 CLI 调用都经过相同范围校验，
避免预览可行而真实启动另有默认值。

显示宽高与 density 是 guest 事实，超采样影响默认 framebuffer 的存储，
宿主帧率上限控制帧节奏。继承尺寸可在建窗前按 Manifest 方向调整，
显式游戏尺寸保留来源，避免重复交换宽高。
继承屏幕尺寸和显式游戏尺寸即使数值相同，方向调整策略也可能不同，
所以从 schema 到 RunGame 的每一步保留值与来源，不在前端压成一组无来源数字。
这些设置不改变 Android display refreshRate 的声明，
理由见[Profile 与用户设置决定](../adr/session.md#adr-0142)。

外部数据来源与挂载位置也属于用户设置。
默认挂到 sdcard，合法子目录由显式配置决定，OBB 保留标准路径和原文件名。
失败运行的缺失路径可形成挂载建议，但仅有可选文件探测失败不意味着数据挂错。
这种建议在退出后由用户决定是否应用，不在运行中自动重挂。

## 桌面 GUI 为什么使用系统 WebView

Windows 使用 WebView2，macOS 使用 WKWebView，平台类型留在 HAL。
启动器加载 bundled webui，即随包静态页面；运行时不需要 Node 或打包浏览器。
[Web UI 决定](../adr/development.md#adr-0072)把 Node 限于固定构建链，
代价是发行前要生成并校验静态产物，并依赖宿主系统 WebView 能力。

启动器限制导航、弹窗与桥接，RPC 以闭合 schema 传递领域事实和实例身份。
导入向导先分析独立快照，再选择数据包与实例策略，
分块上传核对顺序和长度，Manifest 损坏阻止发布，图标或名称失败可使用明确占位。
界面负责表达错误，兼容性判断不进入页面脚本。

C++ 返回状态码和结构化模型，文案在前端翻译。
宿主界面语言与 guest Locale 分开，
切换启动器语言不会改变游戏资源选择。
这种分离多了一层模型映射，却避免界面从整句错误文本中解析业务状态。

## 游戏子进程与结果报告

桌面通过统一启动计划运行当前 EXE 的 `--run-game` 模式。
启动器跟踪 PID、实例、端口和退出码并非阻塞回收，
退出后读取受限日志尾部；启动器自身退出解除跟踪，不擅自杀死仍运行的游戏。
独立子进程让游戏窗口与启动器交互分开，结果与控制则需要显式协议。

last-run.json 保存结构化运行结局，由游戏子进程写入，启动器在退出后重新读取。
closed/exited 与 startup_failed/fault/crashed/no_frame 区分，
待排查分类由条目损坏、数据缺失与失败事实产生，
不从日志中猜“是否兼容”，也不依赖启动器一直在线监听全部日志。
退出码与运行报告分别保留原始事实，报告读取失败不会把一个正常库条目标记成 APK 损坏。
报告用于整理库和提出挂载建议，不能代替 title gate。

运行锁比最近报告更能说明进程是否仍活跃；报告则描述已结束运行的结果。
两者用途分开，使旧失败报告不会阻止正常启动，也不会被当成当前进程状态。
格式与分类以 [library 契约](../../src/frontend/library/MODULE.md)为准。

## CLI 与独立探针复用什么

ogplay-cli 的 library 命令使用共享领域服务，library run 在当前进程执行 RunGame，
持有相同运行锁。JSON 结果与状态行分流，参数错误、guest 退出和运行故障保留不同退出码。
这让脚本能够按结构化事实处理结果，避免解析 GUI 专用流程。

run-apk 装配通用 APK 进程，preflight 只检查所选 ABI 的依赖与导入而不执行 guest。
`dexvm dump-surface` 导出实际 VM 绑定，不运行类初始化或图形；
`dexvm run-dex` 提供独立 main 探针，复用生产 native/JNI 与启动类路径。
这些入口复用能力但回答不同问题，静态可解析不代表运行行为已验证。

run-dex 将 Java stdout/stderr 与宿主诊断分开。
一致性工具可以按 oracle 约定合并 Java 两路输出，
宿主错误仍留在诊断通道，避免污染 expected.txt 比较。
探针通过只说明对应行为，操作入口见[工具说明](../../tools/README.md)。

## 会话循环、时间与呈现

game 取得随附 payload，创建 SDL3 窗口并编排 session。
宿主事件进入输入，Clock 驱动 step，完成帧经 HAL 呈现。
实时 step 按宿主经过时间推进 guest uptime，帧率上限只安排帧节奏；
手动 step 则每次固定推进一帧，等待时仍处理宿主事件。
当前实时推进规则见 [session 契约](../../src/session/MODULE.md)。

这使低帧率限制不会同时放慢 Android 时间，一次 step 的时间增量也就不必等于固定帧长。
手动模式只在许可时推进，异步读回可以在暂停时交付最后完成帧，
但这不触发 guest 工作或 Clock 增长。
统一 Clock 的理由见[线程与时钟决定](../adr/runtime.md#adr-0004)。

源帧序号与成功 present 次数分别统计。
前者可以跳号，FPS 使用实际成功 present 数。
宿主关闭、帧预算、MCP shutdown、guest exit 和 fault 也分别保存，
首次呈现前结束还需表达启动失败，避免把“进程退出了”统一显示为运行成功。

## Dashboard 与按键编辑器

Dashboard 使用游戏进程 loopback 服务的只读 dash.* 路由。
启动器核对 schema 和 PID，避免端口复用后连到其他进程；
辅助页面不拥有启动器 RPC，同实例复用监控窗口，游戏退出移除入口。
诊断事实来自内核快照，详见[可观测性设计](diagnostics-testing.md)。

按键编辑器能冻结、保存和应用配置，授权和执行语义与只读的 Dashboard 不同，
所以它使用独立 control.* 通道和随机令牌，令牌通过环境与页面会话传递而非 URL。
网络线程排队 freeze/apply/resume，主循环执行并返回结果，
服务忙碌时返回失败或等待结果，不会把排队确认冒充已经应用。
这样“查看状态”不会隐式改变运行，只负责监控的页面也不会获得导入、移除或任意宿主路径操作；
GUI 则需要分别核对各页面的 schema、目标进程和就绪条件。映射模型见[输入设计](input.md)。

## 平台与交付边界

共享内核与系统 WebView 的接缝需要各平台分别验证。
Linux GUI 未接入，macOS 与 Windows 部分交互仍缺 App 内实际点击复核，
完整范围见[前端已知问题](../state/KNOWN-ISSUES.md#title-validation)。
共享参数或离线库测试通过不自动构成原生 GUI 验收。

## 契约、决定与操作

- [frontend](../../src/frontend/MODULE.md)、[app](../../src/frontend/app/MODULE.md)
- [library](../../src/frontend/library/MODULE.md)、[game](../../src/frontend/game/MODULE.md)
- [GUI](../../src/frontend/gui/MODULE.md)、[CLI](../../src/frontend/cli/MODULE.md)
- [desktop](../../src/frontend/desktop/MODULE.md)
- [交付与依赖决定](../adr/development.md)、[会话决定](../adr/session.md)
- [Dashboard 手册](../playbook/DASHBOARD.md)、[已知问题](../state/KNOWN-ISSUES.md)
