# 模块：frontend

## 职责

ogplaycore 聚合游戏运行宿主、共享游戏库与公共服务，只导出 `ogplay::app` C++ 外观；
ogplay 拥有桌面模式分派、GUI/RPC 和自身子进程管理，ogplay-cli 拥有命令呈现与 stdio。
只编排内核 API，把 Profile、窗口、输入、截图和会话控制交给 session/agent，不实现游戏兼容行为。

## 公共入口

- 实时 run-apk 启用能力受检的异步窗口读回；手动步进不启用。实时暂停/空闲时仍
  可呈现已完成末帧，不执行 guest step 或推进 Clock，FPS 只累积实际成功 present。
- `run-apk`：选择 compatibility Profile，挂载数据，创建 SDL3/ANGLE surface，装配 Android
  guest、DexVM 与 Activity lifecycle；VFS 使用 `/` 作为默认 guest cwd，Profile 显式
  working directory 可覆盖它。
  文件系统容量只由 guest VFS 的配额卷统计发布，前端不再将 external-dir 的宿主
  剩余空间写为无路径 StatFs 标量。
- `ogplay`：桌面主入口，默认 GUI；ready 条目只启动当前 EXE 的 `--run-game` 子进程，
  回收状态与退出码。`ogplay-cli run-apk` 与游戏模式共用选项解析和运行服务。
- `ogplaycore`：同次构建/同工具链的 C++ 外观，MSVC 统一 /MD，不承诺跨版本 ABI。
  运行归 game、游戏库归 library、外观装配归 app；GUI/CLI 的交互与呈现各归自己的 EXE。
  EXE 不静态链接内核；SDL 为共享动态库，WebView 仅桌面链接，不在加载 DLL 时初始化应用资源。
- `HostBundledDataPaths`：优先读取可执行文件同目录（macOS bundle Resources）的
  `profiles/`、`quirks.toml` 与 `framework/bootdex.jar`，源码树仅作开发回退。
- `--external-dir` 有 Profile external 声明时挂到其唯一 guest 根，否则挂到 `/sdcard`；
  无声明时可用 `--external-guest-dir` 指定 `/sdcard` 内的 guest 根。
  `/mnt/sdcard`、`/storage/emulated/0` 与 `/sdcard` 共用节点。`--obb` 将原文件只读挂到
  `/sdcard/Android/obb/<package>/<filename>`；Profile OBB 声明可额外挂载归档条目。
  `--supersample` 选择 1..4×；`--dexvm-interpreter` 覆盖 Profile，
  默认 `switch`。
- `--mcp`/`--mcp-port` 同时提供 `/dash/` 页面与 `/dash/rpc` 只读 Dashboard，复用端口。
  静态产物来自 bundled `data/webui/dashboard`；启动前只加载固定文件白名单，每文件最多
  2 MiB，worker 不解析宿主路径。缺失制品明确失败，路径变体、非 loopback Host/Origin
  与重复 Host/Origin 拒绝，不提供 CORS 授权。JSON-RPC 只分派 `dash.*`，控制仍走 `/mcp`。
- `run-apk` 在有 MCP 时连接会话、DiagnosticState、日志与账本；不开启诊断写盘协调器，
  除非显式 `--diag*`。连接 DexVM/JNI/CPU/memory/loader/VFS/GPU/AudioTrack/UiTree/VideoView 的 try-snapshot；
  FFmpeg 装配事实在启动时封存进 metadata，不在 HTTP 查询中初始化 decoder。
  HTTP server 作用域比 app_process 短，析构 stop/join 后才允许销毁诊断来源。
- `--mcp`/`--mcp-port` 提供本机服务；`--mcp-manual-step` 等待 step/suspend/resume/
  shutdown。`--diag*` 与 `ogplay-cli diag snapshot` 提供不依赖 SDL 主循环的停滞取证。

`run-apk` 默认使用持久沙盒（ADR-0020）；裸 APK 启动在没有实例时分配 package 首实例，
唯一已有实例自动复用，多实例则要求 `--installation-id` 消歧。GUI 始终传递库中选定的实例。
`--sandbox-dir` 与 `--ephemeral-sandbox` 互斥。打开失败必须终止，不降级为内存模式。持久/
临时沙盒分别保存/重建 CSPRNG `ANDROID_ID`，且不读取宿主设备身份。pause 与 clean stop 均
通过同一 VFS `FlushAll` 落盘。

## 不变量

- `run-apk --cpu-cores`（1..32）与 `--cpu-frequency-mhz`（1..10000）在进程创建前固定
  guest CPU 查询参数；默认 1 核/1000 MHz。GUI 单游戏配置通过相同 CLI 参数接入，主频
  仅为标称值，不改变 Clock/执行速度。全局设备预设和 CPU 名称仍未接入。
- `--cpu-limit-parallelism` 按虚拟核数限制同时执行的原生 guest CPU；
  `--cpu-max-mticks-per-second` 接受 1..10000，限制进程总 backend tick 速率。
  两项独立、默认关闭；GUI 对应速率值 0 表示省略限速参数。启动日志记录查询和执行配置。
  不限制 Java 解释/HLE、宿主 CPU 占用或物理频率，也不改变游戏 Clock。

- CLI/GUI 共用 session、Profile、quirk 与 bundled payload。Profile 决定 API 和入口；系统
  库依赖闭包不得由 CLI 手写。API 19 缺 `bootdex.jar` 与缺 ELF 同样在装配前失败。
  run-apk 读取固定 payload 的 `zoneinfo/tzdata` 并注入 session，缺失明确失败；来源、
  格式与哈希由 Android payload 校验入口维护，不使用宿主时区数据。
- 仅接受规范化的 `dex_activity` Profile。entry/presets 在生命周期前应用，required 数据先
  验证；`--external-dir` 最多一个。quirk 必须在 `data/quirks.toml` 注册并有测试引用。
- `--preflight`/`library run --mode preflight` 在所选 ABI 下逐个将 APK `.so` 作为静态根，
  复用 native 来源与 NEEDED/SONAME 闭包、Bionic boundary catalog 及 ELF 查找/版本规则；
  无关 APK 导出不能跨 scope 消除缺口。逐库输出 `preflight import`（binding=strong/weak）及
  `preflight imports: strong_missing=N weak_missing=N`，同步结构化日志。强缺口退出 1，
  仅弱缺口不失败；无缺口沿用 ready 输出。坏 ELF/缺依赖保持明确失败，全程不执行 guest。
- MCP 仅绑定 `127.0.0.1`，拒绝非 loopback Origin、错误路径/方法、chunked 或超限 body；
  控制请求在 worker 只排队。manual-step 必须启用 transport，且不能与 preflight 共用；无许可不得推进
  Clock、输入、frame 或 present。
- MCP 发布唯一 lifecycle/frame/ticks/presented-frame/movie/exit/fault 快照；fault 不冒充
  stop/success，截图只读已 present 的 RGBA8。`McpPointerDispatcher` 每个 guest step 把一个
  MCP 手势阶段作为外部触点交给 `ControlMapper`，与窗口鼠标、原生触摸和绑定按键触点并存。
  输入重置时取消 session 中的按住状态，静默清空 `ControlMapper` 并清空 MCP 当前和待处理
  手势；后台暂停仅在对应前台通知后自动恢复，不覆盖手动暂停。
- 操控映射（ADR-0133/0134）：`run-apk` 在启动前用 Profile `[input]`、用户键位文件与 payload
  `input/templates` 构建 `ControlConfig`，并按 Profile surface 校验区域；配置错误在 guest
  启动前明确失败。用户键位文件为 `--input-config <file>`，否则为持久沙盒根下的
  `<installation_id>.input.toml`，临时沙盒不落盘；文件不存在视为空布局。窗口事件全部交给
  `ControlMapper`，每个 guest step 前 `Flush` 并经 `session::MapControlOutput` 入队；呈现
  尺寸变化时更新 surface。GUI 总以 `--input-config <实例目录>/input.toml` 启动，该文件独立于
  `settings.toml`。
- 按键编辑器会话（ADR-0134）：loopback 服务存在时，`/controls/rpc` 提供
  `control.get/freeze/resume/frame/apply/status`，要求 `Authorization: Bearer <token>` 且 Host 为
  loopback；令牌取 `OGPLAY_CONTROL_TOKEN` 环境变量（由启动方提供，不进 argv），缺省时生成
  128 位随机令牌并打印。请求经 `ControlEditorChannel` 在主循环执行（`HandleControlRequest`）。
  冻结只停止 guest 推进与窗口输入投递并取消现有触点，不触发 Android 生命周期；`apply`
  校验失败返回 -32010 且运行布局不变，成功后热应用并原子写回用户键位文件。run-apk 的
  状态行写入后立即刷新 stdout。
- `/controls/` 提供编辑器静态页（`data/webui/controls`，与 Dashboard 同一白名单预载与 CSP），
  页面本身不需令牌、Host 须为 loopback；目录不存在时页面返回 404 并提示构建 webui，RPC
  不受影响。run-apk 打印 `/controls/#token=<令牌>`（令牌在片段中，不发往服务器）；令牌
  来自启动方时只打印地址。
- 编辑器服务来源：`--mcp`/`--mcp-port` 时共用 MCP 服务；`--key-editor-port <port>`（与 MCP
  互斥）启动只含 `/controls/` 的服务（`McpHttpServer::StartKeyEditor`，无 `/mcp`、`/dash/`）；
  都没有时游戏内首次 F2 在空闲端口懒启动。帧快照存储在每个窗口会话中存在，供 `control.frame`。
- 宿主快捷键（`HostShortcutFilter`）：F1、F2、Cmd/Win 及 Ctrl/Cmd 按住时按下的键由宿主截留，
  同一按键的重复与抬起跟随按下的归属（先松修饰键也不漏给 guest）；Ctrl 单独按下仍是普通键。
  F1 切换提示叠加层：`DrawControlHints` 在呈现副本上绘制 `ControlMapper::Hints` 的区域与键名
  （内置 5×7 ASCII 字形），guest 帧、MCP 截图与 `control.frame` 不含叠加层；切换在下一帧生效。
  F2：最近 3 秒内有启动器以 `control.status {client:"launcher"}` 轮询时计入 `edit_requests` 交给
  启动器打开，否则用默认浏览器打开 `/controls/#token=…`。
- 冻结看门狗：冻结期间若 8 秒内没有编辑器请求（启动器轮询不算），自动恢复并记 warn 日志。
- pointer 按最新 guest frame 与等比内容区映射；黑边不开始手势，既有 release 必须闭合。
  窗口 FPS 使用独立 `RealtimeClock`，不改变 guest Clock。
- 音频只消费已解析的 resid/APK/VFS 字节区间；解码归 audio，补充有界，退出停止设备。
- process exit、fault、shutdown 均走正常 teardown；native finalizer 先于 ANGLE surface 关闭。
  observer 只在 SDL 宿主线程泵事件，guest Java worker 不触碰窗口或共享进度。
  CLI 显式将外部 observer 限定到窗口宿主线程，后台普通 HLE 可走快路。
  等待 renderer 的生命周期回调复用同一 owner 检查和 Clock 节流泵；只收集 OS 消息，
  输入消费与生命周期命令仍留在帧循环，不从等待回调递归进入 guest。
- 日志保留 backend 来源、Profile、bootstrap、生命周期和原始 JNI/CPU 故障上下文。CLI 顶层
  每个失败只输出一个缩进的 `error [ogplay]` 块；运行循环仍发布 MCP `guest_fault`、清理并
  传播；运行首错在清理前立即写结构化日志，日志失败不得替换原错。
- 库的导入/移除/清理和库内配置读写使用 OS 排他锁；List 锁冲突跳过 `.importing` 清理。
  库启动显式带 library-root/installation-id，持有实例 OS 运行锁至游戏退出；移除与单实例以锁为准。
  GUI 只将 ResolveLaunch 的选项交给 FormatGameRunArgs，禁止另一份运行参数格式。
- GUI 子进程关闭 stdin、继承 stdout、stderr 覆盖写入 `last-run.log`；同 installation id 单实例，
  GUI 退出不杀游戏。存档固定在库根 `sandbox/`；删除条目不删除 external 或存档。CLI
  名为 `ogplay-cli`；GUI 不依赖 CLI 文件。所有用户可见失败同时写结构化日志并给出下一步。

## 禁止

- 不实现 syscall/JNI/GLES、游戏特判或仅 GUI 可用的内核行为。
- 不恢复 Profile 驱动的 Java registry、native phase 或 JNI 调用序列。

## 验证

CLI 参数、MCP transport 和会话控制由 CTest 覆盖；真实 APK 按 playbook 使用
`tools/run_scenario.py` 产出机器可判定证据。
