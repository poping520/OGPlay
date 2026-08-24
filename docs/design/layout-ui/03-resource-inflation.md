# 03 · 资源、AXML 与 inflation

## 1. 原则：parser 只解析格式，widget 层解释语义

当前 `ParseBinaryXmlElements()` 直接把少数属性写进 `BinaryXmlElement` 字段。随着 UI 面
扩大，这会让 loader 逐渐知道 `TextView.text`、`RelativeLayout.below`、`ImageView.scaleType`
等 framework 语义。

目标改为两层：

```text
compiled AXML
    ↓
loader: generic typed XML
    ↓
UI inflater/widget parsers
    ↓
UiNode/LayoutParams/DrawState
```

## 2. Generic typed AXML

建议输出：

```cpp
struct BinaryXmlAttribute {
    std::string namespace_uri;
    std::string name;

    ResourceValueType value_type;
    std::uint32_t data;
    std::optional<std::string> raw_string;
};

struct BinaryXmlElement {
    std::string name;
    std::int32_t parent_index;
    std::vector<BinaryXmlAttribute> attributes;
};
```

loader 必须继续：

- 严格 chunk range 检查；
- UTF-8 / UTF-16 string pool；
- start/end element nesting；
- Android namespace；
- typed value 原样保留；
- malformed input 明确失败。

loader 不做：

- `gravity` 位语义；
- RelativeLayout rules；
- TextView style；
- View visibility；
- resource fallback 策略。

为降低迁移风险，现有 manifest parser 不应被这个 WU 顺手重写；layout AXML 先独立演进。

## 3. UiResourceResolver

在现有 `ArscTable` 基础上建立 UI 需要的有界 resolver；复用同一 APK 文件读取和 image
decoder，禁止第二套资源表。

P0 需要：

```text
layout id   → res/layout/*.xml
drawable id → APK bitmap path / color
id          → stable resource id
dimension   → px
```

P1 扩展：

```text
string
color
simple style parent chain
selector / bitmap drawable
```

### 配置选择

第一版不做完整 Android qualifier solver。规则必须 deterministic：

1. 默认配置优先；
2. drawable density 可选择最接近当前 UiMetrics；
3. orientation 只有真实题目命中再引入；
4. locale 首版只 default。

如果 resource 存在多配置但当前 resolver 无法诚实选择，应记账并明确报告选择策略，不能
随机取第一项。

## 4. Dimension

支持：

```text
px
dp / dip
sp
match_parent / fill_parent
wrap_content
```

dp/sp 必须经 `UiMetrics` 转换。complex dimension 的 radix/unit 解析属于 loader/resource
层；layout engine 只看最终的 `DimensionSpec`/px。

## 5. Inflation registry

不要继续在 `android_app.cpp` 内维护 tag map。建立 descriptor registry：

```cpp
struct UiWidgetDescriptor {
    std::string_view xml_tag;
    std::string_view dex_descriptor;
    UiClass kind;

    ParseAttributesFn parse_attributes;
    MeasurePolicy measure_policy;
    DrawPolicy draw_policy;
};
```

P0 registry：

```text
View
TextView
ImageView
ImageButton
VideoView
LinearLayout
FrameLayout
RelativeLayout
```

为了兼容存量 Java API catalog，Button/EditText/ProgressBar/ScrollView 等可在 P1 WU 加入。

## 6. DexVM object creation

inflater 每遇到可支持 tag：

1. 由 integration 根据 descriptor 创建正确 intrinsic instance；
2. 在 UiTree 创建 node；
3. 建立双向 binding；
4. 解析基础 View attrs；
5. 解析 LayoutParams；
6. 解析 widget-specific attrs；
7. attach parent；
8. 若存在 `android:id`，更新 UiTree id index。

这能保证：

```java
(ImageButton) findViewById(R.id.skip)
```

得到的 object identity 与 XML 创建时相同。

## 7. `<merge>`

P0 必须正确实现：

- `<merge>` 自身不创建 guest View/UiNode；
- 必须有 existing attach parent；
- children 直接 attach parent；
- `<merge>` 自身的 width/height 不作为可观察 child node；
- malformed nested/use-without-parent 明确失败。

Asphalt 6 的 `videoview.xml` 正是这个形态，不能继续只把 `<merge>` “跳过后猜 parent”。

## 8. `<include>`

P1：

```text
<include layout="@layout/foo" .../>
```

处理：

1. resolve layout id；
2. 递归 parse/inflate；
3. attach 到当前 parent；
4. 对 include 节点支持高频 override：
   - id
   - visibility
   - layout_width / height
   - layout params
5. include depth 有上限；
6. cycle 明确失败。

## 9. Unknown tag

### 平台标准 tag 未实现

记录 capability gap；如果该 node 对 hierarchy/geometry 是结构性必需，默认不能悄悄
把 children re-parent 后继续声称布局正确。strict/scenario 模式应 fail。

### 自定义 View

P1/P2 策略：

- 若 DexVM 可解析对应 game class，可创建 guest object；
- 建 generic UiNode，支持基础 id/visibility/layout/input；
- 自定义 `onDraw(Canvas)` 不在首版实现；
- 若 title 依赖 custom draw，记账明确暴露。

这允许“自定义 View 只是逻辑/触摸容器”的题目先工作，而不承诺 Canvas。

## 10. Attribute 解析层次

推荐顺序：

```text
base View attributes
    ↓
ViewGroup/LayoutParams attributes
    ↓
specific layout attributes
    ↓
widget attributes
```

P0 base attrs：

```text
id
visibility
layout_width
layout_height
layout_gravity
gravity
padding / paddingLeft/Top/Right/Bottom
src
```

实际只实现当前 WU 声明的字段；未知但非结构属性可以 warning+ignore，未知结构属性必须
进入 capability/gap。

## 11. Style / Theme

### P0

只保证 element 上显式 `android:*` 属性。

Asphalt 字幕里存在 `?android:attr/textAppearanceSmall`，P0 可以不解析该 theme attr，只要
它不阻塞 skip；但必须允许同 element 上显式 `textColor` 等属性继续解析。

### P1

增加简单：

```text
style="@style/X"
parent style chain
```

merge 顺序锁定：

```text
style parent → style child → element explicit attrs
```

### 非目标

首轮不实现：

```text
完整 Theme.resolveAttribute
defStyleAttr / defStyleRes
完整 TypedArray
```

## 12. Drawable

P0：

- APK bitmap（沿用现有 decoder）；
- solid color；
- intrinsic size；
- transparent default background。

P1：

- BitmapDrawable；
- ColorDrawable；
- 简单 selector/StateListDrawable；
- 9-patch 只在真实 title 命中后设计。

XML `src` 与 Java `ImageView.setImageResource()` 必须走同一 resolver/state 入口，避免
“XML 能显示、运行时换图不显示”的双路径。

## 13. 失败与记账

必须区分：

- malformed AXML/resource：明确失败；
- required layout/drawable 找不到：明确失败；
- unsupported structural attribute：记账 + strict fail；
- presentation-only attribute 暂未支持：warning once + capability note；
- theme visual attr 未解析：可降级，但不能影响 View identity/hierarchy/input。

所有日志使用结构化 logger；同一 unsupported attribute 不应每帧刷屏。
