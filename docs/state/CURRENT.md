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
- [WU-0305] `AngleFrame` 已补齐 depth range、line width、polygon offset、stencil clear/
  function/mask/operation 的真实 ANGLE 调用面，并将 scalar/query 实现拆分为独立源文件；
  原生状态查询与全量 CTest 436/436 通过。
- [WU-0304] GLES1 `glGetIntegerv` 现从转换器返回矩阵栈、纹理/buffer binding、对齐及固定
  管线上限，并将兼容设备查询转发真实 ANGLE；guest 输出受检后原子提交，第二个 exact APK
  已越过该入口并明确停在下一独立缺口 `glPointSize`，全量 CTest 436/436 通过。
- [WU-0303] 第二个 exact Profile 已以 package/version/SO/ABI、1024x600 surface、15 个静态
  JNI lifecycle call 和 required external payload 闭合；真实 API 19 link preflight 完成
  5+2 模块与 50,746 个 relocation，全量 CTest 436/436 通过。
- [WU-0302] `run-apk --external-dir` 现将一个显式宿主目录精确挂到匹配 Profile 声明的
  external guest 根；声明数量、required 输入及 manifest 在窗口前受检，CTest 436/436
  通过，现有 Asphalt Profile 预检无回归。
- [WU-0301] 独立宿主数据目录现可事务索引为通用 external VFS mount，文件首次读取或
  非截断写入时才物化且修改只存在于会话内；不安全/空/歧义目录树明确失败，全量
  CTest 436/436 通过。
- [WU-0300] APK/OBB 只读 VFS backing 现仅挂载路径与尺寸，首次读取才物化、核对并缓存；
  Debug exact APK 单帧由约 12.75 秒稳定降至 3.78–3.81 秒，120 帧正常退出，全量
  CTest 434/434 通过。
- [WU-0299] SDL 窗口现以无混合复制呈现 opaque guest framebuffer，alpha=0 不再把有效
  RGB 混入黑底；透明红像素契约与 CTest 432/432 通过，用户确认 Debug exact APK 闪屏
  恢复完整白底、主界面无回归，424 帧后正常关闭。
- [WU-0298] 鼠标主键以 input mapper 生成固定 id 0 的成对单点触摸；悬停、非主键、
  黑边起始及跨设备事件均不注入，窗口外 release 夹紧闭合；CTest 432/432 通过，用户以
  Debug exact APK 确认拖拽可旋转车辆，3407 帧后正常关闭。
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
