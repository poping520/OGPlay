# 04 · Measure、Layout、Render 与 Input

## 1. 最小 traversal 模型

OGPlay 不实现 `ViewRootImpl`，但保留 Android UI 最核心的因果顺序：

```text
resource/state mutation
    ↓
requestLayout / invalidate
    ↓
measure
    ↓
layout
    ↓
build render list
    ↓
raster overlay
    ↓
present composition
```

input 只使用**最近一次成功 layout 的 resolved geometry**。如果一个 structural mutation
使 geometry dirty，在同一 guest frame 的 input/present 边界要有确定规则；推荐在下一次
input hit-test 前先完成必要 layout，避免用旧 bounds 接新状态。

## 2. MeasureSpec

实现有界 Android 风格：

```cpp
enum class MeasureMode {
    Exactly,
    AtMost,
    Unspecified,
};

struct MeasureSpec {
    MeasureMode mode;
    std::int32_t size;
};
```

### fixed

解析后的 px，受 parent 约束。

### match_parent

在 parent 给出的可用范围内占满。

### wrap_content

由 intrinsic content size 决定：

- ImageView/ImageButton：drawable intrinsic size + padding；
- TextView/Button：text measure + padding；
- ViewGroup：由 child measure 汇总；
- generic View：minimum/0，具体策略写测试锁定。

## 3. Visibility

这是 P0 硬语义：

| 状态 | measure/layout | draw | input |
| --- | --- | --- | --- |
| `VISIBLE` | 是 | 是 | 是 |
| `INVISIBLE` | 是 | 否 | 否 |
| `GONE` | 否 | 否 | 否 |

`GONE` 从 LinearLayout 主轴空间计算中完全移除；`INVISIBLE` 保留位置。

## 4. FrameLayout

Activity `UiContentRoot` 使用 FrameLayout 语义。

P0：

- child 独立 measure；
- match_parent；
- margin/padding；
- `layout_gravity`：
  - left/right；
  - top/bottom；
  - center；
  - center_horizontal；
  - center_vertical；
- document order 决定 draw Z-order。

Asphalt 结果：

```text
VideoView      [0,0,w,h]
TextView       bottom
LinearLayout   bottom
```

后出现的 bottom controls 必须在 VideoView 上层。

## 5. LinearLayout

P0 先做 horizontal；P1 扩 vertical 和 weight。

支持：

```text
orientation
gravity
child layout_gravity
margin
padding
visibility
weight
```

### horizontal measure

1. 跳过 `GONE`；
2. measure 非 weight 或已知 size child；
3. 累加 child width + margin；
4. 高度取最大 child + padding；
5. parent gravity 决定 row 在可用空间的整体偏移；
6. child layout_gravity 可覆盖交叉轴位置。

### Asphalt button row

五个 sibling 为 `GONE`，所以参与主轴 measure 的只有 skip：

```text
rowContentWidth = skip.measuredWidth
skipX = (parentWidth - skipWidth) / 2
```

这由通用 `gravity=center_horizontal` 得出，产品代码不得出现专用坐标。

## 6. RelativeLayout

P1 按真实高频规则做有界 dependency solver，不复制全部 AOSP corner cases。

水平规则：

```text
alignParentLeft / Right
centerHorizontal
toLeftOf / toRightOf
alignLeft / alignRight
```

垂直规则：

```text
alignParentTop / Bottom
centerVertical
above / below
alignTop / alignBottom
```

`centerInParent` 同时作用两轴。

实现建议：

1. 以 android id 建 sibling reference；
2. 水平/垂直分别建依赖图；
3. topological order；
4. resolve position；
5. cycle → structured gap + strict failure。

不得在 cycle 时随机按 hash/container iteration order 排版。

## 7. TextView / Button

P0 Asphalt skip 不依赖文字绘制，因此 text raster 不阻塞前几 WU。

P1 state：

```text
text
textColor
textSize
gravity
singleLine
maxLines
```

Java mutation：

```text
setText          → layout + draw dirty
setTextColor     → draw dirty
setTextSize      → layout + draw dirty
setGravity       → layout + draw dirty
```

text renderer 必须可同时：

- measure；
- rasterize；
- 跨平台结果足够稳定用于测试。

优先使用项目可固定版本的轻量字体栅格 backend；不要绑定宿主 UI toolkit。

## 8. ImageView / ImageButton

P0：

- `src`；
- intrinsic size；
- bitmap draw；
- alpha；
- `VISIBLE/INVISIBLE/GONE`；
- transparent/minimal background。

P1 `scaleType`：

```text
CENTER
CENTER_INSIDE
FIT_CENTER
FIT_XY
CENTER_CROP
```

XML 与 Java setter 共用同一 `ImageState`。

## 9. UiRenderList

renderer 不遍历 guest object；layout 后输出简单命令：

```cpp
using UiDrawCommand = std::variant<
    DrawSolidRect,
    DrawBitmap,
    DrawText,
    PushClip,
    PopClip
>;
```

命令包含已经 resolve 的 screen rect、alpha、resource handle。这样：

- guest/DexVM identity 不进入 rasterizer；
- golden test 可以直接构造 UiTree；
- 后续可替换 CPU/GPU backend 而不改 layout。

## 10. RGBA overlay

首版 CPU raster：

```cpp
struct UiOverlayFrame {
    std::uint32_t width;
    std::uint32_t height;
    std::vector<std::uint8_t> rgba8;
};
```

初始全透明。标准 source-over：

```text
out.rgb = src.rgb * src.a + dst.rgb * (1 - src.a)
out.a   = src.a + dst.a * (1 - src.a)
```

实现可以用整数定点保证 deterministic。

只在 draw dirty 时重建 overlay；不变 UI 不应每帧重解码 drawable/重建文本。

## 11. VideoView 是 external-content node

UI layout engine 只给 VideoView：

```text
screen rect
visibility
z-order slot
```

现有 video subsystem 继续提供 frame/audio/position。

禁止：

- UI renderer 自己打开 FFmpeg；
- video module 知道 `ImageButton`；
- 为 Asphalt 在 video path 内硬画 skip。

## 12. Composition

目标层顺序是 Activity content tree 的 draw order。P0 可以先以当前真实需要落地：

```text
session base/background
    ↓
VideoView frame at resolved rect
    ↓
ordinary UI overlay
    ↓
present
```

如果后续出现“UI child 在两个 external surface 之间”的真实题目，再把 render list 扩成统一
layer list；P0 不需要预先做完整 SurfaceView compositor。

不能把 UI overlay 直接塞进 `video::ComposeRgbaOnCanvas()` 形成 video→UI 反向依赖。
composition 应位于同时拥有 base/video/UI 输入的 integration/session 边界。

## 13. Pointer 坐标

沿用现有 host → guest content rect 坐标映射。UI router 接收的是 guest surface coordinates，
不重新读取 SDL window size，也不自己处理黑边/supersample。

```text
host pointer
    ↓
existing content mapping
    ↓
guest x/y
    ↓
UiTree hit-test
```

## 14. Hit-test

使用 resolved tree，按 draw order 逆序：

```text
topmost child
    ↓
...
    ↓
parent/back layers
```

条件：

```text
visibility == VISIBLE
enabled
screen_frame contains point
inside accumulated clip
```

是否 clickable/touch-listener 决定该 node 是否成为 target。P0 不需要复杂 transformed matrix；
若真实 title 命中 rotation/scale transform，另立 WU。

## 15. Touch dispatch

### ACTION_DOWN

1. hit-test topmost target；
2. 有 `OnTouchListener` 时调用 `onTouch(view,event)`；
3. 若 listener consume，则 capture；
4. 否则按 View clickable/click-listener 规则 capture；
5. 无 target → 回退现有 Activity `onTouchEvent()`。

### ACTION_MOVE

路由到 captured node；首版不要求完整 gesture cancel 体系，但 pointer 离开时仍要记住 capture。

### ACTION_UP

如果 captured node：

- 仍 `VISIBLE`；
- 仍 enabled；
- up 位于有效 bounds；
- listener 没有提前 consume/cancel；

则调用：

```java
OnClickListener.onClick(view)
```

最后释放 capture。

### Activity fallthrough

**只有 UI 没有消费时**才进入现有 Activity touch。这个顺序是 Asphalt P0 必须证明的。

## 16. Asphalt 两阶段交互

初始：

```text
skip = INVISIBLE
```

第一次点击视频区域：

```text
hit-test → skip 不可命中
        → no UI target
        → Activity.onTouchEvent(DOWN)
        → guest Java: skip.setVisibility(VISIBLE)
        → draw dirty
```

下一 present：skip 出现。

第二次点击 skip：

```text
hit-test → skip
        → capture
        → UP
        → guest OnClickListener.onClick(skip)
```

这条链必须用真实 Java listener 测，不接受“host test 直接调用 listener”替代 E2E。

## 17. 动态 View

P1：

```java
ViewGroup.addView()
removeView()
updateViewLayout()
setContentView(View)
```

全部修改同一 UiTree。

`setContentView(View)`：

- 确保 guest object 有 binding/node；
- detach old content generation；
- attach 到 synthetic content root；
- request layout。

这让 XML UI 和 Java 动态 UI 共用同一系统。

## 18. 几何 getter

P1 至少：

```text
getLeft/getTop/getRight/getBottom
getWidth/getHeight
```

读取最近成功 layout 的 `frame`。如果 guest 在第一次 traversal 前读取，行为要对照 AOSP/真实
title 决定，不允许每个 getter 自己临时猜尺寸。
