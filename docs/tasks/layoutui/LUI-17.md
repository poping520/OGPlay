# LUI-17 · Content View 触摸分发

## 目标（一句话）

在无 listener View 命中时，按通用 View 语义把触摸先交给 Activity 的 content view，
并由消费 DOWN 的 content view 持有后续手势。

## 依赖与边界

- 依赖 LUI-9、LUI-16 的 UiTree listener 命中与 click ownership。
- 不改变 listener View 的优先级，不实现完整 ViewGroup dispatch/intercept，不加入 title
  身份或专属分支。

## 验收与结果

- `View.onTouchEvent(MotionEvent)` 成为可覆写的基类方法，默认返回 false。
- 无 listener target 的 DOWN 先虚派发到 `content_view.onTouchEvent()`；返回 true 建立
  capture，MOVE/UP 继续送达，UP 后释放。未消费事件才回退
  `Activity.onTouchEvent()`。
- Activity 切换清空 listener/content 两类手势状态，旧 content view 不得继续收事件。
- `tests/dexvm/widget_click_tests.cpp` 以 guest-style 覆写锁定 DOWN true/false、
  MOVE/UP capture 与释放（21 条断言）。
- Windows Release exact MCP：frame 710 为封面，点击 `(512,300)` 后 frame 740 进入
  `NEW GAME / OPTIONS / MORE GAMES` 主菜单；会话保持 running、无 guest fault，随后
  shutdown 在 frame 740 clean stop。该单轮探索验证修复，不冒充三轮 scenario gate。
- `cmake --preset windows-msvc`、完整 Debug build 与 full CTest 通过（776/776）；
  `CURRENT.md` 6039 bytes，文档布局门禁通过。
