# 02 · 核心架构与状态所有权

## 1. 模块边界

建议新增：

```text
include/ogplay/runtime/ui/
src/runtime/ui/
```

该模块只负责宿主侧 Android View/Layout 的有界语义，不执行 guest Java。

### `loader`

继续拥有“不可信 APK 字节 → 严格结构”的职责：

- `resources.arsc`；
- compiled AXML；
- typed resource value；
- drawable 文件定位。

它不决定 LinearLayout 怎么排 child，也不保存 View runtime state。

### `runtime/ui`

新增低耦合 UI 服务：

- `UiTree` / `UiNode`；
- layout params / metrics；
- measure/layout；
- visibility/enabled/clickable 等 host-observable state；
- render list；
- RGBA overlay raster；
- hit-test；
- dirty state。

**不依赖 DexVM listener/ref 语义，不调用 guest。**

### `runtime/integration/dexvm_android`

保持 guest 平台 API binding：

- XML tag 对应哪个 Android intrinsic class；
- `VmObjectRef ↔ UiNodeId` 双向 binding；
- `findViewById` 把 UiNodeId 转回同一 guest object；
- `View.setVisibility/setText/...` 转发到 `runtime/ui`；
- 保存/调用 `OnClickListener`、`OnTouchListener` 的 guest refs；
- 把 hit-test 返回的 UiNodeId 路由到 guest callback。

individual intrinsic handler 不再包含布局算法。

### `video`

继续只负责视频 player/frame/audio/seek。`runtime/ui` 可以把 VideoView 表示为一个
external-content node，但不得依赖 FFmpeg，也不得自己解码视频。

### session / integration present 边界

现有 session 持有 managed surface 和最终 present；组合层放在这个所有权边界：

```text
base GLES/frame
    ↓
VideoView layers
    ↓
UiOverlay
    ↓
present
```

不得让 `frontend` 或 `video` 反向拥有 UI compatibility behavior。

## 2. 唯一事实源：UiTree

当前 `view_registry`、`layout_views`、`widget_states` 分别保存 identity、geometry 和状态，
导致语义容易分叉。目标结构：

```cpp
using UiNodeId = std::uint64_t;

struct UiNode {
    UiNodeId id;
    UiNodeId parent;
    std::vector<UiNodeId> children;

    UiClass kind;
    std::uint32_t android_id;

    Visibility visibility;
    bool enabled;
    bool clickable;

    LayoutParams layout;
    Insets padding;

    Size measured;
    Rect frame;         // parent-local
    Rect screen_frame;  // content-root coordinates

    DrawableState background;
    std::optional<ImageState> image;
    std::optional<TextState> text;
    float alpha;

    bool layout_dirty;
    bool draw_dirty;
};
```

这里只示意数据归属，不要求字段名逐字照搬。核心不变量：

- hierarchy、android id、visibility、layout params、resolved geometry 只存在一个权威事实；
- renderer 与 hit-test 都读取同一 resolved `screen_frame`；
- `GONE` 是否参与布局只由 UiTree state 决定；
- guest listener ref 不塞入 `runtime/ui`，由 DexVM binding 层以 `UiNodeId` 为 key 保存。

## 3. DexVM binding

`DexVmAndroidContext` 增加 UI binding，而不是继续扩散 widget side table：

```cpp
struct DexVmUiBinding {
    std::unordered_map<std::uint64_t, UiNodeId> object_to_node;
    std::unordered_map<UiNodeId, dexvm::VmObjectRef> node_to_object;

    std::unordered_map<UiNodeId, dexvm::VmObjectRef> click_listener;
    std::unordered_map<UiNodeId, dexvm::VmObjectRef> touch_listener;
};
```

实际容器可按项目现有类型调整，但要锁定：

```text
one guest View object ↔ one live UiNode
```

### 查找路径

```text
Activity.findViewById(resource_id)
        ↓
UiTree.FindByAndroidId()
        ↓
UiNodeId
        ↓
DexVm binding
        ↓
same VmObjectRef
```

### `View.getId()`

直接读绑定 node 的 `android_id`。`setId()` 更新 UiTree id index；重复/无效 id 的处理按
AOSP 4.4 可观察语义校准。

## 4. Activity synthetic content root

Android 的 `<merge>` 需要一个 attach target。OGPlay 不需要完整 DecorView，但必须有
稳定的 layout parent。

每个 Activity content generation 创建一个不暴露给 guest 的：

```text
UiContentRoot
kind = FrameLayout
frame = [0, 0, surface_width, surface_height]
```

`setContentView(int)`：

- 非 `<merge>`：XML root View attach 到 UiContentRoot；
- `<merge>`：XML top-level children 直接 attach 到 UiContentRoot；
- Activity 切换：旧 generation 的 UI tree/bindings/listeners 按 lifecycle 一起失效；
- 不允许上个 Activity 的 node 继续参与 draw/hit-test。

这与现有 SurfaceHolder generation 的 teardown 思路保持一致。

## 5. LayoutParams

统一基础结构：

```cpp
enum class SizeMode {
    Fixed,
    MatchParent,
    WrapContent,
};

struct DimensionSpec {
    SizeMode mode;
    std::int32_t px;
};

struct LayoutParams {
    DimensionSpec width;
    DimensionSpec height;
    Insets margin;
    Gravity layout_gravity;
    float weight;
    RelativeRules relative;
};
```

`FrameLayout`、`LinearLayout`、`RelativeLayout` 在基础字段上读取各自额外参数，不能在
AXML parser 中硬编码“某游戏 button row”的几何。

## 6. UI Metrics

增加单一 metrics：

```cpp
struct UiMetrics {
    float density;
    float scaled_density;
    std::int32_t surface_width_px;
    std::int32_t surface_height_px;
};
```

所有 dp/sp 转换只能经过这里。若当前 session 尚没有可靠 guest density，P0 可明确使用
`1.0f` 作为 session fallback，并通过结构化日志/能力 note 暴露；不能把 density=1 的
近似藏在 binary XML parser 内。

## 7. Traversal

不实现 Choreographer/ViewRootImpl，只保留可观察三阶段：

```text
if layout dirty:
    Measure(content_root)
    Layout(content_root)

if draw dirty:
    BuildRenderList(content_root)
    RasterizeOverlay()
```

执行点放在 session frame/present 的稳定边界。

### `requestLayout()`

node 的 measure-affecting state 改变后，把自身到 root 标脏。

### `invalidate()`

只影响绘制的 state 改变后设置 draw dirty。

### 典型 mutation

| 调用 | layout dirty | draw dirty | input |
| --- | --- | --- | --- |
| `VISIBLE ↔ INVISIBLE` | 通常否 | 是 | 立即变化 |
| 任意 `↔ GONE` | 是 | 是 | 立即变化 |
| `setText` | 是 | 是 | bounds 下一 traversal 更新 |
| `setTextColor` | 否 | 是 | 不变 |
| `setImageResource` | 是 | 是 | bounds 下一 traversal 更新 |
| `addView/removeView` | 是 | 是 | hierarchy 立即变化 |

## 8. 生命周期与线程

UI mutation 跟随当前 DexVM/session 的 guest execution 边界，不新增 Android UI thread。
renderer 在 frame boundary 读取稳定 state；如果实现需要 snapshot，应是显式不可变 snapshot，
而不是让 renderer 跨线程读正在变化的 UiTree。

不要引入：

- 独立 wall-clock UI scheduler；
- 宿主 widget toolkit callback；
- 异步 guest callback；
- 与统一 Clock 无关的动画时间。

## 9. 迁移现有状态

迁移按 WU 渐进完成，但目标态必须删除/停用旧特判事实：

```text
Activity::view_registry        → UiTree id index + DexVm binding
context.widget_states          → UiTree state + listener binding
context.layout_views           → UiTree hierarchy/layout/geometry
support_widget_dispatch bounds → UiTree resolved geometry + generic hit-test
```

允许中间 WU 有 adapter，但 adapter 必须单向：旧调用读新事实。禁止两边都可写。

## 10. 依赖方向

目标方向：

```text
loader/core
    ↑
runtime/ui
    ↑
runtime/integration/dexvm_android
    ↑
session/frontend orchestration
```

`video`、`gles` 保持自己的下层职责；integration/compositor 可以同时消费它们和 UI 的输出，
它们不反向依赖 UI。

实施首个引入 `src/runtime/ui/` 的 WU 必须同步建立 `MODULE.md` 并更新模块索引；如果实际
代码需要破坏上述方向，停止该 WU，先通过 ADR 裁决。
