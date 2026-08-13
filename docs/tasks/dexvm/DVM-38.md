# DVM-38 · android intrinsic 实现与声明完全同址（AndroidHandlers 退场）

## 目标（一句话）

把 DVM-36 迁移期遗留的"实现按域聚合在 `support_*.cpp` 填充 `AndroidHandlers`
结构体、逐类文件按成员引用"的装配，收口为 java.* 侧同款的完全同址形态：
handler 实现体直接内联在其类的 `Declare_*()` 里，`AndroidHandlers`/
`handlers.inc`/`MakeAndroidHandlers`/`Populate*` 全部退场。

## 背景（DVM-34..37 验收发现 1/2）

- 324 个 handler 实现体聚合在 9 个 `support_*.cpp`（≈3100 行）里填充
  `AndroidHandlers` 的具名 `std::function` 成员；165 个逐类文件各自调用
  `MakeAndroidHandlers(context)` 取成员——每次调用全量构造 ~324 个
  `std::function`（约 166 次装配期浪费），且"看一个类的完整行为要跨文件"。
- 使用普查事实：278 个成员恰被 1 个类文件消费（应内联）；34 个被多类消费
  （多为 interface/Impl 成对声明与 IO/prefs 家族，应工厂化）；7 个 platform
  成员仅被 bridge 消费；5 个成员无任何消费者（迁移遗留死代码）。
- support 文件之间零交叉引用；类文件与 support 域仅 3 处多对一
  （View/Context/FileWriter），按波次串行可避免编辑冲突。

## 方案

1. **共享工厂**：34 个跨类成员 + 7 个 platform 成员改为 `shared.h` 声明、
   `shared_handlers.cpp` 实现的工厂函数（捕获 context 的带 `const Context&`
   参数）；bridge 的平台动作绑定表直接持有工厂产物，"目标声明不存在即
   装配失败"语义不变。5 个死成员直接删除。
2. **逐域同址迁移**：9 个 support 域的单类 handler 实现体逐字搬进消费它的
   类文件（Declare 内联 lambda；域内同类共享的局部 lambda 随迁；跨类共享
   helper 提升到 `shared.h`/`shared.cpp`）。`support_widget_dispatch.cpp`
   保留其非 handler 公共 API（点击命中/派发）。
3. **基础设施退场**：`handlers.inc`、`AndroidHandlers`、
   `MakeAndroidHandlers`、全部 `Populate*` 与清空后的 support 文件删除；
   `MODULE.md` 更新为"声明与实现同址是唯一形态"。
4. 行为零变化：实现体逐字搬运，全量 CTest 兜底。

## 验收（机器可判定）

1. `src/runtime/integration/dexvm_android/` 下 grep
   `AndroidHandlers|MakeAndroidHandlers|handlers\.handler_|Populate` 为 0；
   `handlers.inc` 与仅剩空 Populate 的 `support_*.cpp` 文件不复存在。
2. 全量 CTest 全绿（行为零变化）；单文件 ≤800 行。
3. `MODULE.md`（dexvm_android 与 integration）、`CURRENT.md` 同步；
   `capabilities.toml` 无能力变化。

## 进展（交接快照，2026-08-13）

**已完成（编译验证到第一波，第二波构建进行中）**：

- 共享工厂：41 个跨类/platform handler → `shared_handlers.cpp` 工厂 +
  `shared.h` 声明；bridge 平台绑定表改为直接持有工厂产物；5 个死成员删除；
  46 个成员已从 `handlers.inc` 移除；~67 个类文件引用已切换。
- 域迁移已完成 6/9：media（含样板）、activity（57/57，
  `DispatchSurfaceHolderCallbacks` 生命周期胶水保留）、io（28/28，
  `OutputOf` 提升）、video（8/8，`VideoStateOf`/`VideoPositionOf`/
  `InvokeVideoCompletionListener` 提升，pump 子系统保留）、
  content_util（48/48）、graphics（28/28）、files（22/22，`FilePathOf`
  提升，File/FileWriter 收口）。对应 support 文件均已清空为空 Populate
  脚手架（media/io/content_util/graphics/files），activity/video 保留
  非 handler 公共代码。

**剩余工作**（见任务书正文验收项）：

1. 编译验证第二波 + files/video 收口（构建进行中，若失败先修复）。
2. 第三波迁移（两者类文件不相交，可并行）：
   - `support_device.cpp`（60 个 handler → AudioManager/Context
     (get_shared_preferences)/Handler/Locale/Log/Looper/Message/Sensor*/
     Telephony/Thread/Timer/URLEncoder/Wifi）。注意：prefs 装载 helper
     （`LoadPreferencesOnce`）与 `context_get_shared_preferences` 同迁；
     Handler 与 Message 家族若共享 obtain-message helper（跨
     Handler.cpp/Message.cpp 两个文件）→ 提升 `shared.h`；Thread 家族用
     既有共享 `ThreadRuntime`；Timer 用既有共享 `RunJavaThreadNow`。
   - `support_widget_dispatch.cpp`（3 个 handler → android_view_View.cpp）。
     约束：`kVisible/kInvisible/kGone` 常量与 `VisibilityOf` 同时被保留的
     `DeriveBounds`/公共派发 API 使用 → 常量与 `VisibilityOf` 提升
     `shared.h`（实现在原文件或 shared.cpp），handler 迁入 View.cpp；
     该文件保留为点击派发子系统（`FindClickableViewAt`/
     `ViewContainsPoint`/`InvokeViewOnClick` 不动）。
3. 基础设施删除：`handlers.inc`；`shared.h` 的 `#include "handlers.inc"`
   与全部 13 个 `Populate*` 声明；`shared.cpp` 的 `MakeAndroidHandlers`；
   删除已清空的 support 文件（media/io/content_util/graphics/files/device），
   activity/video/widget_dispatch 三个文件保留其余留公共代码并更名与否
   自行判断（CMake 用 GLOB CONFIGURE_DEPENDS，删增自动生效）。
4. 全量构建 + 全量 CTest（基线 709/709，行为零变化）。
5. 文档收口：本文件补最终结果段、README 状态改"完成"；
   `dexvm_android/MODULE.md` 删除"迁移期内部装配"表述（同址成为唯一形态、
   共享 helper/工厂经 shared.h）；`integration/MODULE.md` 相应调整；
   `CURRENT.md` 滚动更新（**注意 6144 字节上限**）；`capabilities.toml`
   无变化；未提交，由用户决定提交时机。

**迁移纪律（沿用）**：实现体逐字搬运、禁止顺手改行为；helper 只被单类
使用则随迁、跨类共享则提升 shared、被保留代码使用则先提升再迁；
目标形态参考 `android_media_SoundPool.cpp`。构建注意：改 `shared.h`/
`handlers.inc` 会触发目录级重编（~190 TU、3-5 分钟，单工程内无 /MP
并行）；被中断的构建可能残留 cl/MSBuild 进程锁 PDB（C1041），等其自行
退出后重试。

## 最终结果（2026-08-13）

- device 的 60 个 handler 与 widget dispatch 的 3 个 handler 已迁入各自
  `Declare_*()`；Handler/Message 共用的消息构造、派发 helper，以及 widget
  可见性事实经 `shared.h` 共享。
- `AndroidHandlers`、`handlers.inc`、`MakeAndroidHandlers`、全部 `Populate*`
  和 6 个清空的 support 文件已删除；activity/video/widget support 仅保留
  surface、视频泵与点击派发公共 API。
- 验收 grep 为 0，目录内最大源文件 507 行。macOS/arm64 keep-going 构建已编译
  全部 DVM-38 变更单元；标准全量构建在既有 `preferences_xml.cpp` 浮点
  `std::from_chars` 的 macOS 部署目标可用性错误处停止。现存测试二进制执行结果为
  653/655，仅两个旧 GUI smoke 因二进制不含 `gui` 命令失败；由于本次对象未能完成
  链接，该结果只作基线参考，不冒充变更后全量 CTest。`capabilities.toml` 无变化。
