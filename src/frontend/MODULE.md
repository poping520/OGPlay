# 模块：frontend

## 职责

提供 CLI 与未来 Qt GUI；二者只编排公共内核 API，不实现兼容行为。
提供可由前端选择启动的 loopback MCP Streamable HTTP transport；协议与截图编码仍由
agent 模块拥有，transport 只负责受限 HTTP framing。

## 公共 API

CLI 支持版本、能力账本、结构化 Agent 请求，以及由精确 Title Profile 驱动的 APK
预检、M4 NativeActivity 与声明式 GLSurfaceView 交互窗口运行；
`run-apk --supersample <1..4>` 可显式选择内部渲染倍率，省略时为 1×；GUI 留在 M7。
`run-apk --external-dir <host-dir>` 可把一个独立数据目录按精确 Profile 声明的 external
guest 根进行 lazy mount；guest 路径不由用户参数或宿主目录名决定。
`run-apk --mcp` 在固定 `http://127.0.0.1:15971/mcp` 启动仅限本机的 MCP 服务；
`--mcp-port <1..65535>` 保留为自定义端口入口，二者均省略时不创建 listener。
`--mcp-manual-step` 仅与上述 MCP transport 和 `gl_surface_view` Profile 组合；窗口隐藏，
启动后不自行推进，guest 主线程逐一消费 step/suspend/resume/shutdown 命令并发布原子状态。
`McpPointerDispatcher` 在 guest 主线程每步最多从 MCP queue 取得一个 pointer phase，将
button/motion 保真映射为通用 boundary input，并与 `MouseTouchMapper` 的已捕获桌面手势互斥。
交互窗口标题始终显示 FPS 状态：首个采样周期前为 `FPS --`，之后每 0.5 秒按成功
present 数更新一位小数的实时值。
声明 `audio.sound_pool` 的 GLSurfaceView Profile 会按 source/path_pattern 从 APK 读取编码
资源，将会话离线 mixer 的 stereo PCM16 按队列水位提交到 SDL3 默认音频设备。
`McpHttpServer::Start` 只监听 IPv4 loopback，端口 0 仅供测试选择临时端口；endpoint 固定为
`/mcp`，每个请求直接委托传输无关的 `McpProtocolAdapter`；server 同时持有调用方提供的
有界 MCP input queue 和可选 session control，不从 worker thread 直接进入 guest。

## 不变量

- CLI 和 GUI 共用相同 session/config/profile。
- MCP HTTP 必须只绑定 `127.0.0.1`，拒绝非 loopback `Origin`、非 `/mcp` 路由、非 JSON
  POST、chunked/unbounded body；notification 返回 202 且不伪造 JSON-RPC response。
- `--mcp` 和 `--mcp-port` 各自最多出现一次、不得组合，且均不得与不执行 guest 的
  `--preflight` 组合；自定义端口只接受完整十进制 1..65535，二者省略时默认禁用 MCP。
- `--mcp-manual-step` 最多一次、必须显式启用一种 MCP transport，只支持
  `gl_surface_view`；缺 transport、preflight 或其他 lifecycle 必须在执行 guest 前失败。
- 手动模式没有 step 许可时不得推进 guest Clock、消费 MCP pointer phase、执行 frame callback
  或 present；suspend/resume/shutdown 只能由同一 guest 主线程执行，网络 worker 只排队。
- 手动会话必须从启动到 teardown 发布同一份 lifecycle/frame/ticks/presented-frame/movie/
  exit/fault 状态；guest fault 不得伪装为停止或成功，并保持 MCP 可读直至 shutdown。
- MCP 只发布成功 present 的最新 RGBA8 guest frame；每次发布以移动所有权替换快照并把
  旧 buffer 交还 guest，shutdown 必须在停止 guest 前交还最后一帧。截图不得推进 guest、
  泵输入或伪造无帧成功。
- MCP pointer gesture 使用最新 guest frame 的整数像素坐标；主循环每次最多派发一个 pointer
  phase，down/move/up 跨连续 guest loop step，MCP 手势与桌面鼠标手势互斥且固定使用
  pointer id 0。
- 用户可见输出可以写 stdout/stderr，但生产库不得裸输出。
- `run-apk` 要求显式 Bionic 目录；APK 入口只能由 Manifest + native hash + ABI 精确 Profile
  决定，支持压缩的 `armeabi`/`armeabi-v7a`，依赖闭包不得由 CLI 手写。
- `run-apk` 加载任何含 quirk 的 Profile 前必须注入仓库根 `data/quirks.toml` 并完成注册表
  交叉验证；未注册、缺参数或测试引用失效的 quirk 必须在 APK 执行前失败。
- `--profiles-dir` 可覆盖默认仓库目录；`--preflight` 在身份、ELF 闭包、API、生命周期与
  surface 受检后，复用生产 Android boundary 完成映射/重定位并报告模块、relocation 与
  native-call 计数，不创建窗口或执行 guest。
- `--supersample` 只接受完整十进制整数 1..4，必须在 APK I/O 和窗口创建前拒绝缺值、
  零、越界或尾随字符；默认值保持 1×。
- `--external-dir` 最多出现一次；匹配 Profile 必须声明且只声明一个 external mount，
  required 输入与 manifest 缺失须在创建窗口前失败。宿主目录经 runtime/vfs 通用入口
  索引，不在前端复制文件内容或实现路径语义。
- Profile 声明 `data.working_directory` 时，前端必须在 guest 启动前注入同一 VFS；相对
  文件访问由 VFS 通用解析，前端不得拼接单个资源路径。
- pointer 事件经 input 模块按最近 guest 帧与当前窗口的等比内容区映射；鼠标主键模拟固定
  id 的单点触摸，悬停不注入；黑边按下/移动不注入，黑边释放仍夹紧转发以闭合已开始的
  手势。
- 窗口 FPS 使用独立 `RealtimeClock` 的显式 ticks 驱动通用 sampler，只统计成功提交的
  guest 帧；标题刷新不得改变 guest Clock、生命周期或 present 节奏。
- `gl_surface_view` 只能组合通用 Android guest call session、Profile lifecycle 与
  managed ANGLE surface；phase、class、export 和参数全部来自已匹配 Profile。Profile
  引用完整 direct-asset implementation set 时，CLI 必须把 APK `assets/` 路径和尺寸懒挂载
  到 `/apk` VFS，首次读取才经统一 loader 解压并校验；只把该组通用 ID 交给 guest session。
- 前端必须把 Profile 已验证的 `runtime.maximum_ticks_per_call` 原样交给通用 guest session；
  不得按标题、生命周期阶段或宿主性能改写预算。
- `gl_surface_view` 帧循环必须观察 guest 的显式进程退出请求，并在请求后通过正常
  lifecycle/session teardown 结束；guest module finalizer 必须在 managed ANGLE surface
  关闭前执行，不得从 Java handler 直接杀死整个 CLI 进程。
- `gl_surface_view` 的长 guest call 必须在通用 A32 slice observer 中只泵宿主窗口消息；
  guest 输入仍由正常帧循环消费，observer 不推进 guest Clock 或提交半帧。
- 前端只可把已验证的 `gles1_material_front_face` id 映射为通用 single-face material
  `AndroidBoundaryOptions` 布尔项；不得在运行时接口中传递游戏名、包名或 Profile 对象。
- SoundPool loader 只消费 Profile 解析后的 source/path；当前 `run-apk` 仅挂载 APK source，
  其他 source 必须明确失败。每次帧循环最多补四个 1024-frame chunk，队列目标为 4096 帧，
  禁止为音频阻塞或无界填充 guest frame loop；退出时显式停止设备。

## 禁止

- 不实现 syscall/JNI/GLES 或游戏特判。
- 不拥有仅 GUI 可用的内核行为。

## 测试

CLI 参数契约和真实 APK smoke 由 CTest/有界集成命令驱动；M6 将 smoke 收敛为结构化
AI 自动化场景，GUI 行为测试在 M7 增加。
