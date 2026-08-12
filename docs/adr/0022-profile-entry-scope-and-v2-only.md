# ADR-0022 · Profile 启动作用域与 v2-only 迁移

- 状态：Accepted
- 日期：2026-08-12
- 关联：[启动作用域裁剪设计](../design/entry-scope/README.md)、
  [ADR-0017](0017-bounded-dex-interpreter.md)
- Supersedes：ADR-0017 中“Profile v1 冻结并与 v2 并行”的过渡决定，以及
  Title Profile v1 的 `native_call` / `[[java.class]]` 人工重放路线。

## 背景

DexVM 已用 Asphalt 5 exact gate 证明：解释执行真实 Activity 生命周期可以完全替代
Profile v1 的手工 JNI 调用序列和 Java handler 映射。继续保留 v1 会形成两套启动语义，
并诱导新 title 通过补商业外壳 Java 面而不是收敛到游戏引擎作用域。

Asphalt 6 的 manifest launcher 会转入首启下载、DRM、推送和分析外壳；在 external
数据已经 provisioned 时，这条 store 路径不是游戏引擎运行所需的作用域。启动作用域
必须由通用机制表达，游戏差异只进入 `data/profiles/`，且数据前提不成立时明确失败。

## 决定

- Title Profile 只接受 schema v2；删除 v1 schema、解析、手工 Java 装配、
  `native_call` 解析/解析符号/生命周期重放代码及其专属测试。
- 所有精确 Profile 使用 `dex_activity`，应用类与方法只来自 APK DEX；平台面只来自
  intrinsic 目录，缺失能力继续记账并明确失败。
- v2 增加可选 `[runtime.entry]`：`launch_activity` 覆盖 manifest launcher；
  `[[runtime.presets]]` 在目标类真实完成初始化后写入受检静态字段。每条 preset 必须
  提供非空 `reason`，且只允许基元或 `java.lang.String`。
- 声明 entry/preset 的 Profile 必须有至少一个 required data manifest 事实；run-apk
  在启动 DexVM 前经统一 VFS 验证这些事实。缺失时不实例化 Activity。
- 方法中性化不是本 ADR 的首批出口；只有 exact 测试证明入口/事实预设仍不足，且方法
  符合设计中的非目标与返回类型红线时，才以独立 WU 引入。

## 后果

- Profile 启动语义只剩一条 DexVM 路线；旧 title 必须迁移到 v2 才能继续进入生产目录。
- 入口覆盖和预设属于结论级配置，必须有 schema 负例、运行时类/字段存在性检查及
  “关闭即失败”exact 证据；gap survey 仍仅是诊断工具。
- `src/` 不出现游戏名、厂商名或包名。Asphalt 6 的入口、字段和理由只存在于其 Profile。
