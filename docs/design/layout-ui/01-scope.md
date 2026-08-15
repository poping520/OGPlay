# 01 · 目标、非目标与覆盖面

## 1. 要解决的真实问题

Asphalt 6 启动时由 Java `Activity` 播放一段视频。页面的 UI 不是 native/GLES 游戏画面
的一部分，而来自 XML layout：

```text
<merge>
 ├─ VideoView      match_parent × match_parent
 ├─ TextView       bottom
 └─ LinearLayout   bottom + center_horizontal
      ├─ backward  ImageButton  GONE
      ├─ play      ImageButton  GONE
      ├─ pause     ImageButton  GONE
      ├─ forward   ImageButton  GONE
      ├─ stop      ImageButton  GONE
      └─ skip      ImageButton
```

Java 侧关键顺序是：

```text
setContentView(R.layout.videoview)
    ↓
findViewById(R.id.skip)
    ↓
skip.setVisibility(INVISIBLE)
    ↓
skip.setOnClickListener(...)
    ↓
VideoView.start()
```

视频播放超过约 3 秒后，`Activity.onTouchEvent(ACTION_DOWN)` 在“页面空白触摸”时把
`skip` 从 `INVISIBLE` 切到 `VISIBLE`；下一次点击可见的 skip 才执行 listener，停止视频并
进入后续 Activity。

因此真正问题不是“把一张 skip PNG 贴到屏幕上”，而是必须闭合：

```text
resource id
→ XML inflation
→ View identity
→ hierarchy
→ measure/layout
→ visibility
→ render
→ hit-test/fallthrough
→ guest OnClickListener
```

任何一步是假实现都会产生可观察错误。例如 `INVISIBLE` 仍能 hit-test，会让第一次触摸
无法落入 `Activity.onTouchEvent()`；`GONE` 仍占宽度，会让 skip 的水平位置错误；UI 在
VideoView 下层合成，会让按钮逻辑存在但看不见。

## 2. 当前能力与缺口

当前代码已经证明以下基础可复用：

| 能力 | 当前状态 | 本方案动作 |
| --- | --- | --- |
| `R.layout` id → APK layout path | 有 | 保留并泛化 resource resolver |
| compiled AXML walker | 有 | 改为通用 typed attribute 输出 |
| 固定 tag → intrinsic object | 有 | 从 `Activity.cpp` 移入 registry/inflater |
| `findViewById` | 有 | 改由 UiTree id index 回答 |
| visibility state | 有 | 移入 UiTree，成为 layout/render/input 同一事实 |
| click listener | 有 | listener 保持 integration 绑定，hit target 来自 UiTree |
| 特定 bottom button row bounds | 有 | 由正规 FrameLayout + LinearLayout 替代 |
| drawable 解码取得 ImageButton 尺寸 | 有 | 变成通用 drawable intrinsic measure |
| UI 像素绘制 | 无 | 新增透明 RGBA overlay |
| 通用 View hierarchy | 无 | 新增 UiTree |
| 通用 measure/layout | 无 | 实现有界布局器 |
| `View.getId()` 与 XML id 一致 | 无 | 由统一 id fact 修复 |
| 动态 `ViewGroup.addView/removeView` | 无 | P1 接入同一 UiTree |

迁移中不能让旧 `layout_views` 和新 UiTree 同时长期决定 input；每个 WU 的出口要锁定
“哪份事实已经成为唯一来源”。

## 3. P0：Asphalt 6 必须具备的能力

P0 只为闭合真实视频页面，但实现必须是通用语义：

### Resource / XML

- `R.layout.xxx`；
- layout compiled AXML；
- `@id` / `@drawable`；
- `<merge>`；
- `match_parent` / `fill_parent`；
- `wrap_content`；
- `gravity`；
- `layout_gravity`；
- `visibility`；
- `src`。

### View / Layout

- synthetic Activity content root（FrameLayout 语义）；
- `VideoView`；
- `TextView` 至少能建立节点；
- `LinearLayout` horizontal；
- `ImageButton`；
- `VISIBLE / INVISIBLE / GONE`；
- bitmap intrinsic width/height；
- bottom 与 center_horizontal；
- document order Z-order。

### Render / Input

- 透明 RGBA UI overlay；
- bitmap draw；
- alpha composition；
- UI 在 VideoView 上层；
- resolved bounds hit-test；
- topmost wins；
- `INVISIBLE/GONE` 不接收 input；
- 无 UI target 时回退现有 Activity touch；
- `OnClickListener.onClick(view)` 收到同一 guest View identity。

## 4. P1：建议覆盖的大多数旧游戏 UI 子集

P0 闭合后，再按 WU 扩到下面的“高收益子集”。这不是一次性承诺全部完成；每项都要求
真实测试或题库命中再推进 capability 状态。

### Layout

- `FrameLayout`
- `LinearLayout` horizontal / vertical
- `RelativeLayout` 核心规则
- `ScrollView` 单 child + 基础 vertical scroll（真实命中后做）
- `AbsoluteLayout` 可作为低成本旧应用兼容项

### Widget

- `View`
- `TextView`
- `Button`
- `EditText` 的显示/状态子集
- `ImageView`
- `ImageButton`
- `ProgressBar` 基础状态
- `VideoView`
- `SurfaceView` 只作为特殊 surface 占位/几何节点，不在本方案内重做 native surface

### 高频属性

```text
id
visibility
layout_width / layout_height
layout_margin*
padding*
gravity
layout_gravity
orientation
layout_weight
background
src
scaleType
text
textColor
textSize
singleLine / maxLines
enabled
clickable
alpha
```

### RelativeLayout 高频规则

```text
alignParentLeft / Right / Top / Bottom
centerInParent
centerHorizontal
centerVertical
above / below
toLeftOf / toRightOf
alignLeft / Right / Top / Bottom
```

## 5. 非目标

以下不做，且不得为了一个 UI WU 顺手引入：

| 非目标 | 原因 |
| --- | --- |
| 完整 `PhoneWindow/DecorView` | OGPlay 不需要 Android window service/tree |
| 完整 Java `LayoutInflater` 反射实现 | 只需 reproduces guest-observable inflation results |
| 完整 `ViewRootImpl` | 只取 measure/layout/draw 三阶段语义 |
| Android `Canvas` / Skia | 首版用 OGPlay RGBA renderer |
| HWUI / DisplayList / GLES20Canvas | 与现有 ANGLE 游戏渲染边界无关 |
| SurfaceFlinger / BufferQueue 模拟 | OGPlay 已拥有宿主 surface/present |
| 完整 Theme/TypedArray/defStyle | 按真实命中做有界解析 |
| Accessibility | 不在老游戏兼容 P0/P1 |
| IME 完整实现 | EditText 若命中，单独设计输入边界 |
| Animation/Transition 完整框架 | 需要时按具体 API 做能力 |
| Pixel-perfect Holo theme | Java 可观察行为与可操作性优先 |

## 6. 兼容优先级

遇到取舍时按以下顺序：

1. guest Java 可观察状态正确；
2. hierarchy 与 geometry 正确；
3. pointer dispatch 正确；
4. APK 自带 drawable/text 能正确显示；
5. 默认 Android 视觉近似；
6. Android 内部实现结构相似。

允许首版 Button 默认皮肤不与 Holo 完全一致；不允许 `GONE` 仍占空间、`getId()` 返回
错误、click listener 收到另一对象、skip 被视频盖住。

## 7. 成功标准

### P0 exact gate

真实 Asphalt 6 scenario 必须机器可判定地证明：

1. `R.layout.videoview` 成功 inflate；
2. `<merge>` children 进入 Activity content root；
3. `surface_view` 和 `skip` 可通过 `findViewById` 获得；
4. `skip.getId()` 与 resource id 一致；
5. onCreate 后 skip 是 `INVISIBLE` 且截图中不可见；
6. 视频推进到游戏允许显示 skip 的时间后，点击空白区域进入 `Activity.onTouchEvent`；
7. Java 把 skip 改为 `VISIBLE` 后，下一帧截图出现 APK 自带 skip drawable；
8. skip 位于页面底部、水平居中；五个 `GONE` sibling 不占空间；
9. 点击 skip 执行 guest listener；
10. 视频停止/Activity 切换沿现有生命周期继续，无 fault、clean shutdown。

### P1 通用 gate

至少建立一个不含任何 title 名称的 UI gallery fixture，覆盖 FrameLayout、horizontal/vertical
LinearLayout、ImageView/ImageButton、TextView/Button、visibility、margin/padding 和
RelativeLayout 核心规则；geometry、render hash 和 input dispatch 都有机器断言。
