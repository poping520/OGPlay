# GUI-8 · 空库 ImGui ID 冲突修复与冒烟补强

## 目标

修复首次空库视图中两个“导入游戏”按钮的有效 ImGui ID 冲突，并让同类回归成为真实
GUI 冒烟可判定的失败。

## 依赖

- GUI-7：主面板基础版闭环。

## 结果

- 顶栏、空库引导及全部现有按钮使用稳定隐藏 ID，显示文案不变。
- `GuiButton` 按 ImGui 的实际 `GetID` 结果审计每帧可见按钮；窗口与 `PushID` 作用域
  已包含在有效 ID 中，合法的跨作用域同名按钮不受影响，同一作用域重复 ID 立即明确
  失败。
- `frontend.gui_smoke` 覆盖空库首屏，今后重复按钮 ID 会使冒烟非零，而不是只出现
  Dear ImGui programmer-error 覆盖层。

## 验收

Windows/MSVC `/W4 /WX` 构建；架构检查 4/4；空库/非空库真实 ANGLE 冒烟通过；
完整 CTest 692/692。
