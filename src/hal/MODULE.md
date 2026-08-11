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
- `hal::PointerButton`：把宿主鼠标按钮规范为 primary/middle/secondary/auxiliary 语义；
  未知按钮显式标记为 unknown，不向上层泄漏 SDL 数值。
- `hal::WindowInput::SetTitle`：只在窗口打开期间更新宿主标题，拒绝内嵌 NUL，SDL
  失败必须明确传播。
- `hal::WindowInput::PumpEvents`：处理宿主窗口消息但不移除规范化输入事件，供长任务
  在主线程维持窗口响应；关闭窗口上调用明确失败。
- `hal::WindowInput::PresentRgba8`：校验完整 RGBA8 帧并缩放提交到当前 SDL 窗口。
- `hal::FrameRateSampler`：以调用方提供的单调 ticks 和累计成功 present 数按固定窗口计算
  FPS；输入倒退明确失败，未到采样周期不发布新值。
- `hal::FitDisplayRect`：用无浮点整数运算把源画面等比居中适配到目标 surface。
- `hal::MapDisplayPoint`：复用内容矩形把有限窗口坐标映射到 source，返回黑边内外事实并
  将越界坐标夹紧到 guest 尺寸。
- `hal::CreateSdlWindowInput`：SDL3 实现工厂；关闭 SDL 的构建会明确失败。
- `hal::VirtualMemoryReservation`：页对齐的宿主预留、提交、权限和释放接口。
- `hal::ReserveVirtualMemory`：按目标平台选择 VirtualAlloc 或 mmap 后端。
- `hal::HostThread` / `StartHostThread`：真实宿主线程启动、标识与显式 join 边界。
- `hal::ScopedHostEnvironment`：串行覆盖一组宿主进程环境变量并在嵌套、异常和析构路径恢复
  原值；`HostEnvironmentValue` 提供同锁只读查询。
- `hal::HostExecutableDirectory`：通过平台实现返回当前宿主可执行文件所在的绝对目录，
  供上层定位随程序交付且可重定位的运行时资源。
- `hal::GraphicsPresenter`：不暴露 SDL/EGL/平台句柄的 drawable 与 present 契约。
- `hal::AudioOutput`：格式、启动停止、帧队列与交错样本提交契约。
- `hal::CreateSdlAudioOutput`：按受检 stream config 打开 SDL3 默认播放设备；支持显式 dummy
  backend 契约测试，提交前后均以完整 frame 计量队列，不暴露 SDL 类型。
- `hal::HostFileSystem` / `CreateStandardHostFileSystem`：宿主文件状态、建目录及二进制读写。
- video 接口按媒体能力需求在后续里程碑定义。

## 不变量

- 平台差异不泄漏到 runtime/session。
- 时间由统一 Clock 提供；线程接口必须映射真实宿主线程。
- 暂停期间 ticks 不增长；不支持的推进方式必须明确失败。
- SDL video 生命周期由创建它的宿主主线程拥有；输入只保留规范化宿主事实，不翻译 guest
  语义；只泵消息不得消费或改写待轮询事件。
- 帧尺寸与字节数必须精确匹配；只有 renderer 上传、缩放与 present 全部成功才累计
  present。guest 帧经流式纹理上传后由 renderer 缩放合成，不再走 CPU surface blit。
- RGBA8 guest framebuffer 作为不透明窗口内容复制，alpha 保留为像素事实但不得与预清理的
  黑色宿主 surface 混合；窗口透明合成不属于当前契约。
- FPS 只能基于成功 present 计数，并从统一 Clock 获取时间；不得在窗口后端直接创建另一
  套时间源。
- present 每帧先清理黑边，再只向等比内容矩形缩放；窗口比例不得拉伸 guest 画面。
- 坐标映射与 present 必须共用 `FitDisplayRect`，禁止各自维护舍入规则。
- 音频 stream 只接受正采样率、1..8 声道和已知样本格式；提交必须完整对齐 frame 且不超过
  SDL 长度上限，start/stop 幂等，无设备、backend 冲突与 SDL 错误必须明确传播。
- 虚拟内存写权限必须同时具备读权限；范围必须页对齐且位于自身 reservation 内。
- 进程环境覆盖必须先完整验证名称集合，重复或非法名称不得产生部分修改；覆盖期间持有
  进程级递归锁，恢复按逆序执行。

## 禁止

- 不包含游戏规则、Title Profile 或 guest API 语义。
- 不依赖 cpu/memory/runtime/gles/audio/input/session/frontend。

## 测试

后端契约测试放在 `tests/hal/`。
