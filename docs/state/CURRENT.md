# 当前状态

更新：2026-08-06 · M5 已打开

## 当前阶段

- M0、M1、M2、M3、M4 已完成并验收。
- M5 去硬编码正在推进；Profile 精确 bootstrap、单一 plan、runtime catalog 与已导入
  asset bundle 均已闭合；当前仍不提交具体游戏 profile。
- Windows/MSVC、Linux/x64 与 macOS/arm64 均在同一主仓库 commit `f1b59bb` 上以 ANGLE
  开启和 warnings-as-errors 通过严格全量 CTest 302/302。记录见
  [M4-ACCEPTANCE.md](M4-ACCEPTANCE.md)。

## 已验收基线

| 里程碑 | 状态 | 验收记录 | Work Unit |
| --- | --- | --- | --- |
| M0 工程地基 | 完成 | [M0-ACCEPTANCE.md](M0-ACCEPTANCE.md) | `docs/tasks/m0/` |
| M1 内核与跨平台 | 完成 | [M1-ACCEPTANCE.md](M1-ACCEPTANCE.md) | `docs/tasks/m1/` |
| M2 Bionic 与 Syscall | 完成 | [M2-ACCEPTANCE.md](M2-ACCEPTANCE.md) | `docs/tasks/m2/` |
| M3 JNI 与 Java 框架 | 完成 | [M3-ACCEPTANCE.md](M3-ACCEPTANCE.md) | `docs/tasks/m3/` |
| M4 ANGLE 与 NativeActivity | 完成 | [M4-ACCEPTANCE.md](M4-ACCEPTANCE.md) | `docs/tasks/m4/` |

能力的机器可读现状以仓库根目录 `capabilities.toml` 为准；本文件不重复维护完整能力历史。

## 最近完成

- [WU-0215] ProfileAssetBundle 以不可变所有权汇总 VFS/audio 字节，在装配前拒绝路径
  歧义、重复 mount/entry/audio 与空资产。macOS/arm64 warnings-as-errors 构建及全量
  CTest 346/346 通过。
- [WU-0214] plan/bootstrap 高层入口直接消费单一 ProfileRuntimeCatalog，Java handler 与
  input mapper 不再由调用方分离拼接；底层组合 API 与 exact/default 语义保持不变。
  macOS/arm64 warnings-as-errors 构建及全量 CTest 343/343 通过。
- [WU-0213] ProfileRuntimeCatalog 以不可变所有权汇总通用 Java handler 与 input mapper；
  implementation id 规范唯一、handler 非空并仅支持精确查询。macOS/arm64
  warnings-as-errors 构建及全量 CTest 343/343 通过。
- [WU-0212] 单一 bootstrap 先执行三重身份精确匹配，再把 exact 或显式 generic default
  送入同一 ProfileSessionPlan；非法身份和已选路径装配失败绝不回退。macOS/arm64
  warnings-as-errors 构建及全量 CTest 341/341 通过。
- [WU-0211] ProfileSessionPlan 事务式汇总 lifecycle、VFS、Java、input、audio，唯一拥有
  VFS/JNI 状态、输入目录和预解析音频；任一子装配失败均不发布部分计划。macOS/arm64
  warnings-as-errors 构建及全量 CTest 337/337 通过。
- [WU-0210] Profile cover music 以 source + path 精确解析编码资源并保留 loop，经通用
  MusicPlayer 提交；资源和播放器失败均明确暴露，不声明解码、混音或设备播放能力。
  macOS/arm64 warnings-as-errors 构建及全量 CTest 334/334 通过。
- [WU-0209] 新增不可变的 code-defined input mapper catalog；Profile input id 精确选择
  通用 mapper，无声明时使用 catalog 显式默认项，未知、重复、非法或空 mapper 均明确
  失败。macOS/arm64 warnings-as-errors 构建及全量 CTest 330/330 通过。
- [WU-0208] Profile Java 类与实例方法装配到全新的 JNI registry/engine，仅注册 Profile
  实际引用的通用 handler；缺失、重复、空 handler 及非法 JNI 声明均明确失败，且失败
  不发布半成品。macOS/arm64 warnings-as-errors 构建及全量 CTest 330/330 通过。
- [WU-0207] Profile data mount 与已导入文件以 guest 根 + source 精确配对，在全新通用
  VFS 中保留 APK/OBB/external 来源和可写性；required mount/manifest、working directory、
  额外重复输入和来源错配均明确失败，失败不发布半成品。macOS/arm64 warnings-as-errors
  构建及全量 CTest 324/324 通过。
- [WU-0206] native_activity、gl_surface_view、custom_jni 映射为强类型通用回调路由，
  三者共用输入→生命周期→渲染→present→音频→调度→统一 Clock 计时的唯一帧实现；
  回调失败锁定状态但保留显式清理路径。macOS/arm64 warnings-as-errors 构建及全量
  CTest 321/321 通过；同时移除当前 SDK 不可用的浮点 `from_chars`，以 classic locale
  严格解析 Profile 浮点纯数据并补正反回归。

## 下一步（按优先级）

1. 把 ProfileAssetBundle 接入 plan/bootstrap，消除分离 VFS/audio span 的错配风险。
2. 建立首批 profile 迁移清单并补齐通用 handler/题库，禁止向 `src/` 增加游戏特判。
3. M4 范围外项目（窗口 surface、未绑定 GLES2、通用多库入口等）不得伪造成功，继续以
   能力账本为准。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
