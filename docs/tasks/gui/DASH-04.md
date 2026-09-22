# DASH-04 · 共享键联动与诊断面板

状态：Windows 实现与定向验收完成；Linux 暂缓。依赖 [DASH-02](DASH-02.md)、[DASH-03](DASH-03.md)，依据 [Dashboard 规划](../../design/gui/dashboard.md)。

## 实现

- 14 个来源区及 DexVM/JNI/CPU/memory/GPU trace/VFS/AudioTrack/VideoView/UiTree/loader/账本/日志面板；monitors、futex、pacer 与 A32/native/syscall 调用链使用原诊断事实。
- guest/context/host 身份只经真实 execution 映射；monitor owner 可跳线程，futex address 可筛选等待者。0 线程身份不提供跳转，fd 0 有效。
- frame 游标固定对应采样快照和时间窗，日志/事件按真实键过滤；快照淘汰明确提示，不把实时线程详情混入历史。
- capability、lifecycle/generation、fd/node_id、player 联动；日志只匹配结构化字段。ARM 文件 syscall 携带可解析的 FD/node 身份，未知保持 null。
- 九类事件泳道；GC/GL error/underrun/能力缺口/VFS flush 使用累计计数的观测差值，空心标记区分真实发生事件。缺少发生 frame/time 时不伪造。
- AudioTrack/UiTree/VideoView/GPU 读取使用 try-lock；生命周期启动/恢复发布 running。FFmpeg 可用性与原因在装配时封存。

## 验证

- TypeScript/Vite 构建及 30 项前端测试通过；包含共享键精度、身份映射、历史选择、结构化日志过滤和告警来源。
- C++ 与真实进程证据见 DASH-02；2 项 `frontend.dashboard_` CTest 通过。
- 浏览器验证真实线程 #2 → Java 栈与调用链、能力缺口 → 观测事件与精确过滤、player → 音频行；页面无横向溢出，表格内部滚动。关闭运行进程后显示断线。

## 支持边界

- 支持已有来源的面板与共享键；字段缺失不新造关联。GLES trace 尚无线程/帧键；全局标量保留其模块作用域，只有带键的记录参与关联。
- 音频当前展示 AudioTrack；视频展示 VideoView 基准位置/decoder 装配，不冒充实时解码队列；UI 展示树/dirty/focus，未发布输入 capture/队列。对应 section 标 partial；规划表中的其他未记账指标不声明支持。
- 累计计数首次以零为基线；观测时间不是精确发生时间，不能用它证明某帧无 GC/欠载。身份缓存最多 4096 项，记录裁剪边界保留原状态。
- FrameService 尚未记账 GL error，运行进程的 gl_errors 为 null、该事件来源 unavailable；不把默认 0 宣称为无错误。其他 GPU 计数仅覆盖已有记录路径。
- 本机 FFmpeg 7 DLL 不可用，视频真实解码未验收；缺少结构化 capability 字段的日志不作文本猜测。完整 BootDex 扩展回归阻塞见 DASH-02。

下一步：[Dashboard 规划](../../design/gui/dashboard.md) 的 DASH-05 操作手册。
