# 06 · Work Unit 实施计划

## 1. WU 使用规则

本章的 `LUI-N` 是**设计级稳定 WU 标识**，便于后续对话和依赖引用。真正启动实施时：

1. 先确定当前里程碑；
2. 在该里程碑的 `docs/tasks/<milestone>/` 创建正式 Work Unit 任务单；
3. 正式文件名沿用项目当前 `WU-M<里程碑>-NNN.md` 规则；
4. 任务标题/正文保留 `LUI-N` 作为设计追踪号；
5. 一个 WU 必须有一句话目标、显式依赖和机器可判定出口；
6. WU 完成时同步相关 `MODULE.md`、`CURRENT.md` 与 `capabilities.toml`。

本设计不提前创建任务目录或占用当前里程碑编号。

## 2. 总览

| WU | 一句话目标 | 依赖 | 机器出口 |
| --- | --- | --- | --- |
| LUI-1 | 把 layout AXML 改成 generic typed attribute 输出且不回归现有解析 | 无 | loader tests 全绿，旧 layout fixture 等价 |
| LUI-2 | 建立 `runtime/ui` 的 UiTree/UiNode 唯一 hierarchy/state 事实 | LUI-1 可并行部分 | pure UiTree tests + MODULE/index |
| LUI-3 | 将 DexVM View identity/id/visibility 绑定到 UiTree | LUI-2 | `findViewById/getId/setId/visibility` integration tests |
| LUI-4 | 用 registry inflater + synthetic content root 正确处理普通 root 与 `<merge>` | LUI-1..3 | XML fixture inflate tree exact |
| LUI-5 | 实现 MeasureSpec + FrameLayout，使 content/external child 得到正规 geometry | LUI-2,4 | FrameLayout geometry suite |
| LUI-6 | 实现 horizontal LinearLayout、GONE/INVISIBLE 与 drawable intrinsic measure | LUI-5 | video-controls 等价 fixture geometry exact |
| LUI-7 | 生成透明 RGBA UI overlay 并绘制 color/bitmap/ImageButton | LUI-5,6 | deterministic render golden |
| LUI-8 | 在 session present 边界合成 VideoView/GLES 基底与 UI overlay | LUI-7 | composition golden + video regression |
| LUI-9 | 用 UiTree resolved geometry 替换旧 bounds 特判并闭合 touch/click/fallthrough | LUI-3,5,6 | input suite + guest listener test |
| LUI-10 | 以真实 Asphalt 6 scenario 闭合“显示 skip → 点击 skip → 继续游戏” | LUI-4..9 | 三轮 scenario evidence |
| LUI-11 | 扩展 vertical LinearLayout、margin/padding/weight 与动态 ViewGroup | LUI-10 | generic gallery layout/input tests |
| LUI-12 | 实现 TextView/Button 状态、measure 与固定 backend 文本 raster | LUI-10 | text golden + Java state round-trip |
| LUI-13 | 实现 RelativeLayout 核心规则与 cycle 诊断 | LUI-11 | dependency/geometry suite |
| LUI-14 | 增加 `<include>`、ImageView scaleType 与首批通用资源/selector 能力 | LUI-11..13 | generic gallery + resource tests |
| LUI-15 | 以多 title gap 数据收敛 P1 capability 并删除遗留 layout 特判 | LUI-10..14 | CTest + selected title scenarios，无旧 bounds 事实源 |

P0 是 LUI-1..10。LUI-11..15 是通用 UI v1 的渐进扩展。

---

## 3. LUI-1 · Generic compiled AXML

**目标一句话**：让 loader 输出通用 typed element/attribute，而不是把 layout framework
属性硬编码进 parser。

### 工作

- 保留现有严格 chunk/string-pool/nesting 校验；
- typed value 原样输出；
- Android namespace 信息保留；
- 给现有少量 layout attr 写 adapter 或一次性迁移；
- 不动 manifest parser 的已验收行为。

### 出口

- UTF-8/UTF-16、reference、dimension、enum/flags fixture；
- malformed fixture；
- 现有 layout parser tests 无回归；
- 新 test 能看到 `visibility/orientation/text` 等尚未实现语义的 raw typed attrs。

---

## 4. LUI-2 · UiTree 地基

**目标一句话**：建立唯一的 hierarchy、id、visibility、layout params、geometry 与 dirty
状态容器。

### 工作

- 新 `runtime/ui` module + `MODULE.md`；
- UiNodeId / UiNode / UiTree；
- attach/detach/order；
- id index；
- visibility/enabled/clickable；
- layout/draw dirty；
- generation reset；
- 更新 modules index。

### 出口

pure unit tests 证明：

- hierarchy 顺序稳定；
- id 查找与更新；
- `GONE/INVISIBLE/VISIBLE` round-trip；
- detach 后不再 find；
- reset 后旧 node 不可访问。

---

## 5. LUI-3 · DexVM View binding

**目标一句话**：让每个 guest View intrinsic 与一个 UiNode 一一绑定，并把现有 id/visibility
API 改读 UiTree。

### 工作

- `VmObjectRef ↔ UiNodeId` binding；
- `Activity.findViewById`；
- `View.getId/setId`；
- `setVisibility/getVisibility`；
- listener map 以 UiNodeId 为 key；
- 旧 `view_registry/widget_states` 变成 adapter 或删除对应权威写路径。

### 出口

DexVM fixture：

```text
inflate/create view
→ set id
→ find
→ getId exact
→ set visibility
→ UiTree state exact
```

listener identity 不变。

---

## 6. LUI-4 · Inflater 与 `<merge>`

**目标一句话**：把 `Activity.setContentView(int)` 从内联 tag 特判变成 registry inflater，并
用 synthetic content root 正确承载 `<merge>`。

### 工作

- UiWidgetDescriptor registry；
- Activity UiContentRoot generation；
- tag → dex descriptor；
- base attrs/LayoutParams 解析；
- `<merge>`；
- unknown structural tag failure/gap；
- Activity switch teardown。

### 出口

通用 fixture：

```text
merge
 ├─ VideoView
 ├─ TextView
 └─ LinearLayout
      └─ ImageButton
```

tree、parent order、id、guest class identity exact。

---

## 7. LUI-5 · MeasureSpec + FrameLayout

**目标一句话**：用正式 measure/layout 取代 fullscreen root 特判。

### 工作

- UiMetrics；
- fixed/match/wrap DimensionSpec；
- MeasureSpec；
- root surface constraints；
- FrameLayout measure/layout；
- gravity/layout_gravity；
- screen rect propagation。

### 出口

geometry tests 覆盖：

- fullscreen child；
- bottom child；
- centered child；
- margin/padding；
- overlapping Z-order。

---

## 8. LUI-6 · Horizontal LinearLayout + visibility

**目标一句话**：让底部按钮行按通用 LinearLayout 规则得到正确尺寸与位置。

### 工作

- horizontal main/cross axis；
- gravity；
- `GONE` 不参与；
- `INVISIBLE` 参与但不可绘制/命中；
- bitmap drawable intrinsic measure；
- ImageButton wrap_content；
- 删除/停用 `support_widget_dispatch` 中相应 edge-row bounds 特判。

### 出口

等价 controls fixture：

- 5 个 GONE sibling；
- 1 个 visible ImageButton；
- row bottom；
- visible child exact center；
- 改成 INVISIBLE 后 geometry 不变；
- 改成 GONE 后 layout 重算。

---

## 9. LUI-7 · Bitmap UI renderer

**目标一句话**：把 UiTree resolved state rasterize 成 deterministic 透明 RGBA overlay。

### 工作

- UiRenderList；
- DrawSolidRect；
- DrawBitmap；
- alpha；
- clip 基础；
- resource bitmap cache；
- draw dirty cache。

### 出口

exact pixel/hash tests：

- transparent；
- bitmap；
- alpha overlap；
- Z-order；
- invisible/gone 不绘制。

---

## 10. LUI-8 · Present composition

**目标一句话**：在 session 已拥有的 present 边界把 VideoView/GLES 与 UI overlay 按正确
层级合成。

### 工作

- 明确 compositor owner；
- VideoView resolved rect 输入；
- UI overlay source-over；
- 不把 UI 逻辑下沉到 video module；
- screenshot/presented frame 读取合成后的最终像素。

### 出口

- fake video fullscreen + bitmap button overlay golden；
- UI 不可见时 video hash 与基线一致；
- overlay 可见时只改变 expected region；
- video playback/seek/completion tests 无回归。

---

## 11. LUI-9 · Generic UI pointer dispatch

**目标一句话**：用 resolved UiTree hit-test 取代旧 bounds 特判，并让 UI 未消费时准确回退
Activity touch。

### 工作

- reverse draw-order hit-test；
- visibility/enabled/clickable；
- pointer capture；
- OnTouchListener 最小语义；
- click on UP-inside；
- hidden/removed target cancel；
- no-target fallthrough；
- Invoke guest OnClick using binding。

### 出口

- topmost；
- invisible/gone；
- down/up；
- remove/hide during gesture；
- fallthrough；
- guest Java listener exact identity。

旧 `FindClickableViewAt` 的 special geometry 不再是生产 bounds 来源。

---

## 12. LUI-10 · Asphalt 6 P0 exact gate

**目标一句话**：用真实 APK 证明 XML skip 控件能由 Java 状态驱动显示并可点击进入后续游戏。

### 工作

只补 scenario/evidence/必要的通用缺口；发现新结构能力时不要在本 WU 无限扩面，若超出 P0
边界则新建后续 WU。

### 出口

三轮稳定：

```text
onCreate: skip INVISIBLE
→ 视频 > 3s
→ 点击空白
→ Activity.onTouchEvent
→ skip VISIBLE
→ screenshot 看到 bottom-center skip
→ 点击 skip
→ guest listener
→ video/activity 后续 checkpoint
→ no fault + clean shutdown
```

---

## 13. LUI-11 · 通用 LinearLayout + 动态 hierarchy

**目标一句话**：补齐多数旧游戏需要的 vertical/margin/padding/weight，并让 Java 动态
add/remove 共享 UiTree。

### 工作

- vertical；
- margins/paddings；
- weight；
- `ViewGroup.addView/removeView/updateViewLayout`；
- `setContentView(View)`；
- geometry getter。

### 出口

通用 UI gallery 用 XML + dynamic Java 两种方式构造等价 tree，geometry/input 一致。

---

## 14. LUI-12 · TextView / Button

**目标一句话**：建立可测量、可绘制且 Java state round-trip 的基础文本控件。

### 工作

- text/textColor/textSize/gravity；
- fixed font/text raster backend；
- wrap_content measure；
- Button 基础 background/content；
- `setText/getText` 共用 UiNode text state。

### 出口

- string mutation 改变 measure/draw；
- fixed golden；
- multi-line/unsupported 语义按 capability 明确边界；
- 不依赖宿主 UI toolkit。

---

## 15. LUI-13 · RelativeLayout 核心规则

**目标一句话**：覆盖旧游戏常见 sibling/parent 相对定位，同时对 cycle 明确失败。

### 工作

- sibling id reference；
- horizontal/vertical dependency graph；
- parent align；
- center；
- above/below；
- left/right；
- align edges；
- cycle/missing reference diagnostics。

### 出口

每个规则有 geometry test；组合规则 deterministic；cycle strict fail。

---

## 16. LUI-14 · include / scale / resources

**目标一句话**：补足常见 XML 复用和图片显示资源，使通用 UI gallery 不依赖直接 inline layout。

### 工作

- `<include>`；
- recursion/cycle limit；
- ImageView scaleType 首批；
- color/string/dimen resolver；
- simple style/selector 只按命中实现。

### 出口

resource/layout fixture + render golden；include 与 inline 等价 geometry。

---

## 17. LUI-15 · 多 title 收敛与遗留清理

**目标一句话**：基于真实 title gap 数据决定 P1 最小集，并清除旧 layout 特判和并行事实源。

### 工作

- 选择若干有 Java/XML overlay 的旧 title；
- gap survey/日志统计；
- 只为真实命中补 capability；
- 清理 `layout_views`/edge-row bounds 等遗留路径；
- 文档/MODULE/capabilities 收敛。

### 出口

- full CTest；
- selected scenarios；
- production hit-test 只读 UiTree geometry；
- 不存在 title-specific UI branch；
- `CURRENT.md` 与 capability 状态准确。

## 18. AI 执行纪律

每个正式 WU 开始前：

```text
AGENTS.md
→ docs/state/CURRENT.md
→ 正式 WU task
→ docs/design/layout-ui/README.md
→ 当前章节
→ 相关 MODULE.md
→ capabilities.toml
→ 必要的 AOSP 参考文件
```

实现某个 layout/widget 行为前，按 [07](07-aosp-reference.md) 找对应 AOSP 文件，记录“参考
的是哪条可观察语义”。不要复制 Android 的系统结构；不确定时优先写 fixture 把语义钉住。
