# 子模块：runtime/ui

## 职责

管理有界 View/Layout 的宿主布局分区和外部窗口投影分区，提供 resolved geometry、dirty、
render 与 hit-test 快照。本模块不执行 guest Java，不保存 DexVM object/listener，也不拥有
SDL、ANGLE 或视频解码。

`UiFallbackFont` 只解析 SHA-256 固定的 API19 system font，由上层 bytes 注入每 tree；
BMP glyph 按字形/像素高度缓存，最多 128 项，ASCII 保持原有固定字体路径。缺字、
shaping/bidi 与补充平面明确失败，不读取宿主字体或任意应用字体。

## 公共 API

- `ui_leaf_measure.h` 的 `MeasureTextLeaf` / `MeasureImageLeaf` 只收 const 值输入与两轴
  MeasureSpec；不依赖 tree/node/VM/children，不写 geometry。Size/Insets/MeasureSpec 与固定
  字体测量声明归此独立头。输出 desired/constrained 与每轴 AT_MOST TOO_SMALL 位；这是
  有界服务状态。java_text_view 模式按 AOSP TextView 先选宽再断行，直接裁剪尺寸且不置 TOO_SMALL；
  返回固定字形首行 baseline，未实现完整 Android 字体排版。HostLayout 文本 baseline 仍缺席。
  intrinsic+padding/minimum 可独立供 image/video 使用；不提供 adjustViewBounds/比例算法。
- 文本输入包含固定字号/四样式、固定 fallback、行数策略及四槽 compound 尺寸。默认按
  spec 扣 padding 与左右 compound 生成 immutable `TextLinePlan`，记录实际行文本、字体及
  metrics；空串 desired 仍留 M cell，plan 不绘制占位字。DrawText 携带 plan 时只消费其
  文本/字体/metrics，不重新换行。plan 的生命周期独立于输入和树。
  HostLayout 显式选择 `LegacyPadding` 保留宿主文本测量公式，由宿主遍历写 node.measured；
  renderer 继续按最终 text band 换行，该宿主路径不承诺统一 line-plan。
  JavaTraversal 节点携带与完整 projection epoch 一起冻结的 text_line_plan，presenter 不再测量/断行；
  Java 模式空串宽度为0，maxLines 截断可见行，singleLine 转换由 integration 提供。

- `UiTree`：每 generation 建立唯一 synthetic `ContentRoot`；创建、按稳定顺序 attach、
  detach/destroy node，并以 android id 查找已接入 content root 的 node。
- `UiNode`：在宿主分区拥有、在外部窗口分区投影 parent/children、class、android id（NO_ID=-1）、visibility、
  enabled/clickable、focusable/touch-mode、scroll-container、滚动条开关、orientation、
  gravity、image resource、layout params、
  `clipChildren`/`clipToPadding`（默认 true）、measured/frame/screen frame、alpha 与
  dirty state。
- guest View 的 `padding` / `minimum` 是 integration 从 Java 权威导入的布局、渲染输入，
  本层不维护另一份 setter 状态，不读取 Java 或执行回调。纯宿主节点（如 Dialog 平面）
  继续直接拥有这两个输入；guest measured/frame 只来自完整 Java 投影。
- `window_visible` 及 `SetWindowVisible` 保存独立窗口可见性；隐藏窗口保留 hierarchy，
  不改变 guest View.visibility。有效可见性包含祖先窗口状态，绘制、输入、焦点共同使用。
- direct content-root child 的 `modal_layer` 标记合成平面；render-list 可选 all/normal/modal，
  默认 all 保持既有行为。选择只过滤相应根子树，沿用唯一 geometry/clip/raster；不读取
  guest Dialog 或改变可见性来模拟层序。分层 cache 的调用方在消费 draw dirty 前保留
  变更证据，以 force_rebuild 同步另一层；平面改变本身也使 cache 失效。
- `SetVisibility`：VISIBLE/INVISIBLE 只标 draw dirty；任意 GONE 转换同时从 node 到 root
  标 layout/draw dirty。
- HostLayout 焦点归 `UiTree` 单一 owner；请求需节点及祖先可见/启用并满足 focusable，转移、隐藏、
  禁用、detach、destroy/reset 会清除旧 owner。`isFocused` 只认自身，`hasFocus` 包含子孙。
- dirty 消费严格分相：`LayoutUiTree` 只清 `layout_dirty`；`UiOverlayRenderer` 只在 overlay
  成功重建后清 `draw_dirty`。layout traversal 不得吞掉尚未 rasterize 的 mutation。
- Detach 的默认策略请求布局；preserve_layout 策略只标祖先 draw dirty，保留已有
  layout dirty 和剩余节点的 resolved geometry。两者都更新 parent/children、id index
  和焦点，不销毁节点；不通过事后清 dirty 模拟布局期间移除。
- `BringChildToFront(parent, child)`：仅直接子节点按 API19 数组移动到末尾，保持 attach、
  焦点及 Surface 生命周期，已接入时重建 id index、只标祖先 draw dirty；非子节点返回 false。
  requestLayout/invalidate 由调用方派发。
- `Reset`：推进 generation，销毁全部旧 node/id index 并创建新的 content root；旧
  `UiNodeId` 永不重新变为有效。
- `UiTree` 的 geometry owner 在构造时固定：HostLayout 与 ExternalProjection 分区不能切换。
  每个外部分区代表一个窗口 root；纯宿主 UI 使用独立布局根。生产 guest 唯一使用 JavaTraversal；
  View/ViewGroup、LinearLayout/RelativeLayout 与真实 Window FrameLayout 根投影提交整棵 Java 树的局部 frame、hierarchy 与 ID，
  本层不补算 guest bounds；render/hit-test 消费同一完整窗口 epoch。
- `SetProjectionTarget` 由上层提供 root、window generation、content/metrics revision 及 viewport；
  同代 revision 不回退，尺寸改变必须推进 metrics revision。`CommitProjection` 只接受完全匹配
  当前 target、严格递增非零 epoch 的完整窗口几何批次或独立窗口事实批次。节点必须存在、唯一、全树连通且满足
  node/child/depth 上限；非法 geometry、visibility、坐标溢出、旧代/旧 revision/旧 epoch 明确失败。
  先在副本验证 hierarchy/id index，并按 screen(child)=screen(parent)+local(child)−parent.scroll_x/y
  同时解析双轴 screen_frame/screen_clip；每层用64位累加并拒绝32位坐标溢出，成功才整体替换；
  失败批次零修改，遗漏节点退出层级与 id index。批次可显式携带 android_id（包括 NO_ID），
  与 parent/children 一起验证并原子替换；未携带则沿用构建属性。enabled/clickable/focusable/
  focusable_in_touch_mode 可随同批次显式提交，guest collector 必须提交其全部 Java 值。
  ExternalProjection 不保存独立焦点 owner，RequestFocus 明确拒绝；焦点语义由上层 Java 持有。
  measured 与 frame 独立保存，不反向写语义 owner。
- `RetireProjectedIdentity` 用于外部分区的弱绑定 GC sweep 或上层确认的窗口退役，拒绝合成根及宿主分区；
  删除构建缓存身份/相关边并清构建索引，最后已发布快照不变，下一完整批次重建索引。
- `PublishedProjection` 仅发布完整成功的不可变快照；target 失效或待发布状态不覆盖上一完整 epoch。
  外部分区 render 与 `HitTestProjection` 只接受同一快照，读取已解析 screen_frame/screen_clip；
  presenter 不访问构建中的树；快照指针原子发布/获取。上层串行执行 target/commit，
  本 API 不以 VM 锁代替 owner 线程，也不提供可并发修改的 UiTree。
- `LayoutUiTree` 在修改前拒绝外部分区；外部层级/visibility 只能经整批提交更新。
  `Get` 的可变访问仅供 owner 构建宿主绘制属性；consumer 只能持有 const 完整快照。
- `LayoutUiTree`：以 surface `UiMetrics` 的 EXACTLY root constraint 执行有界
  MeasureSpec + FrameLayout traversal；fixed/match/wrap、padding/margin、child
  gravity 与 screen-frame propagation 共用一条路径。无 intrinsic 的普通叶子 `View`
  按 Android `getDefaultSize` 语义采用 bounded MeasureSpec 尺寸，不得折叠为 0x0。
- `LinearLayout`：horizontal/vertical 均按 document order 累加非 GONE child 主轴尺寸，
  parent gravity 定位整组，child layout_gravity 覆盖交叉轴；INVISIBLE 保留 geometry。
  指定主轴约束下，finite non-negative weight 按剩余像素确定性分配，padding/margin 同时
  参与可用空间和最终 frame；负值/NaN/Inf 明确失败。
- HostLayout 的 `RelativeLayout`：LayoutParams 保存 parent align/center 与 sibling above/below/left/right/
  align-edge 规则；横纵依赖图分别确定性解析且不依赖 document order，missing sibling、重复
  sibling id 与 cycle 明确失败。同轴边缘规则按 API19 覆盖顺序解析，双边缘确定尺寸，
  居中仅在两边均未定位时生效。sibling 规则值为非正数（含
  `addRule(verb)` 写入的 TRUE=-1）时按 AOSP `rule > 0` 过滤语义视为无锚点。
- DVM-119：RelativeLayout gravity 在相对定位后按含 margin 的非 GONE 子节点整体
  边界平移，支持右/下/居中，默认 START/TOP 保留原定位。Java 参数经 integration
  映射为 UiTree 布局输入快照；UI 不保存 guest 引用或读取 Java 字段。
- ExternalProjection 的 RelativeLayout 规则、margin/方向、缺失或 GONE 锚点与依赖环均由上层 ROM 处理；
  本层只接收成功遍历的完整 frame，不能套用上述 HostLayout 拒绝语义或另算规则。
- `BuildUiRenderList` / `RasterizeUiOverlay`：从 resolved tree 生成 solid/bitmap/clip 命令，
  以整数 source-over 输出透明 RGBA8；`UiOverlayRenderer` 仅在 generation、metrics 或
  draw dirty 改变时重建。窗口先裁剪 content root；父容器 `clipChildren`（默认 true）把子
  View 裁到其 `screen_frame`；容器 `clipToPadding`（默认 true）再把子孙裁到 padding box。
  两个开关互不替代，关闭某一容器不解除祖先或输出边界裁剪。clip 改变只标 draw dirty。
  输出 `fully_transparent` 只在所有实际绘制后的 alpha 均为零时为 true，并随缓存重建；
  其他 producer 创建的帧默认 false，消费者不得从命令数量猜测透明性。
- `TextView/Button`：UiNode 唯一保存 text、RGBA textColor、textSize、gravity 与行数边界；
  内置 5x7 完整可打印 ASCII（0x20–0x7E）同时提供确定性 measure/raster，共用固定字体算法；空文本
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
  图片可来自资源缓存或 integration 提供的不可变 RGBA 投影，颜色使用 RGBA 填充；
  image alpha 与 View alpha 相乘。UI 不持有 guest Drawable/Bitmap，不读取 guest 像素。
  render-list 在 node content box 内按 API19 对齐语义生成目标 rect，CPU raster 使用确定性
  nearest-neighbor scale。CENTER_CROP 的 dest 可超出 content box；默认父容器
  `clipChildren` 把该子 View 裁回自身 bounds。关闭父容器 clip 后，溢出仍受祖先与输出
  边界约束。触摸命中继续使用 `screen_frame`，不随绘制越界扩大。

## 不变量

- 纯宿主分区的 hierarchy、visibility、layout params、geometry 与 clip 由 UiTree 拥有。
  JavaTraversal 的 guest hierarchy/geometry 由 Java 唯一拥有，integration 收集并受检发布，
  UiTree 只保存宿主投影，不回填 Java measured/bounds。实验测试根提交 ROM layout 后的局部 frame，
  仅完整成功的 epoch 可供 render/hit-test 同时消费；宿主遍历仅服务独立 HostLayout。
  本层不执行 guest、不持 Java 引用、不读 Java 字段；未迁移文本/样式仍沿既有宿主单一存储。
- renderer 与 input 只能读取同一 `screen_frame`；不得各自推导 bounds。
  integration 的共同 presentation 只持有完整 guest 投影引用和已布局宿主树的 const 冻结副本；
  本层不拥有该发布序列、不新增 guest 几何权威。宿主树复制仅冻结宿主分区，不对 guest 分区重排。
- integration/session 仅为独立宿主 Dialog 调用 HostLayout；本层仍只接收 UiTree/UiMetrics，
  不获取解释器、guest 回调或 Java 引用。
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

SurfaceView 三种 surface_layer 为 UiNode 唯一配置事实；上层发布 surface_has_frame，
仅有真实输出的普通媒体/媒体叠层在 View 绘制顺序中输出 ClearSurfaceRect。
开孔沿用相同 clip，后绘制的控件保留；窗口上层不对 UI 开孔。光栅后重新认证最终
透明性，清除已绘制像素也能正确失效缓存。视频仍由上层按同一 geometry 合成，
UiTree 不持有播放器或 guest 引用，不依赖解码器。

实验 ImageView 光栅以同一投影 epoch 的 Drawable intrinsic（含 density）计算五种缩放目标，
不可变像素 backing 保留原尺寸用于采样；纯色 intrinsic=-1 仍填充内容区。HostLayout
继续用原像素尺寸，renderer 不重新测量或改写投影几何。

JavaTraversal managed 窗口批次显式携带 `window_visible`：CommitProjection 将该事实赋给
synthetic root，并清除本批子节点的历史窗口位；Java visibility 仍逐节点提交。窗口隐藏
与恢复同 hierarchy/geometry 原子发布，IsVisible 不绕过任何祖先。未提供该字段的通用
投影调用保留原契约；managed collector 必须提供，HostLayout SetWindowVisible 保持。

`CommitProjection` 的空节点窗口事实批次仅为 ExternalProjection 重发最后完整几何与新窗口事实，推进
epoch、原子替换快照，保留构建属性、target 与待布局工作；不测量或收集未完成 Java 几何。

外部 UiProjectedNode.alpha 可携上层已计算的祖先有效 alpha，CommitProjection 同 epoch
冻结到 UiNode；本层只按绘制命令消费，不执行 Java 或重新测量。RasterizeUiOverlay 输出
预乘 RGBA；调用方合成时须选择预乘 source-over，不能再次乘像素 alpha。当前按每个
绘制相乘，没有离屏子树层，存在重叠子树的组不透明度差异。

TextView 阴影由 UiNode 的 TextShadow 唯一保存，DrawText 携带同 epoch 值；同一 styled/fallback/
line-plan 字形生成掩码，先画偏移阴影再画正文，不参与测量，最终沿现有 clip 裁剪。
radius <= 0 不绘制；实际 radius 钳制到 32 px，采用 sigma=0.57735*radius+0.5 的归一化
可分离高斯核（截断 3 sigma），分数偏移双线性采样。有限原始参数保留供 getter；非有限值
或偏移绝对值超过 4096 px 明确失败。此服务不承诺 Skia 像素一致性。
