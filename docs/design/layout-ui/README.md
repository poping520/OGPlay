# Layout UI · 有界 Android XML UI 方案设计

本目录定义 OGPlay 对 Android 4.4 时代 Java/XML UI 的**有界兼容方案**。首个必须闭合的
真实场景是启动视频上的 Java `ImageButton`：游戏通过 `Activity.setContentView()` inflate
XML，运行时切换按钮可见性，用户点击按钮后回调游戏自己的 `OnClickListener`。

本方案不是 Android UI Framework 的移植计划。目标是把游戏进程**直接观察和依赖**的
View/Layout 语义做成一个小而正规的运行时：资源与 Binary XML → View 树 → measure/layout
→ UI overlay → pointer hit-test → guest Java listener。Android 的 WindowManager、
`ViewRootImpl`、Canvas/Skia、HWUI、SurfaceFlinger 都不进入 OGPlay。

## 效力声明

- 本目录是 layout UI 子系统的长期设计上下文。实施启动后，代码与对应 `MODULE.md`
  契约逐步接管；本目录保留设计溯源与 WU 边界。
- 项目元规范继续由 `AGENTS.md`、`docs/state/CURRENT.md`、`capabilities.toml` 和当前
  里程碑任务单约束。本文只展开 layout UI 特有的工程规则。
- 若实施发现必须改变既有模块依赖方向、ANGLE/SDL surface 所有权或其他已冻结架构，
  先写 ADR，再继续实现；不得在单个 UI WU 内顺手改写架构。
- 产品代码不得出现 title、厂商、包名或 `R.id.skip` 等专用分支。真实 title 只作为
  scenario/evidence。

## 一句话架构

> `loader` 严格解析 APK 资源与 AXML；新增 `runtime/ui` 持有唯一 View 树、布局、绘制
> 与 hit-test 事实；`runtime/integration/dexvm_android` 只把 guest Java View 对象和
> listener 绑定到 UI 节点；session 已有 present 边界把 Video/GLES 基底与透明 UI overlay
> 合成后提交。

```text
R.layout.xxx
    │
    ▼
resources.arsc + compiled AXML
    │
    ▼
loader::resource / binary_xml
    │
    ▼
DexVm UI inflater ──────── guest View intrinsic
    │                           │
    └────────────┬──────────────┘
                 ▼
              UiTree
                 │
          measure / layout
                 │
        ┌────────┴────────┐
        ▼                 ▼
   UiRenderList       Ui hit-test
        │                 │
   RGBA overlay       UiNodeId target
        │                 │
        ▼                 ▼
  frame compositor    guest listener
        │
        ▼
     present
```

## 实现状态

LUI-1..15 与验收修复 WU-M10-016 已完成。严格 AXML/ARSC loader、generic inflater、UiTree measure/layout、RGBA
renderer、present composition、pointer dispatch、动态 hierarchy、TextView/Button、
RelativeLayout、include/resource 与 ImageView scaleType 已沿上图依赖方向闭合。

UiTree 现在是 hierarchy/id/visibility/layout/geometry 的唯一事实源；旧 `view_registry`、
`widget_states`、`LayoutViewFact/layout_views` 和 fullscreen/edge-row hit-test 特判均已删除。
Asphalt 6 启动视频 Skip 与 Asphalt 5 title-flow 各自完成三轮关闭 survey 的 exact scenario。
未被真实执行命中的静态候选 API 不因本里程碑扩张。逐项能力和限制仍以
`capabilities.toml`、模块 `MODULE.md` 与 `docs/state/CURRENT.md` 为准。

## 阅读顺序

| 篇 | 内容 |
| --- | --- |
| [01 · 目标、非目标与覆盖面](01-scope.md) | Asphalt 6 问题、P0/P1 能力边界、成功标准 |
| [02 · 核心架构与状态所有权](02-architecture.md) | 模块边界、UiTree、DexVM binding、traversal、composition |
| [03 · 资源、AXML 与 inflation](03-resource-inflation.md) | typed attributes、resource resolver、`<merge>`/`<include>`、tag registry |
| [04 · Measure/Layout/Render/Input](04-layout-render-input.md) | Frame/Linear/Relative、visibility、overlay、VideoView、pointer dispatch |
| [05 · 验证、记账与诊断](05-verification.md) | unit/golden/scenario、Asphalt P0 gate、日志、安全与性能 |
| [06 · Work Unit 实施计划](06-work-units.md) | 设计级 WU、依赖、机器出口、实施顺序 |
| [07 · AOSP 4.4.4 参考策略](07-aosp-reference.md) | `.local/aosp/frameworks/base` 对照文件与“取语义不取结构”纪律 |

## 状态

本设计的 LUI-1..15 已于 M10 实施并经 WU-M10-016 正式验收；本文继续作为设计溯源，能力现状以
`capabilities.toml` 和模块契约为准。
