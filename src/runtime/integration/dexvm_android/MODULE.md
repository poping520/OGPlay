# 子模块：runtime/integration/dexvm_android

## 职责

提供 dex_activity 生命周期使用的 android.*、相关 java.* 与 javax.* 平台
intrinsic。`catalog.cpp` 是唯一注册聚合点；平台类按 API 家族聚合到一个源文件，
每个类仍导出独立的 `Declare_<类名>(context)`，返回已经直接持有 handler 的
不可变声明。Java handle 家族聚合文件用于控制翻译单元数量，不受项目通常的
800 行源文件上限约束。

声明与实现同址是唯一 handler 形态。`shared.h` 只暴露跨类共享 helper 与工厂；
跨类复用的 handler 以 `shared_handlers.cpp` 中的工厂函数（如 `ViewInitHandler()`、
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
