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

## 当前代码基础

现有实现已经有一条“窄 inflation + 点击事实”路径，应**迁移和泛化**，不是推倒重写：

- `src/runtime/integration/dexvm_android/android_app_Activity.cpp`
  - `setContentView(int)` 能从 `resources.arsc` 找 layout 文件；
  - 解析 compiled XML；
  - 为固定 tag 创建 intrinsic 对象；
  - `android:id` 进入 `view_registry`；
  - 少量布局事实进入 `layout_views`。
- `src/loader/binary_xml.cpp`
  - 已有严格 AXML walker；
  - 当前只抽取少量 `id/layout_width/layout_height/gravity/layout_gravity/paddingTop/src`
    属性。
- `src/runtime/integration/dexvm_android/support_widget_dispatch.cpp`
  - 已有 visibility/click listener 与 hit-test；
  - bounds 只支持 fullscreen root 与顶部/底部的特定 horizontal `LinearLayout` 行。
- `src/runtime/integration/dexvm_android/android_view_View.cpp`
  - visibility 与 click listener 已有真实状态；
  - `getId()` 尚未绑定 XML id；
  - 很多绘制属性仍是 no-op。
- `src/runtime/integration/dexvm_android/android_view_ViewGroup.cpp`
  - 动态 `addView/removeView/updateViewLayout` 目前不维护真实 hierarchy。
- `src/video/`
  - 已有 VideoView 所需的确定性/FFmpeg player 与 RGBA frame 能力；
  - UI 子系统不得重复实现视频解码。

本方案的关键迁移原则是：**最终只保留一份 hierarchy/geometry/visibility 事实**。
`view_registry`、`layout_views`、widget-side geometry 和独立 hit-test 特判不能长期并存。

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

设计目标是给实施 AI 一个可直接执行的根节点；本文不宣称 capability 已完成。真正启动
任一 WU 前，仍必须按项目流程读取 `CURRENT.md`、当前任务单、相关 `MODULE.md`，并以
`capabilities.toml` 的当时状态为准。
