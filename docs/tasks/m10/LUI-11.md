# LUI-11 · 通用 LinearLayout 与动态 hierarchy

## 目标（一句话）

让 XML 与 Java 动态构造的 horizontal/vertical LinearLayout 共享同一 geometry/input 事实。

## 依赖

- LUI-10。

## 验收与结果

- vertical/horizontal 主轴 traversal 支持 padding、四向 margin、INVISIBLE/GONE 与有限非负
  weight 的确定性剩余像素分配；非法 weight 明确失败。
- `ViewGroup.addView` 三个 overload、`removeView/removeViews/updateViewLayout` 真实维护
  UiTree document order；重复 parent、错误 index/params 明确抛 Java 异常。
- ViewGroup/FrameLayout/LinearLayout LayoutParams constructor 与 `setMargins` 保存 typed
  width/height/weight/margin，`View.set/getLayoutParams` round-trip object identity。
- `Activity.setContentView(View)` 保留已动态建立的 subtree，并安全退休旧 content subtree；
  `getLeft/Top/Right/Bottom/Width/Height` 在 dirty traversal 后返回 resolved frame。
- generic pure layout 与 DexVM dynamic hierarchy/geometry tests 机器判定通过。
