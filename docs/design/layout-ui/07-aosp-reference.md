# 07 · AOSP 4.4.4 参考策略

## 1. 本地参考根

本地 OGPlay 工作区已经有 Android 4.4.4 `frameworks/base`，本文统一使用：

```text
.local/aosp/frameworks/base
```

所有路径均相对仓库根。该目录是**语义参考**，不是要编译进 OGPlay 的运行时依赖。

原则与 DexVM 的 AOSP 参考策略一致：

> **取可观察语义，不取 Android 系统结构。**

layout UI WU 不应凭记忆实现 Android 细节，也不应因为 AOSP 有一个类就把它完整移植。

## 2. 原始完整链路

用于理解边界：

```text
Activity.setContentView()
    ↓
PhoneWindow.setContentView()
    ↓
LayoutInflater.inflate()
    ↓
Resources / AssetManager / XmlBlock
    ↓
View / ViewGroup tree
    ↓
ViewRootImpl.performTraversals()
    ↓
measure → layout → draw
    ↓
Canvas/Skia 或 HWUI
    ↓
Surface
```

OGPlay 只复刻中间的 guest-observable 子集：

```text
resource → inflate → tree → measure/layout → draw overlay → input
```

## 3. 对照表

| OGPlay 组件/语义 | AOSP 参考路径 | 重点函数/内容 | 只参考什么 |
| --- | --- | --- | --- |
| Activity content 入口 | `.local/aosp/frameworks/base/core/java/android/app/Activity.java` | `setContentView` | Activity 如何把 content 交给 Window 的可观察时机 |
| Window content / attach target | `.local/aosp/frameworks/base/policy/src/com/android/internal/policy/impl/PhoneWindow.java` | `setContentView`, `installDecor` | content parent 与 `<merge>` attach 的概念 |
| Inflation | `.local/aosp/frameworks/base/core/java/android/view/LayoutInflater.java` | `inflate`, `createViewFromTag`, `createView`, `rInflate` | XML 递归、tag→View、attach 顺序 |
| Layout resource | `.local/aosp/frameworks/base/core/java/android/content/res/Resources.java` | `getLayout`, `loadXmlResourceParser` | resource id → compiled XML 的语义 |
| Asset/XML bridge | `.local/aosp/frameworks/base/core/java/android/content/res/AssetManager.java`, `.local/aosp/frameworks/base/core/java/android/content/res/XmlBlock.java` | XML/resource access | typed AXML/resource value 语义 |
| Base View | `.local/aosp/frameworks/base/core/java/android/view/View.java` | `measure`, `layout`, `draw`, visibility/id/listener APIs | MeasureSpec、visibility、geometry、draw/input observable state |
| ViewGroup | `.local/aosp/frameworks/base/core/java/android/view/ViewGroup.java` | child attach, measure/layout helpers, `dispatchDraw`, touch dispatch | hierarchy/Z-order/child visibility/input ordering |
| Traversal | `.local/aosp/frameworks/base/core/java/android/view/ViewRootImpl.java` | `performTraversals`, `performMeasure`, `performLayout`, `performDraw` | 三阶段顺序与 invalidation/requestLayout 因果 |
| FrameLayout | `.local/aosp/frameworks/base/core/java/android/widget/FrameLayout.java` | `onMeasure`, `onLayout` | match/wrap、gravity、overlap |
| LinearLayout | `.local/aosp/frameworks/base/core/java/android/widget/LinearLayout.java` | `measureHorizontal`, `measureVertical`, `layoutHorizontal`, `layoutVertical` | orientation、gravity、weight、GONE |
| RelativeLayout | `.local/aosp/frameworks/base/core/java/android/widget/RelativeLayout.java` | `onMeasure`, dependency/order 逻辑 | 高频 relative rules 与 dependency/cycle 行为 |
| TextView | `.local/aosp/frameworks/base/core/java/android/widget/TextView.java` | `onMeasure`, `onDraw` | text size/measure/gravity 的 observable 子集 |
| ImageView | `.local/aosp/frameworks/base/core/java/android/widget/ImageView.java` | `onMeasure`, bounds/scale 配置、`onDraw` | intrinsic size 与 scaleType |
| ScrollView | `.local/aosp/frameworks/base/core/java/android/widget/ScrollView.java` | measure/layout/scroll | 单 child vertical scroll 子集 |
| Drawable | `.local/aosp/frameworks/base/graphics/java/android/graphics/drawable/` | Bitmap/Color/StateList 等 | intrinsic size、state selection、padding；不移植 Canvas |
| Software draw 链 | `.local/aosp/frameworks/base/core/java/android/view/ViewRootImpl.java` | `drawSoftware` | “View tree 最终变为像素”的因果，不复制 Surface/Skia |
| HWUI | `.local/aosp/frameworks/base/core/java/android/view/HardwareRenderer.java`, `.local/aosp/frameworks/base/libs/hwui/` | draw/display list | 仅用来确认层次/合成语义；不进入 OGPlay 实现 |

## 4. 每个 WU 的参考方法

实现前先把问题缩成“guest 能观察什么”。

例如 LUI-6 的问题不是：

> 如何移植 `LinearLayout.java`？

而是：

> 在 Android 4.4.4 中，horizontal LinearLayout 对 `GONE` child、wrap_content ImageButton
> 和 parent `gravity=center_horizontal` 的 measure/layout 结果是什么？

然后：

1. 阅读 `LinearLayout.java` 的相关 measure/layout 方法；
2. 阅读 `View.java` 的 visibility / MeasureSpec 语义；
3. 用小 fixture 把结果写成 OGPlay test；
4. 实现最小算法；
5. 与 AOSP 不同的 intentional simplification 写进 test/doc/capability。

## 5. P0 重点阅读顺序

### LUI-1 / LUI-4：resource + inflate

```text
.local/aosp/frameworks/base/core/java/android/content/res/Resources.java
.local/aosp/frameworks/base/core/java/android/view/LayoutInflater.java
.local/aosp/frameworks/base/policy/src/com/android/internal/policy/impl/PhoneWindow.java
```

关注：

- `inflate(resource, root, attachToRoot)`；
- `<merge>` 的 parent/attach 要求；
- XML child 顺序；
- id/resource typed value。

### LUI-5：FrameLayout

```text
.local/aosp/frameworks/base/core/java/android/view/View.java
.local/aosp/frameworks/base/core/java/android/widget/FrameLayout.java
```

关注：

- `MeasureSpec`；
- match/wrap；
- child gravity；
- parent padding/margin。

### LUI-6：LinearLayout

```text
.local/aosp/frameworks/base/core/java/android/widget/LinearLayout.java
```

重点对照：

```text
measureHorizontal
layoutHorizontal
GONE child handling
gravity
```

不要复制 baseline alignment/divider 等未命中的复杂面。

### LUI-9：input

```text
.local/aosp/frameworks/base/core/java/android/view/View.java
.local/aosp/frameworks/base/core/java/android/view/ViewGroup.java
```

关注：

- visibility 对 dispatch 的影响；
- touch target；
- listener consume；
- click 的 down/up 可观察行为；
- child Z-order。

OGPlay 可以有更简单的 dispatcher，但测试结果必须覆盖 title 依赖的行为。

## 6. 允许简化的地方

有明确文档/测试时可简化：

- 不构造 Android `DecorView`；
- 不创建真实 `AttributeSet` Java object 给所有 constructor；
- 不执行完整 style/theme；
- 不保留 AOSP 私有 measure cache；
- 不做 invalid region 优化；
- 不做 display list；
- 不做 Canvas/Skia；
- 不做 accessibility；
- 不做 View animation；
- 不做复杂 pointer splitting/multi-touch，除非 title 命中。

## 7. 不允许“简化掉”的 P0 语义

Asphalt gate 依赖，不能近似：

- `<merge>` hierarchy；
- `findViewById` identity；
- XML id 与 `View.getId` 一致；
- `GONE` 不参与 LinearLayout space；
- `INVISIBLE` 保留 layout、但不 draw/input；
- bottom + center_horizontal geometry；
- document Z-order；
- UI overlay 在 VideoView 上层；
- no UI target → Activity touch；
- visible ImageButton → guest click listener。

## 8. 分歧仲裁

当 AOSP 实现、现有 OGPlay 行为和真实 APK 需求不一致：

1. 先确认 Android 4.4.4 AOSP 的可观察行为；
2. 再看 OGPlay 现有契约是否已经冻结更窄但兼容的语义；
3. 用最小 fixture/真实 title scenario 复现；
4. 如果选择有界简化，必须写清 capability 边界与机器测试；
5. 如果需要改变项目架构契约，走 ADR。

不要为了“更像 Android”破坏 OGPlay 已验收的线程、Clock、ANGLE surface、VFS 或 session
所有权。
