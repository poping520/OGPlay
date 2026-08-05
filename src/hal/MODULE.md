# 模块：hal

## 职责

定义 window、gfx、audio、input、fs、video、thread、clock 的宿主接口；平台实现只能位于
`hal/windows`、`hal/linux`、`hal/macos`。

## 公共 API

- `hal::Clock`：所有 guest 时间源使用的抽象。
- `hal::ClockRate`：正整数分子/分母表示的精确倍率。
- `hal::FixedStepClock`：无 sleep、按帧推进且保留倍率余数的确定性后端。
- `hal::RealtimeClock`：基于单调宿主时间的实时后端，支持暂停与倍速。
- `hal::WindowInput`：不暴露 SDL 类型的窗口生命周期与输入事件接口。
- `hal::WindowInput::PresentRgba8`：校验完整 RGBA8 帧并缩放提交到当前 SDL 窗口。
- `hal::FitDisplayRect`：用无浮点整数运算把源画面等比居中适配到目标 surface。
- `hal::CreateSdlWindowInput`：SDL3 实现工厂；关闭 SDL 的构建会明确失败。
- `hal::VirtualMemoryReservation`：页对齐的宿主预留、提交、权限和释放接口。
- `hal::ReserveVirtualMemory`：按目标平台选择 VirtualAlloc 或 mmap 后端。
- `hal::HostThread` / `StartHostThread`：真实宿主线程启动、标识与显式 join 边界。
- `hal::GraphicsPresenter`：不暴露 SDL/EGL/平台句柄的 drawable 与 present 契约。
- `hal::AudioOutput`：格式、启动停止、帧队列与交错样本提交契约；设备后端属于后续阶段。
- `hal::HostFileSystem` / `CreateStandardHostFileSystem`：宿主文件状态、建目录及二进制读写。
- video 接口按媒体能力需求在后续里程碑定义。

## 不变量

- 平台差异不泄漏到 runtime/session。
- 时间由统一 Clock 提供；线程接口必须映射真实宿主线程。
- 暂停期间 ticks 不增长；不支持的推进方式必须明确失败。
- SDL video 生命周期由创建它的宿主主线程拥有；输入只保留宿主事实，不翻译 guest 语义。
- 帧尺寸与字节数必须精确匹配；只有 SDL surface 更新成功才累计 present。
- present 每帧先清理黑边，再只向等比内容矩形缩放；窗口比例不得拉伸 guest 画面。
- 虚拟内存写权限必须同时具备读权限；范围必须页对齐且位于自身 reservation 内。

## 禁止

- 不包含游戏规则、Title Profile 或 guest API 语义。
- 不依赖 cpu/memory/runtime/gles/audio/input/session/frontend。

## 测试

后端契约测试放在 `tests/hal/`。
