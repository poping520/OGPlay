# 模块：frontend

## 职责

提供 CLI、GUI 与可选 loopback MCP transport；只编排公共内核 API，把 Profile、窗口、
输入、截图和会话控制交给 session/agent，不实现游戏兼容行为。

## 公共入口

- `run-apk`：选择 compatibility Profile，挂载数据，创建 SDL3/ANGLE surface，装配 Android
  guest、DexVM 与 Activity lifecycle；VFS 使用 `/` 作为默认 guest cwd，Profile 显式
  working directory 可覆盖它。
- `ogplay-gui` / `ogplay gui`：共用 GUI shell；ready 条目只启动同目录 `run-apk`，回收状态
  与退出码。
- `HostBundledDataPaths`：优先读取可执行文件同目录（macOS bundle Resources）的
  `profiles/`、`quirks.toml` 与 `framework/bootdex.jar`，源码树仅作开发回退。
- `--external-dir` 有 Profile external 声明时挂到其唯一 guest 根，否则挂到 `/sdcard`；
  无声明时可用 `--external-guest-dir` 指定 `/sdcard` 内的 guest 根。
  `/storage/emulated/0` 与 `/sdcard` 共用节点。`--obb` 将原文件只读挂到
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
  shutdown。`--diag*` 与 `ogplay diag snapshot` 提供不依赖 SDL 主循环的停滞取证。

`run-apk` 默认使用持久沙盒（ADR-0020）；裸 APK 启动在没有实例时分配 package 首实例，
唯一已有实例自动复用，多实例则要求 `--installation-id` 消歧。GUI 始终传递库中选定的实例。
`--sandbox-dir` 与 `--ephemeral-sandbox` 互斥。打开失败必须终止，不降级为内存模式。持久/
临时沙盒分别保存/重建 CSPRNG `ANDROID_ID`，且不读取宿主设备身份。pause 与 clean stop 均
通过同一 VFS `FlushAll` 落盘。

## 不变量

- CLI/GUI 共用 session、Profile、quirk 与 bundled payload。Profile 决定 API 和入口；系统
  库依赖闭包不得由 CLI 手写。API 19 缺 `bootdex.jar` 与缺 ELF 同样在装配前失败。
- 仅接受规范化的 `dex_activity` Profile。entry/presets 在生命周期前应用，required 数据先
  验证；`--external-dir` 最多一个。quirk 必须在 `data/quirks.toml` 注册并有测试引用。
- `--preflight` 只验证身份、ELF 闭包、API、surface 与 boundary 映射，不执行 guest。
- MCP 仅绑定 `127.0.0.1`，拒绝非 loopback Origin、错误路径/方法、chunked 或超限 body；
  控制请求在 worker 只排队。manual-step 必须启用 transport，且不能与 preflight 共用；无许可不得推进
  Clock、输入、frame 或 present。
- MCP 发布唯一 lifecycle/frame/ticks/presented-frame/movie/exit/fault 快照；fault 不冒充
  stop/success，截图只读已 present 的 RGBA8。`McpPointerDispatcher` 在 guest 主线程映射输入，
  并与窗口手势互斥。
- pointer 按最新 guest frame 与等比内容区映射；黑边不开始手势，既有 release 必须闭合。
  窗口 FPS 使用独立 `RealtimeClock`，不改变 guest Clock。
- 音频只消费已解析的 resid/APK/VFS 字节区间；解码归 audio，补充有界，退出停止设备。
- process exit、fault、shutdown 均走正常 teardown；native finalizer 先于 ANGLE surface 关闭。
  observer 只在 SDL 宿主线程泵事件，guest Java worker 不触碰窗口或共享进度。
- 日志保留 backend 来源、Profile、bootstrap、生命周期和原始 JNI/CPU 故障上下文。CLI 顶层
  每个失败只输出一个缩进的 `error [ogplay]` 块；运行循环仍发布 MCP `guest_fault`、清理并
  传播，不重复打印。
- GUI 子进程关闭 stdin、继承 stdout、stderr 覆盖写入 `last-run.log`；同 installation id 单实例，
  GUI 退出不杀游戏。存档固定在库根 `sandbox/`；删除条目不删除 external 或存档。macOS CLI
  名为 `ogplay-cli`。所有用户可见失败同时写结构化日志并给出下一步。

## 禁止

- 不实现 syscall/JNI/GLES、游戏特判或仅 GUI 可用的内核行为。
- 不恢复 Profile 驱动的 Java registry、native phase 或 JNI 调用序列。

## 验证

CLI 参数、MCP transport 和会话控制由 CTest 覆盖；真实 APK 按 playbook 使用
`tools/run_scenario.py` 产出机器可判定证据。
