# LUI-12 · TextView / Button

## 目标（一句话）

建立可确定性测量、绘制并与 Java 双向 round-trip 的基础单行文本控件。

## 依赖

- LUI-10。

## 验收与结果

- TextView/Button 的 text、RGBA textColor、textSize、gravity 和单行上限由 UiNode 唯一持有；
  `setText/getText` 与 Editable clear/replace 读写同一状态并推进 dirty。
- 内置固定 5x7 ASCII 字体同时用于 measure 和 raster，不依赖宿主 UI toolkit；大小限制为
  1..128 px，lowercase 确定性折叠为 uppercase。
- TextView wrap_content 由 text metrics + padding 决定；string/size mutation 会改变后续
  geometry/draw，固定 `HI` RGBA golden 逐像素锁定。
- Button 使用独立 UiClass，具有确定性 background、padding、clickable 默认值和 text content。
- 当前明确只支持单行与内置字形；多行、未知字形、非法 size，以及 LUI-14 前的 text/color
  resource reference 均明确失败且 mutation 保持事务性。
- 定向 TextView/Button、renderer、overlay 测试与 full CTest 752/752 通过。
