# LUI-4 · Registry inflater 与 merge

## 目标（一句话）

把 `Activity.setContentView(int)` 从内联 tag 特判迁为 registry inflater，并用 synthetic
content root 正确承载普通 root 与 `<merge>`。

## 依赖

- LUI-1..3。

## AOSP 4.4.4 语义参考

- `LayoutInflater.inflate/createViewFromTag/rInflate`：document order 创建、attach children。
- `PhoneWindow.setContentView`：content parent 是 inflate attach target。
- `<merge>` 不创建 View，必须有 attach parent，children 直接 attach target。

## 范围

- `UiWidgetDescriptor` 固定 registry：XML tag → dex descriptor + UiClass。
- generic typed base attrs/LayoutParams：id、visibility、width/height、gravity、orientation、
  padding/margin/weight、src。
- `<merge>` 只允许唯一 root；普通 root attach synthetic content root。
- unknown tag、非法 root/parent、未知 `layout_*` structural attr 明确失败并记 gap hit。
- inflation 事务失败只留下空的新 generation；Activity switch 销毁旧 generation。

## 验收

- 通用 merge fixture 的 VideoView/TextView/LinearLayout/ImageButton tree、parent order、id、
  visibility 与 guest class identity exact。
- unknown structural tag、nested merge 明确失败且不发布部分 tree。
- 第二次 inflation 后旧 node/listener 均失效。

## 实施结果

- inflater 已从 `android_app_Activity.cpp` 提取，旧内联 tag map 与静默 unknown re-parent
  删除；`setContentView(View)` 同样建立新的 synthetic content generation。
- drawable intrinsic measure 暂继续填充 legacy geometry adapter，LUI-6 迁入正式 layout。
