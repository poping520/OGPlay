# 当前状态

更新：2026-08-09 · M5 已打开

## 当前阶段

- M0、M1、M2、M3、M4 已完成并验收。
- M5 去硬编码正在推进；首个精确 legacy title Profile 已闭合 APK 身份、Bionic 依赖、
  native-call 目标/A32 调用帧与 Android link preflight，完整 JNIEnv/JavaVM guest ABI
  也已映射；指定 `gl_surface_view` guest process 已执行并提交首个 managed ANGLE frame。
- Windows/MSVC、Linux/x64 与 macOS/arm64 均在同一主仓库 commit `f1b59bb` 上以 ANGLE
  开启和 warnings-as-errors 通过严格全量 CTest 302/302。记录见
  [M4-ACCEPTANCE.md](M4-ACCEPTANCE.md)。
- 当前 M5 增量 WU 的开发与验收目标为 macOS-arm64 + ANGLE：warnings-as-errors、全量
  CTest 与 exact-APK bounded smoke；其他平台留到显式跨平台检查点，不再阻塞每个
  增量 WU。

## 已验收基线

| 里程碑 | 状态 | 验收记录 | Work Unit |
| --- | --- | --- | --- |
| M0 工程地基 | 完成 | [M0-ACCEPTANCE.md](M0-ACCEPTANCE.md) | `docs/tasks/m0/` |
| M1 内核与跨平台 | 完成 | [M1-ACCEPTANCE.md](M1-ACCEPTANCE.md) | `docs/tasks/m1/` |
| M2 Bionic 与 Syscall | 完成 | [M2-ACCEPTANCE.md](M2-ACCEPTANCE.md) | `docs/tasks/m2/` |
| M3 JNI 与 Java 框架 | 完成 | [M3-ACCEPTANCE.md](M3-ACCEPTANCE.md) | `docs/tasks/m3/` |
| M4 ANGLE 与 NativeActivity | 完成 | [M4-ACCEPTANCE.md](M4-ACCEPTANCE.md) | `docs/tasks/m4/` |

能力的机器可读现状以仓库根目录 `capabilities.toml` 为准；本文件不重复维护完整能力历史。

## 进行中

- 无。

## 最近完成

- [WU-0300] APK/OBB 只读 VFS backing 现仅挂载路径与尺寸，首次读取才物化、核对并缓存；
  Debug exact APK 单帧由约 12.75 秒稳定降至 3.78–3.81 秒，120 帧正常退出，全量
  CTest 434/434 通过。
- [WU-0299] SDL 窗口现以无混合复制呈现 opaque guest framebuffer，alpha=0 不再把有效
  RGB 混入黑底；透明红像素契约与 CTest 432/432 通过，用户确认 Debug exact APK 闪屏
  恢复完整白底、主界面无回归，424 帧后正常关闭。
- [WU-0298] 鼠标主键以 input mapper 生成固定 id 0 的成对单点触摸；悬停、非主键、
  黑边起始及跨设备事件均不注入，窗口外 release 夹紧闭合；CTest 432/432 通过，用户以
  Debug exact APK 确认拖拽可旋转车辆，3407 帧后正常关闭。
- [WU-0297] HAL 鼠标按钮已规范为 backend-independent 的 primary/middle/secondary/
  auxiliary 语义，未知按钮显式标记且 SDL 数值不再泄漏；macOS-arm64 + ANGLE
  warnings-as-errors 配置、构建及全量 CTest 429/429 通过。
- [WU-0296] SDL3 `AudioOutput` 已实现受检格式、幂等启停、完整 frame 提交与队列计量；
  `run-apk` 按 Profile source/path 从 APK 读取 SoundPool 资源，并以每帧至多四个 chunk 的
  4096-frame 水位泵送 PCM。Debug exact APK 运行 11264 帧后正常退出，用户验收后要求提交；
  macOS-arm64 + ANGLE warnings-as-errors 配置、构建及全量 CTest 429/429 通过。
- [WU-0295] SoundPool load/lazy-play 现仅在注入资源读取与 Ogg 解码成功后提交 loaded，
  失败保留 pending 和可查询原因；全部 voice 控制同步驱动线程安全离线 mixer，mono/stereo
  经线性重采样、64-bit 累加与 PCM16 饱和输出；macOS-arm64 + ANGLE warnings-as-errors
  配置、构建及全量 CTest 427/427 通过。
- [WU-0294] 仓库固定的 stb_vorbis 1.22 已将最大 64 MiB Ogg Vorbis 内存输入事务解码为
  最大 128 MiB 的拥有型 mono/stereo PCM16；空、损坏、超限、不支持格式与溢出均明确
  失败，真实小型 OGG fixture 覆盖成功路径；macOS-arm64 + ANGLE warnings-as-errors
  配置、构建及全量 CTest 423/423 通过。
- [WU-0293] M5 出口新增游戏声音/音效真实输出要求；Title Profile 可用唯一受检
  `{resource}` / `{resource:0N}` 占位符声明 SoundPool source 与规范资源路径，首个目标的
  raw-resource 命名差异仅存在 Profile 数据中；macOS-arm64 + ANGLE warnings-as-errors
  配置、构建及全量 CTest 421/421 通过。
- [WU-0292] Debug 帧路径已移除冗余 `glFinish`、ANGLE readback 宿主行翻转、帧 RGBA8
  重复分配、Bionic 小块内存 HLE 临时堆分配及 GLES1 legacy 整状态复制；用户以同一
  exact-APK 主菜单确认最高 22.2 FPS，达到游戏自身封锁帧率，上一基线约 13.7 FPS；
  macOS-arm64 + ANGLE warnings-as-errors 构建及全量 CTest 420/420 通过。
- [WU-0291] GLES1 高频 client pointer 已改为验证候选/原子提交，draw input、guest index
  与顺序索引复用宿主暂存高水位容量但每次重新读取 guest；用户确认 Debug exact-APK
  主菜单约 13.7 FPS，较本轮 7 FPS 基线接近翻倍。
- [WU-0290] Dynarmic 已通过受保护 4 KiB 数据页表直达无 observer 的 RW 非执行页；
  observer、权限、execute、跨页与 fault 继续回退受检 callback。Debug exact-APK 从约
  4 FPS 提升到约 7 FPS，ADR-0016 记录其权限边界。
- [WU-0289] 默认拥有型 1× ANGLE readback 直接转移原存储，2..4× 继续使用确定性整数
  box resolve；布局、字节数和存储地址测试已闭合。

## 目标 ELF 尚未实现的 GL 入口

以下清单以 `docs/demo/games/libasphalt5.so` 的 62 个 GL import 与 WU-0264 后的显式
GLES1 handler 对照得出；当前已实现 62 个，尚余 0 个。它只表示该目标实际导入且尚未
实现的入口，不代表完整 GLES1 命名空间。

- 无。

## 下一步（按优先级）

1. 建立可自动判定的 exact-APK 主界面/readback 检查，替代人工视觉验收。
2. 按 exact-APK 后续调用证据继续闭合通用 Java/JNI 能力。
3. 为其他声明音频 source 的 Profile 补齐 OBB/external 前端挂载路径。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
