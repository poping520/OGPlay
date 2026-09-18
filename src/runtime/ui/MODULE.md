# 子模块：runtime/ui

## 职责

持有有界 Android View/Layout 的宿主侧 hierarchy、state、resolved geometry、dirty、
render 与 hit-test 事实。本模块不执行 guest Java，不保存 DexVM object/listener，也不拥有
SDL、ANGLE 或视频解码。

## 公共 API

- `UiTree`：每 generation 建立唯一 synthetic `ContentRoot`；创建、按稳定顺序 attach、
  detach/destroy node，并以 android id 查找已接入 content root 的 node。
- `UiNode`：唯一保存 parent/children、class、android id（NO_ID=-1）、visibility、
  enabled/clickable、focusable/touch-mode、scroll-container、滚动条开关、orientation、
  gravity、image resource、layout params、padding、
  `clipChildren`/`clipToPadding`（默认 true）、measured/frame/screen frame、alpha 与
  dirty state。
- `SetVisibility`：VISIBLE/INVISIBLE 只标 draw dirty；任意 GONE 转换同时从 node 到 root
  标 layout/draw dirty。
- 焦点归 `UiTree` 单一 owner；请求需节点及祖先可见/启用并满足 focusable，转移、隐藏、
  禁用、detach、destroy/reset 会清除旧 owner。`isFocused` 只认自身，`hasFocus` 包含子孙。
- dirty 消费严格分相：`LayoutUiTree` 只清 `layout_dirty`；`UiOverlayRenderer` 只在 overlay
  成功重建后清 `draw_dirty`。layout traversal 不得吞掉尚未 rasterize 的 mutation。
- `Reset`：推进 generation，销毁全部旧 node/id index 并创建新的 content root；旧
  `UiNodeId` 永不重新变为有效。
- `LayoutUiTree`：以 surface `UiMetrics` 的 EXACTLY root constraint 执行有界
  MeasureSpec + FrameLayout traversal；fixed/match/wrap、padding/margin、child
  gravity 与 screen-frame propagation 共用一条路径。无 intrinsic 的普通叶子 `View`
  按 Android `getDefaultSize` 语义采用 bounded MeasureSpec 尺寸，不得折叠为 0x0。
- `LinearLayout`：horizontal/vertical 均按 document order 累加非 GONE child 主轴尺寸，
  parent gravity 定位整组，child layout_gravity 覆盖交叉轴；INVISIBLE 保留 geometry。
  指定主轴约束下，finite non-negative weight 按剩余像素确定性分配，padding/margin 同时
  参与可用空间和最终 frame；负值/NaN/Inf 明确失败。
- `RelativeLayout`：LayoutParams 保存 parent align/center 与 sibling above/below/left/right/
  align-edge 规则；横纵依赖图分别确定性解析且不依赖 document order，missing sibling、重复
  sibling id、同轴冲突与 cycle 明确失败。sibling 规则值为非正数（含
  `addRule(verb)` 写入的 TRUE=-1）时按 AOSP `rule > 0` 过滤语义视为无锚点。
- DVM-119：RelativeLayout gravity 在相对定位后按含 margin 的非 GONE 子节点整体
  边界平移，支持右/下/居中，默认 START/TOP 保留原定位。Java 参数经 integration
  映射为 UiTree 布局输入快照；UI 不保存 guest 引用或读取 Java 字段。
- `BuildUiRenderList` / `RasterizeUiOverlay`：从 resolved tree 生成 solid/bitmap/clip 命令，
  以整数 source-over 输出透明 RGBA8；`UiOverlayRenderer` 仅在 generation、metrics 或
  draw dirty 改变时重建。窗口先裁剪 content root；父容器 `clipChildren`（默认 true）把子
  View 裁到其 `screen_frame`；容器 `clipToPadding`（默认 true）再把子孙裁到 padding box。
  两个开关互不替代，关闭某一容器不解除祖先或输出边界裁剪。clip 改变只标 draw dirty。
- `TextView/Button`：UiNode 唯一保存 text、RGBA textColor、textSize、gravity 与行数边界；
  内置 5x7 ASCII 大小写字形同时提供确定性 measure/raster，两者共用按词换行结果；空文本
  控件仍保留一行字体高度。wrap_content 加入 padding，Button
  提供固定 background/padding/clickable 默认语义。compound drawables 以资源 id +
  resolved intrinsic 存于 UiNode（left/top/right/bottom）；measure 的内容宽为
  max(text.width, top.width, bottom.width)+left.width+right.width，高为
  max(text.height, left.height, right.height)+top.height+bottom.height，再加入 padding。
  空文本仍计入全部图标。左右图标在扣除上下图标后的带内居中，上下图标在扣除左右
  图标后的带内居中，使用 AOSP 整数除法；文本 gravity 在四边内缩后的区域生效。
- View 背景的资源/颜色、逐 Drawable alpha 与 UiNode 绘制失效共用一份投影；解码像素可按
  resource id 共享，Drawable 实例的 alpha/bounds 不共享。当前位图背景按目标 bounds 拉伸；
  编译 PNG 的 `npTc` 两轴 stretch div 与 padding 进入共享 UiBitmap；光栅化保持四周固定区，
  只缩放中心区。Drawable 的 alpha/bounds 仍是逐实例状态。
- `ImageView/ImageButton`：UiNode 保存 CENTER/CENTER_INSIDE/FIT_CENTER/FIT_XY/CENTER_CROP；
  render-list 在 node content box 内按 API19 对齐语义生成目标 rect，CPU raster 使用确定性
  nearest-neighbor scale。CENTER_CROP 的 dest 可超出 content box；默认父容器
  `clipChildren` 把该子 View 裁回自身 bounds。关闭父容器 clip 后，溢出仍受祖先与输出
  边界约束。触摸命中继续使用 `screen_frame`，不随绘制越界扩大。

## 不变量

- hierarchy、android id、visibility、layout params、geometry 与 clip 开关只有 UiTree
  一份权威事实。
- renderer 与 input 后续只能读取同一 `screen_frame`；不得各自推导 bounds。
- `ClearLayoutDirty` 与 `ClearDrawDirty` 只能由各自阶段消费；禁止恢复同时清除两类状态的
  模糊入口。
- 只有接入当前 content root 的 node 进入 id index；detach/destroy/reset 后立即不可查找。
- tree 最多 4096 node、每 parent 最多 1024 child、深度最多 128；超限或 cycle 明确失败。
- 本模块可依赖 core/loader 等下层事实，不得依赖 DexVM integration、session、frontend、
  SDL、ANGLE 或 video。

## 测试

`tests/runtime/ui_tree_tests.cpp` 锁定 hierarchy 顺序、id 更新、visibility dirty、detach、
destroy、generation reset、非法 mutation，以及 FrameLayout fullscreen/bottom/center、
padding/margin、wrap intrinsic、普通叶子 View bounded default size、document-order overlap
geometry，以及 horizontal/vertical
LinearLayout 的 GONE/INVISIBLE、weight、padding/margin geometry。
RelativeLayout tests 锁定 parent/sibling/center、反向 document order 与 missing/cycle failure。
`tests/runtime/ui_renderer_tests.cpp` 锁定透明、bitmap、alpha overlap、Z-order、clip、
visibility、layout 后 draw cache 刷新、固定字体 measure/text golden、Button content/background，以及五种
ImageView scale destination 与 CENTER_CROP exact pixel golden；另锁定 `clipChildren`/
`clipToPadding` 对越界子 View、padding 与嵌套容器的像素差，以及动态切换后的 cache 重建。

DVM-120：内置字体 BOLD 以行像素并集加粗、ITALIC 按行右移，四种样式共用测量与绘制
的 advance/边界；半透明加粗像素只混合一次。text_style 随 UiNode dirty 一起失效。
不依赖系统字体或 Skia，不承诺 Android 字体像素一致性。
