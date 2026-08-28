# 05 · 验证、记账与诊断

## 1. 验证层级

layout UI 必须同时有四层证据：

```text
loader fixture
    ↓
pure UiTree/layout unit tests
    ↓
DexVM Android integration tests
    ↓
real APK scenario
```

任何“按钮在真实游戏里可见可点”的结论都不能只由单元测试推出。

## 2. Loader / Resource 测试

覆盖：

- UTF-8/UTF-16 AXML string pool；
- start/end nesting；
- typed reference；
- dimension；
- enum/flags；
- malformed/out-of-range chunk；
- unknown namespace；
- layout id → file；
- drawable id → file；
- missing required resource。

generic parser 的 golden 应断言“保留原始 typed attr”，而不是断言某个 LinearLayout 结果。

## 3. UiTree 测试

必须锁定：

- create/attach/detach；
- parent/child order；
- id index；
- `setId()` 更新 index；
- visibility；
- dirty propagation；
- Activity generation reset；
- removed node 不再 find/hit/draw；
- no duplicate live binding。

## 4. Layout 测试

### FrameLayout

- match_parent；
- fixed/wrap child；
- bottom；
- center；
- margin/padding；
- overlapping children 的 Z-order。

### LinearLayout

- horizontal；
- vertical（P1）；
- center gravity；
- `INVISIBLE` 保留空间；
- `GONE` 移除空间；
- weight（P1）；
- wrap_content parent；
- drawable intrinsic child。

### RelativeLayout

- parent alignment；
- sibling relation；
- center；
- missing reference；
- dependency cycle deterministic failure。

geometry 用整数 rect 直接断言，不靠肉眼截图。

## 5. Render 测试

P0 golden：

- transparent empty overlay；
- solid color；
- PNG/ImageButton intrinsic draw；
- alpha；
- two overlapping bitmap order；
- clipping。

对 raster 结果使用：

- exact pixel/hash；
- 或固定 fixture 的 PNG hash。

不要使用宿主字体做 P0 golden；TextView WU 应先固定字体 backend 再引入 text golden。

## 6. Input 测试

必须覆盖：

- topmost clickable wins；
- `INVISIBLE` 不命中；
- `GONE` 不命中；
- disabled 不命中/不 click；
- DOWN outside → no target；
- DOWN inside + UP inside → click；
- DOWN inside + UP outside → no click；
- view 在 gesture 中被隐藏/移除 → no click；
- sibling without listener 不阻挡后面的 Activity fallthrough（按最终规则锁定）；
- listener 收到与 `findViewById` 相同的 View object。

## 7. DexVM 集成测试

沿用 `tests/dexvm/` 现有模式，至少扩展：

- XML inflate 后 `findViewById/getId`；
- `setVisibility` 驱动 layout/render/input；
- Java `OnClickListener`；
- `ViewGroup.addView/removeView`（P1）；
- `setImageResource` 与 XML `src` 同路径；
- Activity generation teardown。

旧 `widget_click_tests.cpp` 中为特定 bottom row 手造 `layout_views` 的测试，在新 layout engine
覆盖后应迁移成“构造 UiTree 或 inflate fixture → layout → hit-test”，避免继续锁定旧特判。

## 8. Asphalt 6 P0 scenario

P0 的最终 gate 必须是自动化 scenario，而不是手工窗口观察。

### 前置

- 使用普通（非 survey）运行；
- 使用 title 既有 profile/外部数据约束；
- 不新增 title-specific runtime branch；
- 证据走项目现有 scenario/playbook。

### 步骤

```text
1. 启动 MyVideoView Activity
2. 等待视频开始并确认无 fault
3. 断言 skip 当前 INVISIBLE / screenshot 不含 skip
4. 推进 guest 时间到游戏允许显示 skip 的区间
5. 点击视频区域中不属于任何 VISIBLE UI 的点
6. 断言 Activity.onTouchEvent 路径发生、skip 变为 VISIBLE
7. 捕获下一 presented frame
8. 断言 skip drawable 出现在预期 bottom-center bounds
9. 点击 skip 中心
10. 断言 guest OnClick listener 被执行
11. 断言视频停止/Activity 切换继续
12. 继续到既有 Asphalt 6 后续 checkpoint
13. clean shutdown
```

### 三轮

与项目 gate 纪律一致，兼容结论要求重复运行稳定。P0 UI gate 不要求整个游戏主菜单首次就在
该 WU 重新定义；但 skip 行为、无 fault、后续 checkpoint 必须稳定。

## 9. Asphalt XML fixture

可以建立一个**通用命名**的 test fixture，结构等价于真实 `videoview.xml`：

```text
merge
 ├─ VideoView full
 ├─ TextView bottom
 └─ LinearLayout bottom/center
      ├─ five GONE ImageButton
      └─ one visible ImageButton
```

fixture/test 代码不要写 title/厂商名。真实 Asphalt 只在 scenario/evidence 文档中出现。

## 10. 结构化诊断

建议事件：

```text
ui.inflate.begin
ui.inflate.node
ui.inflate.unsupported_tag
ui.resource.missing
ui.attr.unsupported
ui.layout.begin
ui.layout.node
ui.layout.cycle
ui.render.overlay
ui.input.hit
ui.input.capture
ui.input.click
ui.input.fallthrough
```

日志字段优先：

```text
node_id
android_id
class/tag
parent_id
rect
visibility
resource_id
reason
```

禁止每帧重复输出同一 unsupported attr；做 once/per-layout 去重。

## 11. Tree dump

开发/Agent 诊断可暴露有界 dump：

```text
ContentRoot [0,0 1280x720]
 ├─ VideoView#surface_view [0,0 1280x720] VISIBLE
 ├─ TextView#SrtText [...] VISIBLE
 └─ LinearLayout#buttonsLayout [0,650 1280x70] VISIBLE
      ├─ ImageButton#backward GONE
      ├─ ...
      └─ ImageButton#skip [604,660 72x50] VISIBLE
```

这是诊断接口，不是 capability 判断源。正式结论仍来自 tests/scenario。

## 12. Capability 记账

实施每个 WU 前先查看当时的 `capabilities.toml`，复用已有条目；不存在时再新增适当粒度的
layout UI capability。不要在设计阶段臆造当前 ledger 中不存在的 key。

建议记账维度至少能区分：

```text
AXML/resource
inflation/hierarchy
layout
render
input
widget families
```

状态只随机器证据推进。unsupported structural feature 必须可查询，不允许只写日志。

## 13. 安全边界

APK 资源是不可信输入。实现时给出显式上限：

- XML node count；
- tree depth；
- include depth；
- children count；
- resource recursion；
- drawable dimension/decoded bytes；
- text length；
- RelativeLayout graph size。

数值应结合现有 loader/image 限制选取并写测试；超限明确失败。

## 14. 性能

目标是“UI overlay 不成为游戏主渲染热点”。

要求：

- layout clean 时不重复 measure/layout；
- draw clean 时不重 raster overlay；
- drawable 解码缓存；
- render list 缓存；
- Text WU 后 glyph/text cache；
- 不因 UI 每帧重新 parse XML。

性能优化必须先有测量；P0 优先 correctness/determinism。

## 15. 回归门禁

每个 WU 至少：

```text
cmake --preset dev
cmake --build --preset dev --target <affected-target>
ctest --preset dev -R "<affected-test>"
```

Windows 使用项目对应 MSVC preset。默认只运行受影响能力的单点/定向测试，全量测试仅在
用户明确要求时运行。涉及真实 title 时再按 playbook 跑 scenario，不以手工操作代替机器出口。
