# 子模块：runtime/integration/dexvm_android

## 职责

提供 dex_activity 生命周期使用的 android.*、相关 java.* 与 javax.* 平台
intrinsic。`catalog.cpp` 是唯一聚合点；每个平台类由同名源文件导出
`Declare_<类名>(context)`，返回已经直接持有 handler 的不可变声明。

`shared.h` 只暴露跨类共享 helper、占位工厂和迁移期内部装配；跨类复用的 handler
以 `shared_handlers.cpp` 中的工厂函数（如 `ViewInitHandler()`、
`PrefsEditHandler(context)`）形式提供，捕获会话状态的工厂显式接收 context。
资源、VFS、音频、视频、widget、线程与设备事实全部来自显式传入的
`DexVmAndroidContext`，不得读取游戏身份或另建宿主状态。

## 不变量

- 每个类描述符只能由一个 `Declare_*()` 文件发布，`catalog.cpp` 不包含行为。
- catalog 返回的每个非抽象方法都直接持有 implementation；字符串 handler id 只
  允许存在于 DVM-37 将删除的兼容实现中，不能进入最终声明。
- 新类必须进入独立文件；禁止新增 misc、按 title 或按厂商聚合的实现文件。
- 中性占位只能通过 `NeutralHandler(shorty)` 或 `PlaceholderString()` 显式生成；
  引用返回值不能擅自伪造对象。
- `DexVmAndroidContext` 是唯一会话状态入口，handler 行为与迁移前保持一致。

## 依赖

本目录属于 runtime/integration，可依赖 dexvm、loader、framework、VFS、音视频等
下层模块；任何下层模块不得反向依赖本目录。

## 测试

`tests/session/profile_entry_scope_tests.cpp` 锁定 catalog 唯一性、直接绑定及
Activity/Intent/Bundle/TextView 方法集合；`tests/dexvm/file_vfs_tests.cpp`、
`videoview_tests.cpp`、`widget_click_tests.cpp` 锁定主要集成行为。
