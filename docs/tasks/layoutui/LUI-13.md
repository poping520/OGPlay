# LUI-13 · RelativeLayout 核心规则

## 目标（一句话）

让旧游戏常用的 sibling/parent 相对定位通过确定性依赖图解析并对非法图明确失败。

## 依赖

- LUI-10。

## 验收与结果

- typed RelativeRules 覆盖 alignParent 四边、center 三类、above/below、toLeftOf/toRightOf
  与 align 四边，并随 LayoutParams 唯一复制到 UiNode。
- 横向和纵向依赖图分别按 sibling android id 解析；anchor 可晚于 dependent 出现在 document
  order，最终 geometry 不依赖容器/hash iteration order。
- XML `layout_*` structural attrs 与 Java `RelativeLayout.LayoutParams.addRule` 共用同一规则；
  attached params mutation 会推进 layout dirty 并改变 geometry。
- missing sibling、重复 sibling id、同轴冲突和 dependency cycle 明确失败；Java baseline、RTL
  verb、非法 anchor 也明确抛异常。
- parent/sibling/center exact geometry、反向 document order、XML/Java round-trip 以及
  missing/cycle tests 通过；full CTest 757/757 通过。
