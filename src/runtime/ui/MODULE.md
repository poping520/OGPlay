# 子模块：runtime/ui

## 职责

持有有界 Android View/Layout 的宿主侧 hierarchy、state、resolved geometry、dirty、
render 与 hit-test 事实。本模块不执行 guest Java，不保存 DexVM object/listener，也不拥有
SDL、ANGLE 或视频解码。

## 公共 API

- `UiTree`：每 generation 建立唯一 synthetic `ContentRoot`；创建、按稳定顺序 attach、
  detach/destroy node，并以 android id 查找已接入 content root 的 node。
- `UiNode`：唯一保存 parent/children、class、android id（NO_ID=-1）、visibility、
  enabled/clickable、orientation、gravity、image resource、layout params、padding、
  measured/frame/screen frame、alpha 与 dirty state。
- `SetVisibility`：VISIBLE/INVISIBLE 只标 draw dirty；任意 GONE 转换同时从 node 到 root
  标 layout/draw dirty。
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
  sibling id、同轴冲突与 cycle 明确失败。
- `BuildUiRenderList` / `RasterizeUiOverlay`：从 resolved tree 生成 solid/bitmap/clip 命令，
  以整数 source-over 输出透明 RGBA8；`UiOverlayRenderer` 仅在 generation、metrics 或
  draw dirty 改变时重建。
- `TextView/Button`：UiNode 唯一保存 text、RGBA textColor、textSize、gravity 与单行边界；
  内置 5x7 ASCII 字形同时提供确定性 measure/raster，wrap_content 加入 padding，Button
  提供固定 background/padding/clickable 默认语义。
- `ImageView/ImageButton`：UiNode 保存 CENTER/CENTER_INSIDE/FIT_CENTER/FIT_XY/CENTER_CROP；
  render-list 在 node content box 内按 API19 对齐语义生成目标 rect，CPU raster 使用确定性
  nearest-neighbor scale，CENTER_CROP 仍由 node clip 裁切。

## 不变量

- hierarchy、android id、visibility、layout params 和 geometry 只有 UiTree 一份权威事实。
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
ImageView scale destination 与 CENTER_CROP exact pixel golden。
