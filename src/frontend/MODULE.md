# 模块：frontend

## 职责

提供 CLI 与 GUI；前端只编排公共内核 API，不实现兼容行为。提供可选 loopback MCP
Streamable HTTP transport，并把截图、输入和会话控制交给 agent/session 契约。

## 公共 API

- `run-apk`：按 exact Title Profile v2 选择 APK 根库，挂载外部数据，创建 SDL3/ANGLE
  surface，装配 Android guest session + DexVM bridge + `DexActivityLifecycle`。
- `ogplay-gui` / `ogplay gui`：共用 `frontend/gui` shell；前者是零参数双击入口，后者
  只为开发/CI 接受库根覆盖与有界帧数。点击 ready 条目只 spawn 同目录既有
  `run-apk` 路径；运行状态和退出结果由 SDL 进程对象回收。
- `--external-dir`：把一个宿主目录按 Profile 声明的唯一 external guest 根 lazy mount；
  guest 路径不取决于宿主目录名。
- `--supersample <1..4>`：显式选择内部渲染倍率，默认 1×。
- `--mcp` / `--mcp-port`：启动仅限本机的 MCP 服务；`--mcp-manual-step` 让 DexVM 会话
  等待 step/suspend/resume/shutdown 命令并发布原子状态。
- `McpPointerDispatcher`：在 guest 主线程把 MCP pointer phase 映射为通用 boundary input，
  与桌面鼠标手势互斥。
- 结构化日志报告 exact Profile、guest bootstrap、静态预置、JNI/lifecycle、首帧、进度和
  有序 teardown；长调用 observer 只读取既有 A32 slice 事实。

`run-apk` 默认为 APK 的 package 打开持久存档沙盒（ADR-0020）：根目录取
`hal::HostUserDataDirectory()` + `sandbox/`，`--sandbox-dir` 显式指定，
`--ephemeral-sandbox` 关闭持久（两者互斥）。平台目录约定只在 `hal/<platform>`
里判定，frontend 只决定 OGPlay 在其下放什么。沙盒打不开即终止启动并给出可
执行修复提示，绝不静默降级为内存模式；自动化（scenario runner）默认一次性
沙盒以保持 golden 确定性。底层挂载与沙盒装配在 `cli/run_apk_vfs.{h,cpp}`。
`run-apk` 把 lifecycle 的 pause/clean-stop 持久状态回调绑定到同一 VFS 的
`FlushAll`，退出时仍打开的脏文件不再依赖 guest 逐个 close。

## 不变量

- CLI 和 GUI 共用相同 session/config/profile；生产库不得裸输出。
- MCP HTTP 只绑定 `127.0.0.1`，拒绝非 loopback Origin、非 `/mcp`、非 JSON POST、
  chunked/unbounded body；worker 只排队，不进入 guest。
- MCP manual-step 必须显式启用 transport，不能与 preflight 组合；无许可时不得推进 guest
  Clock、输入、frame 或 present。
- MCP 会话发布同一份 lifecycle/frame/ticks/presented-frame/movie/exit/fault 状态；fault
  不得伪装为停止或成功。截图只读取已成功 present 的 RGBA8 快照。
- `run-apk` 要求显式 Bionic 目录；入口只由 Manifest + native hash + ABI exact Profile
  决定，支持压缩的 armeabi/armeabi-v7a，依赖闭包不得由 CLI 手写。
- 只接受 Title Profile v2 `dex_activity`。Profile entry override 在生命周期前解析，static
  presets 在 DexVM class 初始化后、Activity 启动前应用；required external manifest 必须
  已由 VFS 验证。
- `--profiles-dir` 可覆盖目录；`--preflight` 只做身份、ELF 闭包、API、surface 与 Android
  boundary 映射/重定位，不创建窗口或执行 guest。
- `--external-dir` 最多一个；Profile 必须声明唯一 external mount。required 输入缺失在
  创建窗口前失败。
- quirk Profile 必须通过仓库 `data/quirks.toml` 交叉验证；未注册或无测试引用时失败。
- pointer 事件按最新 guest frame 与等比内容区映射；黑边不开始手势，已开始的 release
  必须闭合。窗口 FPS 使用独立 `RealtimeClock`，不改变 guest Clock。
- SoundPool loader 只消费解析后的 source/path；每轮音频补充有界，退出时停止设备。
- guest process exit、fault 和 shutdown 都必须进入正常 lifecycle/session teardown；native
  finalizer 在 managed ANGLE surface 关闭前执行。
- observer 只泵宿主窗口消息并按宿主时间节流，不推进 guest Clock、消费 guest 输入或
  提交半帧。
- GUI 子进程必须关闭 stdin、继承 stdout、把 stderr 覆盖写入条目 `last-run.log`；同
  package 单实例，GUI 退出不得杀死仍运行的游戏。
- GUI 删除库条目不得删除 external 数据或持久存档；设置面只保存启动必需的
  system/Profile 目录，所有用户可见失败必须同时进入结构化日志并给出下一步。

## 禁止

- 不实现 syscall/JNI/GLES 或游戏特判。
- 不恢复 Profile 驱动的 Java registry、native phase 或调用参数拼装。
- 不拥有仅 GUI 可用的内核行为。

## 测试

CLI 参数契约、MCP transport 和会话控制由 CTest 覆盖；真实 APK 用
`tools/run_scenario.py` 按 playbook 产出机器可判定证据。
