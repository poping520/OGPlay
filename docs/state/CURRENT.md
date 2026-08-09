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

- 无。目标 ELF 的 62 个 GL import 已全部具有显式行为 handler；exact-APK 在一帧边界
  正常停止。matrix-palette skinning 与 fixed renderer 完整语义仍按能力账本保持 partial。

## 最近完成

- [WU-0281] SoundPool 普通/big load 与 play 共享通用 pending/loaded 状态机；loaded 查询
  修正为 legacy Java 的 `0` / `-1` 契约，play 执行 lazy request，只有真实 loaded resource
  才创建 voice。exact-APK 600 帧及 shutdown 均以状态 0 完成；解码和输出保持 partial。
- [WU-0280] Java SoundPool 已加载资源以类别 + resource 的通用键线程安全建账；
  `is_sound_loaded` / `is_sound_loaded_big` 查询目录事实，`unload_sound` /
  `unload_sound_big` 精确删除同一键。exact-APK 120 帧及 shutdown 均以状态 0 完成。
- [WU-0279] 统一 JNI invocation engine 的 missing-handler 错误现携带已解析的规范
  implementation ID；exact-APK 下一边界由不透明错误精确定位为
  `audio.is_sound_loaded_big`。
- [WU-0278] Java SoundPool 状态已扩展为线程安全、按普通 pool / big 分类的通用 voice
  目录；`stop_all_sounds`、`stop_all_pool`、`stop_all_big` 三个 handler 批量闭合，分类
  stop 支持保留指定 resource。exact-APK 120 帧及 shutdown 均以状态 0 完成。
- [WU-0277] exact-APK frame call 9 首个缺失 implementation 已确认为
  `audio.destroy_sound_pool`，并与紧随其后的 `audio.init_sound_pool_array` 一并接入会话
  拥有的幂等 SoundPool 生命周期。2 帧 smoke 状态 0；120 帧可运行至 shutdown，下一精确
  边界为 `nativeDestroy` 中的 `audio.stop_all_sounds`。
- [WU-0276] `CallStaticObject/Boolean/Byte/Char/Short/Int/Long/Float/Double/VoidMethod`
  的普通、`V`、`A` 共 30 个 guest 槽已批量闭合；三种 A32 参数布局与所有返回位型均由
  descriptor 驱动并进入统一 invocation engine。用户报告的 `CallStaticVoidMethod`
  unbound 边界已消除，exact-APK 一帧 smoke 以状态 0 完成。
- [WU-0275] 最后 3 个 `GL_OES_matrix_palette` import 已进入受检、可重置的 palette index
  与 client pointer 状态；未完成的 skinning draw 明确失败。目标 62/62 GL import 均有
  行为 handler，exact-APK 一帧 smoke 继续以状态 0 完成。
- [WU-0274] 批量闭合 4 种 client pointer、enable/disable client state 与两种 draw；guest
  array/index 按真实 draw 范围预检上传，内部 GLES2 fixed shader 消费矩阵、颜色、light0、
  texture、fog 与 alpha state。exact-APK 首次成功提交 1 个 managed ANGLE frame。
- [WU-0273] 一次批量闭合 `glCompressedTexImage2D`、`glCopyTexImage2D`、
  `glTexImage2D` 与 `glTexSubImage2D`，guest 像素受检搬运并在 level 0 消费 automatic
  mipmap 状态。exact-APK 推进到 `glEnableClientState`。
- [WU-0272] 一次批量闭合 `glAlphaFunc`、`glClientActiveTexture`、`glColor4f/ub`、
  `glGetFloatv`、`glTexEnvfv/i`；最大 anisotropy 来自真实 ANGLE，其他 legacy 状态按
  context/texture unit 保存。exact-APK 推进到 `glCompressedTexImage2D`。
- [WU-0271] GLES1 `GL_GENERATE_MIPMAP` 已按 active unit 与 texture object 隔离保存，
  delete/reset 同步且不再错误转发 GLES2；exact-APK 推进到 `glGetFloatv`。自动生成消费
  留待 texture upload，能力保持 partial。

## 目标 ELF 尚未实现的 GL 入口

以下清单以 `docs/demo/games/libasphalt5.so` 的 62 个 GL import 与 WU-0264 后的显式
GLES1 handler 对照得出；当前已实现 62 个，尚余 0 个。它只表示该目标实际导入且尚未
实现的入口，不代表完整 GLES1 命名空间。

- 无。

## 下一步（按优先级）

1. 增加 exact-APK 更长时限和输入驱动 smoke，采集下一真实行为边界与稳定黄金帧。
2. 为 SoundPool pending request 接入声明式 APK raw-resource 解析，再推进解码与 HAL 输出。
3. 按 exact-APK 后续真实调用证据，批量闭合类/实例及字段/数组/字符串 JNI 槽。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
