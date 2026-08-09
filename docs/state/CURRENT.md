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

- [WU-0286] ANGLE pbuffer config 已明确请求 RGBA8+D24S8，真实查询验证默认 framebuffer
  具备至少 24-bit depth 与 8-bit stencil；exact-APK 逐批 draw 隔离证明黑块来自缺失 depth
  attachment 后，正式修复版由用户确认两个黑块消失。
- [WU-0285] fixed renderer 已支持两个启用纹理单元顺序级联并隔离 sampler、坐标、base
  format、texture environment 与 texture matrix；lighting alpha 遵循 diffuse material
  alpha。真实 ANGLE 双纹理/透明光照 readback 与 exact-APK 主界面纹理均已验收。
- [WU-0284] fixed renderer 已按 texture object 的 level-0 base format 消费
  MODULATE/REPLACE/ADD 与单纹理完整 COMBINE 状态；exact-APK 越过三个真实边界并由用户
  确认进入游戏主界面，517 帧后正常停止且状态 0。
- [WU-0283] `display.change_mode` 已映射为线程安全的通用允许休眠/保持唤醒请求状态；
  exact-APK 越过该 Java handler。宿主防休眠控制尚未接入，能力保持 partial。
- [WU-0282] SoundPool voice control 家族已批量闭合：普通/big pause、resume、stop、volume，
  普通 pool pitch 与 big reset 共 10 个 handler 驱动同一受检状态。exact-APK 无界运行约
  90 秒、1323 帧后正常停止且状态 0；帧存活不替代语言界面视觉验收。
- [WU-0281] SoundPool 普通/big load 与 play 共享通用 pending/loaded 状态机；loaded 查询
  修正为 legacy Java 的 `0` / `-1` 契约，play 执行 lazy request，只有真实 loaded resource
  才创建 voice。exact-APK 600 帧及 shutdown 均以状态 0 完成；解码和输出保持 partial。
- [WU-0280] Java SoundPool 已加载资源以类别 + resource 的通用键线程安全建账；
  `is_sound_loaded` / `is_sound_loaded_big` 查询目录事实，`unload_sound` /
  `unload_sound_big` 精确删除同一键。exact-APK 120 帧及 shutdown 均以状态 0 完成。

## 目标 ELF 尚未实现的 GL 入口

以下清单以 `docs/demo/games/libasphalt5.so` 的 62 个 GL import 与 WU-0264 后的显式
GLES1 handler 对照得出；当前已实现 62 个，尚余 0 个。它只表示该目标实际导入且尚未
实现的入口，不代表完整 GLES1 命名空间。

- 无。

## 下一步（按优先级）

1. 建立可自动判定的 exact-APK 主界面/readback 检查，替代本次人工视觉验收。
2. 为 SoundPool pending request 接入声明式 APK raw-resource 解析，再推进解码与 HAL 输出。
3. 按 exact-APK 后续真实调用证据，批量闭合类/实例及字段/数组/字符串 JNI 槽。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
