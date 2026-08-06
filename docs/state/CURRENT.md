# 当前状态

更新：2026-08-06 · M5 已打开

## 当前阶段

- M0、M1、M2、M3、M4 已完成并验收。
- M5 去硬编码正在推进；Profile 精确 bootstrap 的最高层入口已统一消费 runtime catalog
  与已导入 asset bundle，调用方不再分离拼接运行时和资产容器；当前仍不提交具体游戏
  profile。
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

- [WU-0223] APK launch preflight 串联唯一 Profile 候选与对应 API 的 Bionic 闭包；CLI
  不再限定 stored v7a 单库或手写依赖，根库、API、生命周期与 surface 均来自 Profile，
  可在不创建窗口/执行 guest 时输出受检事实。macOS/arm64 warnings-as-errors 构建及全量
  CTest 358/358 通过。
- [WU-0222] Bionic module set 从根 ELF 的 `DT_NEEDED` 递归拥有真实 guest 系统库闭包，
  排除 HLE boundary 与未使用来源；未知、缺失及 SONAME 矛盾明确失败，load bias 稳定分配
  且不碰 thunk 区。macOS/arm64 warnings-as-errors 构建及全量 CTest 357/357 通过。
- [WU-0221] APK Profile 入口组合 Manifest package/version 与全部 ARM library hash/ABI，
  只发布唯一四项精确候选；无匹配返回空，ABI 矛盾与多库命中明确失败。目标 APK 已由内存
  Profile 精确选中唯一 `armeabi` library。macOS/arm64 warnings-as-errors 构建及全量
  CTest 355/355 通过。
- [WU-0220] Title Profile v1 的 JSON schema、Python 校验器、C++ loader/catalog 现以同一
  强类型集合接受 `armeabi` 与 `armeabi-v7a`，非 ARM 或非法枚举明确失败；既有三重指纹
  匹配语义未扩大。macOS/arm64 warnings-as-errors 构建及全量 CTest 353/353 通过。
- [WU-0219] APK 32 位 ARM native library 目录只接受规范 `armeabi`/`armeabi-v7a`
  路径，拥有解压字节、稳定排序并产出 SHA-256，但不猜 main library；目标 APK 唯一条目
  已核对为 `armeabi`、1,919,371 字节与预期哈希。macOS/arm64 warnings-as-errors 构建
  及全量 CTest 353/353 通过。
- [WU-0218] APK 二进制 Manifest 以受检 UTF-8/UTF-16 string pool、namespace 与 typed
  attribute 产出身份/SDK 事实；目标 APK 的 package、versionCode/name 与 minSdk 已同
  独立工具对拍。macOS/arm64 warnings-as-errors 构建及全量 CTest 351/351 通过。
- [WU-0217] APK 统一条目读取支持 stored、三种 Deflate block 与有/无签名 ZIP32 data
  descriptor；目标 APK 的压缩 native ELF 已按 central/local/descriptor/CRC 严格解出。
  macOS/arm64 warnings-as-errors 构建及全量 CTest 348/348 通过。
- [WU-0216] plan/bootstrap 最高层入口统一消费 ProfileAssetBundle 与
  ProfileRuntimeCatalog；返回 plan 不依赖两个输入容器的生命周期，exact/default、失败
  与底层组合语义保持不变。macOS/arm64 warnings-as-errors 构建及全量 CTest 346/346
  通过。
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
## 下一步（按优先级）

1. 建立首批 profile 迁移清单，把遗留项逐条归类为通用缺陷、纯数据、真 quirk 或删除。
2. 按清单补齐通用 handler 与题库，再提交具体 profile；禁止向 `src/` 增加游戏特判。
3. M4 范围外项目（窗口 surface、未绑定 GLES2、通用多库入口等）不得伪造成功，继续以
   能力账本为准。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
