# 子模块：runtime/integration/dexvm_android

## 职责

提供 dex_activity 生命周期使用的 android.*、相关 java.* 与 javax.* 平台
intrinsic。`catalog.cpp` 是唯一注册聚合点；平台类按 API 家族聚合到一个源文件，
每个类仍导出独立的 `Declare_<类名>(context)`，返回已经直接持有 handler 的
不可变声明。Java handle 家族聚合文件用于控制翻译单元数量，不受项目通常的
800 行源文件上限约束。

声明与实现同址是唯一 handler 形态。`shared.h` 只暴露跨类共享 helper 与工厂；
跨类复用的 handler 以 `shared_handlers.cpp` 中的工厂函数（如 `ViewInitHandler(context)`、
`PrefsEditHandler(context)`）形式提供，捕获会话状态的工厂显式接收 context。
资源、VFS、音频、视频、widget、线程与设备事实全部来自显式传入的
`DexVmAndroidContext`，不得读取游戏身份或另建宿主状态。

javax EGL/GL façade 遵循 DVM-31：`javax_microedition_khronos_egl.cpp` 聚合该
家族的 10 个 Java handle 声明、handler、唯一
display/config/window-surface/context/currency 状态机和 swap pacer，把 guest
`eglMakeCurrent`/`eglSwapBuffers`/`glGetString` 窄接到 session 已拥有的 managed
ANGLE surface；它不创建、替换或终止第二套 EGL surface。

## 不变量

- 每个类描述符只能由一个 `Declare_*()` 发布；同一 API 家族的多个
  `Declare_*()` 必须同址于一个聚合 cpp，`catalog.cpp` 不包含行为。
- 单类专用 handler 必须直接定义在对应 `Declare_*()`；禁止恢复按域填充函数
  或全量 handler 容器。
- catalog 返回的每个非抽象方法都直接持有 implementation；字符串 handler id 只
  允许存在于 DVM-37 将删除的兼容实现中，不能进入最终声明。
- 新类必须进入所属 API 家族的聚合文件；禁止新增单类翻译单元、misc、按 title
  或按厂商聚合的实现文件。
- 中性占位只能通过 `NeutralHandler(shorty)` 或 `PlaceholderString()` 显式生成；
  引用返回值不能擅自伪造对象。
- `DexVmAndroidContext` 是唯一会话状态入口，handler 行为与迁移前保持一致。
- 每个 live guest View object 与一个 live UiNode 一一绑定；hierarchy/id/visibility/
  geometry 只写 `runtime/ui`，guest click/touch listener 只在本层以 UiNodeId 为 key 保存。
- drawable decode 的 intrinsic size 写入 UiNode；click adapter 只消费 `screen_frame`，旧
  fullscreen/edge-row `layout_views` bounds 推导已删除。
- drawable resource 按 id 解码一次并缓存为 RGBA `UiBitmap`，renderer 不读取 APK、arsc
  或 guest object。
- pointer dispatch 在 dirty 时先 traversal，按 clipped reverse draw order 选择 topmost
  enabled/visible listener node；OnTouchListener 与 click listener 均以 UiNodeId 调 guest。
  `findViewById/getId/setId` 必须经双向 binding 返回/修改同一 object/node identity；
  content generation reset 同时清空两向 binding 与 listener，旧 node 不得继续可见。
- `UiWidgetRegistry` 是 XML tag → dex descriptor/UiClass 唯一目录；inflater 只解释 generic
  typed attrs，`<merge>` 只允许作为唯一 document root 且不创建 object/node。未知 tag、
  非法 parent/root、未知 `layout_*` structural attr 必须记账或明确失败，禁止跳过并
  re-parent children 后声称成功。
- 动态 BroadcastReceiver 注册按发起调用的 Context 实例拥有；null receiver 只查询
  sticky broadcast，未注册、重复或跨 Context 注销抛 `IllegalArgumentException`。
  当前平台没有广播来源，因此不伪造 `onReceive` 派发。
- Intent extra 当前支持的 String/Int 类型共享逻辑 key 空间；`removeExtra` 从全部
  类型分表删除该 key，空表随即释放，不存在的 key 无操作。
- VideoView error listener 按 view 实例注册、替换或清除；没有具体异步错误事件时
  不伪造 `onError` 回调。pause/seek capability 只反映已打开 player；缺失 player
  的 completion 延迟到视频 pump，禁止从 `start()` 重入 guest。
- Activity 替换会关闭旧 SurfaceHolder generation 并清除其 holder/callback；新
  generation 注册完成后接收仍存活 managed surface 的 created/changed，禁止跨
  Activity 累积 callback。
- EGL 规范内失败走 false/EGL_NO_* + last-error；单 surface、单 currency 模型被
  破坏时必须记账并抛出，未知入口同样记账明确失败。
- guest swap 发布帧后进入条件帧屏障：driver 可运行时等下一次 lifecycle generation；
  driver 经通用执行锁 observer 进入 guest 阻塞时立即放行；shutdown 同样唤醒。
  observer 按注册的 driver 宿主线程 id 过滤，GLThread 自己释放执行锁不改变状态。
  禁止退化为 publish+yield、墙钟超时或单次免停泊额度。

## 依赖

本目录属于 runtime/integration，可依赖 dexvm、loader、framework、VFS、音视频等
下层模块；任何下层模块不得反向依赖本目录。

## 测试

`tests/session/profile_entry_scope_tests.cpp` 锁定 catalog 唯一性、直接绑定及
Activity/Intent/Bundle/TextView 方法集合；`tests/dexvm/file_vfs_tests.cpp`、
`videoview_tests.cpp`、`widget_click_tests.cpp` 锁定主要集成行为。
