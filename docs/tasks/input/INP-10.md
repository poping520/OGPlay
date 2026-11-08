# INP-10 · 相对鼠标与 look 视角原语（FPS 键鼠 P1a/P1b）

状态：实现完成，实机验收待用户。依赖：INP-9。设计：[Input 设计](../../design/input/README.md)
第 4、5、9 节（`look`、`mouse:motion`、`wheel:up|down`、相对鼠标）；决策：
[ADR-0142](../../adr/session.md#adr-0142)。

目标：FPS/TPS 游戏用鼠标转视角、WASD 移动、鼠标键开火、滚轮切枪；配置写在 input.toml。

## 改动与边界

- HAL：`WindowInput::SetRelativePointer`（SDL `SDL_SetWindowRelativeMouseMode`），失败返回 false。
- input：`look`（准星）与 `lock` 原语，`mouse:motion`、`wheel:up|down` 输入源；规则见
  `src/input/MODULE.md`。参考夜神/逍遥（准星点、`~` 切换锁定、Esc 等放开）与 scrcpy-mask
  （起点 + 全屏拖动范围、可选水平/垂直最大偏移、锁定期间手指常按、回中累积位移）。
  - 准星是按下点，手指可划过整个画面（离边缘留短边 3%），可选最大偏移限制。到边缘时同帧
    抬起旧指、在准星按下新指；按下停留默认 0 帧。锁定期间鼠标静止不抬指。
  - 指针默认放开；`lock` 键（编辑器默认 `~`，占用时 F3、F4）锁定/放开；内置 Esc→Back 带
    `release_pointer`，按下时同时放开，可在编辑器取消。有准星而指针放开时鼠标键与滚轮绑定让出（直通点击、滚轮捏合）；无准星时绑定照常。
- session：input.toml/JSON 读写 look（zone、sensitivity、sensitivity_y、invert_y、settle_frames、
  max_offset_x、max_offset_y）与 lock。
- frontend：run-apk 每帧同步相对指针；编辑冻结、MCP 暂停、后台、失败时放开。
  F1 提示把 `mouse:motion` 标为 MOUSE、滚轮标为 WH↑/WH↓。
- 编辑器：“添加准星（鼠标视角）”（每模式一个，默认右侧中部小圆，同时添加 `~` 锁定键，`~`
  已占用则不加）；无绑定区域可“设为准星”；准星画为十字，选中时虚线框显示手指拖动范围；
  面板可调锁定键、水平/垂直灵敏度、垂直反转、最大水平/垂直偏移、按下停留；删除准星同时
  删除锁定键。按钮绑定框内滚动滚轮可绑定滚轮。
- 过程：首版用“视角区”矩形限制拖动范围，区域小时频繁回中出现“断触”；放开光标后左右键仍被
  开火绑定占用。按用户实测与模拟器调研改为上述准星模型。准星版实测快速左甩时左摇杆瞬间
  出现，推测为单帧大跨度移动被游戏重判或准星压在左半屏边界；改为每帧至多移动短边 20%，
  待复测。
- UI 调整：画面上右键控件弹出删除菜单（不再出现 WebView 默认菜单、右键不拖动）；新摇杆外圈
  短边 22%、行程 0.6（原 16%、0.7），实际行程约短边 13%；删除宿主 F3（与 `~` 重复），Esc 的
  放开改为可见的 `release_pointer` 选项（“其他绑定”勾选“同时放开鼠标”），快捷键栏写为
  “安卓返回键（同时放开鼠标）”；无锁定键时快捷键栏与 F1 提示（`NO MOUSE LOCK KEY`）提醒。
- 第二批：回中方式 `regrip = "single" | "handoff"`（双指交接：新指先按、旧指下一帧随新指首次
  MOVE 抬起）；button/key 的 `release_pointer`（如地图、背包键按下时放开鼠标）。
- 提示：编辑器顶部“游戏中快捷键”条以键帽图标显示锁定键（`~` 标注“波浪键”）、Esc、F1、F2；
  准星面板的锁定行同样醒目。F1 游戏内提示第二行显示锁定键，字形新增 `~`，`` ` `` 键标签统一为 `~`。
- 第二批实测（Dead Trigger，Unity）：单指同帧回中“来回弹、视角回到之前位置”，双指交接左滑
  持续触发移动摇杆，快速左甩仍闪现摇杆；当时准星在画面正中（`anchor = "center"`），开火键为
  `press = "tap"`。处理：单指回中改为抬起后下一帧再按下；编辑器在准星中心 x < 55% 宽时画红并
  提示移到右半边；回中选项改为“抬起再按下（推荐）/双指交接”并说明双指交接的风险；鼠标键
  新建按钮默认“按住”。复测：准星移到右半边后闪现消失（根因为准星压在移动区边界）。
- 再次实测：快速左右甩时后半段“瞬间滑动”，为限速 20% 加两倍画面积压导致鼠标停后仍追赶。
  改为每帧至多短边 35%、积压至多两步（超出丢弃）；编辑器连发间隔默认由 6 帧改为 2 帧。待复测。
- 未做：观察键（小眼睛）。

## 示例（Dead Trigger 类，坐标由用户按实际画面调整）

```toml
schema = 1

[[zone]]
name = "aim_1"
anchor = "right"
x = -440
y = -40
r = 18

[[zone]]
name = "move"
anchor = "bottom_left"
x = 120
y = -120
r = 110

[[zone]]
name = "fire"
anchor = "bottom_right"
x = -260
y = -60
r = 60

[[binding]]
source = "mouse:motion"
action = "look"
zone = "aim_1"
sensitivity = 1.0

[[binding]]
source = "key:grave"
action = "lock"
mode = "*"

[[binding]]
source = "keys:w,a,s,d"
action = "stick"
zone = "move"

[[binding]]
source = "mouse:left"
action = "button"
zone = "fire"
press = "hold"
```

## 验证

- `ogplay_tests -tc="INP-10*"`：源解析/格式/校验（含 lock 只用键盘、偏移非负）；默认放开、lock
  键切换、无准星时不可锁定、Reconfigure 保留锁定；准星按下、即时跟随、合并 MOVE、静止
  常按、放开抬起；全屏拖动、边缘同帧回中与剩余位移、向左划过左半屏不回中；最大偏移；
  灵敏度/反转/停留；锁定期间鼠标与滚轮、模式切换；滚轮绑定整格点按；放开后直通点击、
  滚轮捏合、F1 提示与重新锁定后恢复开火。session：look/lock/滚轮 TOML/JSON 往返与类型校验。
- `ogplay_app_tests -tc="*INP*,*control*"` 通过；F3 不再被宿主截留、内置 Esc 放开指针有断言。
- Web：`tsc --noEmit`、Vitest controls 21 例（准星默认值与锁定键、占用时不加锁定键、删除连带、
  拖动范围计算、滚轮捕获、锁定键 F3/F4 备选、摇杆新默认值）与 i18n 覆盖通过；`npm run build` 重建 controls 制品。
- 第二批与提示：`INP-10 look travels … at a finger's pace`（每帧 ≤96 px、向左划过不回中）、
  `handoff regrip`、`a button or key can free a locked pointer`、`the hint banner names the mouse lock
  keys`；session 往返含 regrip/release_pointer；Vitest 21 例含 `~` 标签与“波浪键”说明。
- 待用户：Dead Trigger 实机手感（快速左甩是否仍出现摇杆、单指/双指交接对比）与编辑器提示。
