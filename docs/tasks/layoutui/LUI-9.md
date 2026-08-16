# LUI-9 · Generic UI pointer dispatch

## 目标（一句话）

只用 UiTree resolved geometry 完成 topmost hit、pointer capture、touch/click 与 fallthrough。

## 依赖

- LUI-3、LUI-5、LUI-6。

## 验收与结果

- input 前 dirty traversal；沿 clipped tree reverse document order hit-test。
- 仅 VISIBLE + enabled + listener node 命中；INVISIBLE/GONE/removed 不命中。
- DOWN capture，MOVE/UP 送 OnTouchListener；listener 未消费时仅 UP-inside 调 OnClick。
- target 隐藏/删除/移出取消 click；无 target 完整回退 Activity touch。
- guest listener 使用 UiNodeId binding 恢复 exact View identity；旧 bounds 来源已于 LUI-6 删除。
