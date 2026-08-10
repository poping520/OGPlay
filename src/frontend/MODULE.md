# 模块：frontend

## 职责

提供 CLI 与未来 Qt GUI；二者只编排公共内核 API，不实现兼容行为。

## 公共 API

CLI 支持版本、能力账本、结构化 Agent 请求，以及由精确 Title Profile 驱动的 APK
预检、M4 NativeActivity 与声明式 GLSurfaceView 交互窗口运行；
`run-apk --supersample <1..4>` 可显式选择内部渲染倍率，省略时为 1×；GUI 留在 M6。
`run-apk --external-dir <host-dir>` 可把一个独立数据目录按精确 Profile 声明的 external
guest 根进行 lazy mount；guest 路径不由用户参数或宿主目录名决定。
交互窗口标题始终显示 FPS 状态：首个采样周期前为 `FPS --`，之后每 0.5 秒按成功
present 数更新一位小数的实时值。
声明 `audio.sound_pool` 的 GLSurfaceView Profile 会按 source/path_pattern 从 APK 读取编码
资源，将会话离线 mixer 的 stereo PCM16 按队列水位提交到 SDL3 默认音频设备。

## 不变量

- CLI 和 GUI 共用相同 session/config/profile。
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
- `gl_surface_view` 帧循环必须观察 guest 的显式进程退出请求，并在请求后通过正常
  lifecycle/session teardown 结束；不得从 Java handler 直接杀死整个 CLI 进程。
- 前端只可把已验证的 `gles1_material_front_face` id 映射为通用 single-face material
  `AndroidBoundaryOptions` 布尔项；不得在运行时接口中传递游戏名、包名或 Profile 对象。
- SoundPool loader 只消费 Profile 解析后的 source/path；当前 `run-apk` 仅挂载 APK source，
  其他 source 必须明确失败。每次帧循环最多补四个 1024-frame chunk，队列目标为 4096 帧，
  禁止为音频阻塞或无界填充 guest frame loop；退出时显式停止设备。

## 禁止

- 不实现 syscall/JNI/GLES 或游戏特判。
- 不拥有仅 GUI 可用的内核行为。

## 测试

CLI 参数契约和真实 APK smoke 由 CTest/有界集成命令驱动；GUI 行为测试在 M6 增加。
