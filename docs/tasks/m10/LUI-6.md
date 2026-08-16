# LUI-6 · Horizontal LinearLayout 与 visibility

## 目标（一句话）

让底部按钮行按通用 horizontal LinearLayout 和 drawable intrinsic 规则获得 geometry。

## 依赖

- LUI-5。

## AOSP 4.4.4 语义参考

- `LinearLayout.measureHorizontal/layoutHorizontal`：document-order 主轴累计、parent
  gravity、child cross-axis gravity、margin/padding。
- `View.GONE` 不参与 measure/layout；`INVISIBLE` 参与但不命中。

## 验收与结果

- controls-equivalent row：GONE sibling 完全移除、INVISIBLE 保留空间、visible ImageButton
  由 decoded drawable intrinsic wrap measure 并居中。
- visibility 改为 GONE 后下一次输入前重做 traversal。
- `support_widget_dispatch` 的 fullscreen/edge-row bounds 特判删除，现只读取 UiTree
  resolved `screen_frame`。
