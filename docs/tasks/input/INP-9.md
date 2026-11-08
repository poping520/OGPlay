# INP-9 · 滚轮与触控板合成双指捏合

状态：完成（macOS 滚轮/触控板手测）。依赖：INP-8。设计：[Input 设计](../../design/input/README.md)
第 4、8 节（内置默认“滚轮→pinch，以光标为心”）；决策：[ADR-0142](../../adr/session.md#adr-0142)。

目标：PC 上用滚轮或触控板捏合完成游戏的双指缩放（如关卡需缩小才能看到全景）。

## 改动与边界

- HAL：SDL `SDL_EVENT_MOUSE_WHEEL` 映射为 `pointer_wheel`，携带窗口坐标与 OS 已处理自然
  滚动方向的 delta；触摸合成的滚轮不转发。
- input：`ControlMapper` 内置捏合，向上滚张开放大；同帧双指按下、静置、逐帧滑动、静止后
  抬起，越过间距限位时抬起再起手。几何与时序常量、互斥规则见 `src/input/MODULE.md`。
- 主指固定在光标处：首版以光标为中心对称放置双指，Angry Birds 实测在弹弓右侧滚轮缩小时，
  左侧手指作为 ACTION_DOWN 落在小鸟上，被游戏当作拉弓，合拢后松开把小鸟向后弹出。
  改为主指落在光标、仅副指移动后，首个触点由用户指向的位置决定。
- 触控板：SDL `SDL_EVENT_PINCH_BEGIN/UPDATE/END` 映射为 `pinch_begin/update/end`。Cocoa 的
  pinch 不带窗口和坐标，HAL 仅在光标位于本窗口时上报 begin 并取光标位置，update/end 始终
  上报（scale 为相对上次 update 的比例）。默认设置下 macOS 触控板手指触摸以
  `SDL_MOUSE_TOUCHID` 上报，HAL 本就丢弃，不与捏合冲突。ControlMapper 把每次 scale 换算为
  间距，手势进行中停顿不抬指，end 后静止抬起。
- 不进入 input.toml/编辑器：不可解绑、不可反向，无键盘或拖动触发。触控板双指滚动同样经
  滚轮事件触发捏合。Windows 精确式触控板捏合是否产生 SDL pinch 事件未验证。

## 验证

- `ogplay_tests -tc="INP-9*"`：滚轮 3 例（主指落在光标且不动、副指起手位置/静置/逐帧步长/静止抬起、
  缩小方向、限位重新起手及剩余量上限、左右边缘副指选侧、拖动中丢弃滚轮、按下鼠标先抬双指、
  CANCEL、黑边/零增量）；触控板 3 例（光标起手、逐次 scale、停顿不抬指、end 后抬起、限位重新
  起手并继续按住、无 begin 的 update 与黑边 begin 忽略、拖动中拒绝整次手势、按住期间忽略滚轮、
  鼠标按下抬起并忽略后续 update）；SDL 无窗口 pinch 映射 1 例。
- `SDL events map to backend-independent keyboard and pointer events` 扩展滚轮映射断言。
- 用户实测首版：Angry Birds 滚轮缩放正常，但光标在弹弓右侧缩小会把小鸟向后弹出（见上）；
  修正后用户复测基本正常。macOS 触控板捏合用户实测缩放正常。
