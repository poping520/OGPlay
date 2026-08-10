# 当前状态

更新：2026-08-10 · M5 已打开

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

- [WU-0317] Title Profile v1 runtime 新增可选 `maximum_ticks_per_call`，默认维持 2 亿，
  合法范围 1..10 亿；schema、Python 目录门禁和 C++ 强类型 loader 共同拒绝未知、越界
  或错误类型输入。
- [WU-0316] 第二个 exact APK 在真实 SWF 加载阶段稳定超过单次 2 亿 guest tick；耗尽 PC
  `0x102eb2b0` 已反汇编定位到正常 `InterpolateColours` 计算而非自旋。未验证的预算实验
  已撤销，现状记入 KI-0008。
- [WU-0315] VFS 现可从显式、受检的 Profile 工作目录解析相对 guest 路径，并继续拒绝
  traversal；第二个 exact APK 的 `./data/...` 资源已命中 external 数据，原空对象 fault
  消失并推进至独立 tick-budget 边界，全量 CTest 通过。
- [WU-0314] legacy phone-language 回调现从统一受检 Locale 配置派生确定性语言索引，
  支持 ISO-639 两/三字母代码且不读取宿主区域设置；第二个 exact APK 单帧干净退出，
  120 帧 smoke 推进至独立 A32 guest fault，全量 CTest 通过。
- [WU-0313] legacy Java `process.exit` 现发布线程安全、可查询的 guest 会话退出请求，
  GLSurfaceView 前端观察请求后执行正常 lifecycle/session teardown；宿主进程不会被 handler
  直接终止，第二个 exact APK 已完成单帧有界运行并干净退出，全量 CTest 通过。
- [WU-0312] 第二个 exact Profile 已以纯数据声明 JADX/ELF 共同确认的 30 项 GLMediaPlayer
  static Java lookup，复用通用 audio implementation id 并保留新增语义的显式命名；真实 APK
  已越过 audio native init 并推进至下一独立边界，全量 CTest 通过。
- [WU-0311] 第二个 exact Profile 已以纯数据声明 JADX/ELF 共同确认的 15 项 activity static
  Java lookup，复用通用 implementation id 且生产代码无游戏分支；真实 APK 已越过 activity
  native init，推进至下一独立边界，全量 CTest 通过。
- [WU-0310] GLES1 client-array descriptor 现于创建/reset 后恢复规范 size/type/stride/
  pointer/buffer/enable 默认值；Dungeon Hunter 已越过状态保存/临时绘制/恢复与全部 GL 初始化，
  明确推进至独立 JNI 缺口 `sendAppToBackground()V`，全量 CTest 通过。
- [WU-0309] GLES1 已补齐 current-matrix 右乘、current normal 与六项 eye-space clip plane，
  fixed shader 对启用平面执行真实裁剪；Dungeon Hunter 目标 ELF 的 74 个 GL import 已达到
  74/74 显式 handler、缺口 0，实跑保持在独立 client-array type 边界，全量 CTest 437/437。
- [WU-0308] GLES1 已补齐 buffer name 生命周期、`glBufferData`/`glBufferSubData` 与受检
  `glReadPixels`，删除绑定对象同步 transfer state；第二个 exact APK 无倒退并保持在独立
  client-array type 边界，全量 CTest 436/436 通过。
- [WU-0307] GLES1 已补齐 `glGetBooleanv`、`glIsEnabled`、`glGetPointerv`、`glTexEnvf`，
  并扩展 `glGetIntegerv` 的 client-array descriptor 与 legacy blend alias；第二个 exact APK
  已越过相关查询，明确停在后续 client-array type 边界，全量 CTest 436/436 通过。
- [WU-0306] GLES1 已发布 clear-stencil、depth-range、line-width、polygon-offset、三项
  stencil 与两项 point handler；point size/min/max 由 fixed shader 消费，第二个 exact APK
  已从 `glPointSize` 推进至 `glIsEnabled`，全量 CTest 436/436 通过。
## 目标 ELF 尚未实现的 GL 入口

以下清单以 `docs/demo/games/libasphalt5.so` 的 62 个 GL import 与 WU-0264 后的显式
GLES1 handler 对照得出；当前已实现 62 个，尚余 0 个。它只表示该目标实际导入且尚未
实现的入口，不代表完整 GLES1 命名空间。

- 无。

## 下一步（按优先级）

1. 为第二个 exact Profile 声明受检的单次调用预算，并用 bounded smoke 验证首次加载是否
   能返回；不得按标题在生产代码中猜测预算。
2. 建立可自动判定的 exact-APK 主界面/readback 检查，替代人工视觉验收。
3. 为其他声明音频 source 的 Profile 补齐 OBB/external 前端挂载路径。

## 阻塞

- KI-0008：第二个 exact APK 的首次 SWF 加载超过当前单次 guest call tick 上限；下一轮需
  在保持有界失败的前提下通过 Profile 预算测量加载是否能够完成。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
