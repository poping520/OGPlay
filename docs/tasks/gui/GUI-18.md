# GUI-18 · 帧率上限

状态：macOS 定向测试与 Java/原生两条路径实跑已验；App 内设置点击与 Windows 待验。依赖：GUI-4（全局设置）、GUI-5（每实例覆盖与启动参数）。

目标：首个发布版本提供帧率上限，可选常用帧率或不限；只限制宿主放行画面的速度，与游戏自带的限帧互不影响。

## 范围

- 设置：全局「图形 · 默认帧率上限」与游戏「性能 · 帧率上限」共用 key `max_fps`，取值 30/60/90/120/144/unlimited，
  默认 unlimited（保持此前不限帧的行为）；经 EffectiveGameSetting 按游戏 > 全局 > 默认解析，原游戏页预留字段转为正式字段。
- 启动：ResolveLaunch 转为 `GameRunOptions.max_fps`（unlimited 为 0），经 `--max-fps <1..1000>` 传给游戏进程。
- 节拍（`hal::FramePacer`，截止时间、只等不补）作用于游戏交帧的三处：
  1. DexActivityLifecycle：只有 `TryClaim` 抢到槽位的 step 投递 Choreographer vsync、调用 GLSurfaceView
     onDrawFrame/swap 并放行受控 GL 线程；step 本身不等待。
  2. guest 原生代码对 window surface 的 eglSwapBuffers：帧发布、释放模块锁后按 surface 等待再返回；
     Java EGL 外观的宿主调用帧（`A32CallFrame::HostInvoked`）已由第 1 项节拍，不再等待。
  3. 生命周期驱动线程以外的 SurfaceHolder.unlockCanvasAndPost：发布后释放 VM 执行锁按 holder 等待。
- 互不影响：游戏自己已慢于上限时永不等待；落后超过一个周期从当前时刻重排，不连发补帧；
  不改 guest `eglSwapInterval` 语义与 Display refreshRate（仍为 60）。MCP 手动步进不节拍。
- 等待使用 `hal::SleepPreciseNs`（SDL_DelayPrecise；Windows 为高精度计时器），避免 15.6ms 系统计时粒度。

为何不节拍整个 step：step 同时泵主 Looper，并让逻辑 uptime（Handler 延时与 nanoTime 的来源）前进 16ms。
最初的实现让主循环每个 step 等待槽位，DH 在上限 60 下同样 300 帧多用 14%，上限 30 时多用 47%，
游戏逻辑随之变慢，因此改为只门控显示帧。

不做：主线程上的 SurfaceHolder 画布提交及 View invalidate 循环（驱动线程等待会拖慢逻辑时钟）、
NDK ANativeWindow 软件提交（该入口尚未实现）、垂直同步、按显示器刷新率自动选择。

## 验证

- `ogplay_tests`：
  - FramePacer：限速、不拖慢慢速生产者、不补帧、TryClaim 不提前占槽、不限时直通。
  - boundary：guest window swap 按周期放行，托管 swap 不等待。
  - SurfaceHolder 画布：非驱动线程按周期放行，驱动线程不等待。
  - Choreographer：上限下 4 个 step 只投递 1 次 vsync，逻辑时钟仍前进 4×16ms；去掉上限后每个 step 恢复投递。
- `ogplay_app_tests`：`--max-fps` 往返与越界拒绝；ResolveLaunch 全局 60 → 60、游戏 unlimited 覆盖 → 0。
- webui：帧率选项显示为「60 FPS」/「不限」，设置图标覆盖。
- 实跑（macOS release，MCP 每秒采样 successful_presents）：
  - Sweet Shop（NativeActivity 原生 swap）：不限约 1050 FPS；上限 60 为 60～61，上限 30 为 30～31。
  - Dungeon Hunter（Java 生命周期）：不限时菜单约 102 FPS；上限 60 为 60～61，上限 30 为 30～31。
    低于上限的约 28 FPS 阶段不变，加载停顿出现在相同秒数，游戏逻辑进度一致。
- 待验：App 内设置页选择并启动、Windows 编译与计时精度。
