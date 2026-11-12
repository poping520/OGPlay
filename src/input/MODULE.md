# 模块：input

## 职责

把 HAL 键鼠/手柄事件映射为触摸、传感器、物理按键和轨迹球事件。

## 公共 API

- `MouseTouchMapper`：把宿主鼠标主键的 down/drag/up 转为固定 pointer id 0 的单点触摸；
  只在有效内容区开始手势，拖出内容区后仍以夹紧坐标闭合 release。
- `TouchMapper`：复用鼠标映射，并接收 HAL 原生 finger 事件，生成平台无关 TouchSnapshot。
  同一逻辑触屏同时捕获一个物理触摸设备或鼠标；保留 64 位宿主身份，最多 16 个触点。
  接触期间 pointerId 稳定，UP 快照包含离开的触点，后续快照才删除；Android action
  编码归 session；压力与中性修饰键状态随快照保留。窗口重置、后台及 finger cancel
  终止整个捕获手势。
- `ControlMapper`（[Input 设计](../../docs/design/input.md)、ADR-0133）：一块逻辑触屏，
  宿主触摸、鼠标主键、绑定按键与外部（MCP）触点共用最多 16 个触点，pointerId 取最小
  空闲值且存续期间稳定，溢出拒绝并计数。`Accept` 记录 HAL 事件，`Flush` 每 guest 帧
  调用一次，按“按键输出 → MOVE → UP → DOWN”顺序返回本帧输出；同帧多次移动合并为一次
  MOVE。原语为 `direct`、`button`（tap/hold/repeat）、`stick`（四键组、固定原点、8 向）、
  `key`（Android keycode）、`mode`（toggle/hold）、`look`（准星：`mouse:motion` 拖动视角）与
  `lock`（键盘键切换指针锁定）。
  输入源另有 `mouse:motion`（仅 look）与 `wheel:up|down`（每满一格按下并抬起一次，可用于
  button/key/mode，不可用于 stick）。`ControlConfig` 为已解析的数据；
  `DefaultControlConfig` 为内置默认（鼠标/触摸直通、Esc→`KEYCODE_BACK` 且放开锁定的指针）；
  `ParseInputSource`/`ParseAnchor` 解析物理键名、鼠标键名与九宫格锚点，`FormatInputSource`/
  `AnchorName` 为其逆。`Reconfigure` 先以新配置构造并按当前 surface 校验，成功后才取消旧
  布局并返回其 CANCEL 与按键释放；`Layout` 返回按当前 surface 解析的区域（ADR-0134）；
  `Hints` 返回当前基础模式、`*` 与按住叠加模式中有效绑定所用区域及其源，供 F1 提示。
- 滚轮捏合（内置、不经配置）：HAL `pointer_wheel` 在内容区内合成水平双指；
  +delta_y（向上滚）张开即放大，每格间距 ×1.2。主指（ACTION_DOWN）落在光标处且全程不动，
  只有副指移动；副指在右侧放得下起手间距时放右侧，否则放空间较大的一侧。几何按 surface
  宽度比例：起手间距 0.30、范围 0.08..0.60（且不超出所在侧）、副指每帧至多 0.02。
- 触控板捏合：HAL `pinch_begin` 在光标处起手（几何同滚轮），每个 `pinch_update`
  按其 scale 乘间距，`pinch_end` 后按静止规则抬起；手势进行中停顿不抬指，期间忽略滚轮。
- `look`（准星）：区域中心即按下点。指针锁定后，鼠标相对位移 × 各轴灵敏度（可反转 Y）
  累计为 guest 像素；有位移时在准星按下，静置 settle 帧（默认 0）后每帧一次 MOVE 跟随，每帧
  至多移动短边 35%，待走位移至多两步（超出丢弃，鼠标停下后视角一两帧内停住），可划过整个
  surface，距边缘保留短边 3%；`max_offset_x/y`
  > 0 时再限制在准星两侧各该距离内。到达限位时停在该处并继续剩余位移：默认下一帧抬起、再下一帧在准星按下（同帧抬起再按下同一 pointerId 会被按帧采样的
  引擎合并成跳回）；`look_handoff`（双指交接）下一帧以另一触点身份在准星先按下新指，旧指再
  多按一帧，随新指首次 MOVE 抬起。锁定期间鼠标静止也不抬指，放开或切到无
  look 的模式时抬起。`release_pointer` 的 button/key 按下时（有激活 look）放开指针。
- 指针默认放开：`lock` 绑定（`TogglePointerLock`）在有激活 look 时锁定，已锁定时放开；
  `release_pointer` 绑定按下时放开，内置 Esc→Back 默认带此标记；锁定状态跨 `Reconfigure` 保留。
  `WantsRelativePointer` = 未放开且有激活 look，宿主据此锁定指针；`LookActive`、`LockSources`
  供宿主提示锁定键。
- M6 在此基础上定义完整映射 schema、逻辑/物理坐标转换与录制回放。

## 不变量

- 坐标换算只有一处实现。
- 输入配置只由内置直通/Esc 默认与用户层组成；区域及游戏按键均由用户创建，不加载
  Profile、类型模板或应用内置游戏布局（ADR-0142）。
- 触摸 down/up 成对且保留帧时序；中断用 cancel 闭合。传感器扩充不属于本阶段。
- 鼠标悬停、非主键及其他设备的事件不得进入已捕获的单点触摸序列。
- `ControlMapper` 的合成触点按下与抬起至少相隔一帧；`stick` 在原点按下并停留
  `stick_settle_frames`（默认 2）帧，之后每帧最多移动 `travel / stick_ramp_frames`（默认 4）
  滑向当前方向，换向同样渐进，不以单次 MOVE 跳到目标。区域按“区域自身锚点 = surface 锚点 + 偏移”解析，越界、未知区域、
  同模式源冲突、`*` 模式与其他模式源冲突均在构造或 `SetSurface` 时明确失败。
- 未被当前模式绑定的键直通；已直通按下的键其抬起与重复始终直通；绑定在按下时
  才接管源，模式切换停用的绑定立即释放其触点，需重新按键才生效。
- 捏合双指同帧按下，静置 2 帧后才移动；滚轮捏合静止 3 帧后同帧抬起，触控板捏合在 end 之后
  同样静止 3 帧才抬起；到达间距上下限仍有剩余输入时先抬起、下一帧在原锚点重新起手
  （剩余量至多一个完整范围，触控板手势仍在进行时照常重新起手）。只在没有其他触点时起手，
  否则丢弃该次滚动或整次触控板手势；鼠标主键、宿主触摸或外部触点按下时立即抬起捏合，
  该触控板手势后续 update 忽略，不形成三指。
- 指针锁定（有激活的 look 且未放开）期间鼠标只经绑定生效：未绑定鼠标键不再直通触摸（锁定
  前已按下的拖动照常结束），滚轮与触控板不触发内置捏合。有激活 look 而指针放开时，look 暂停，
  鼠标键与滚轮绑定一并让出：鼠标按键直通触摸、滚轮走内置捏合，F1 提示不列只由鼠标/滚轮
  驱动的区域；放开前已按住的鼠标绑定仍经绑定抬起。没有激活 look 时鼠标键与滚轮绑定照常生效。
  当前模式绑定了任一滚轮方向时滚轮只走绑定，未绑定方向无动作，不触发捏合。
- `input_reset`、后台、宿主触摸 cancel 与 surface 尺寸变化取消全部触点并释放直通键；
  `Reset` 仅在宿主已取消 guest 输入流时静默清空。

## 禁止

- 不包含特定游戏的坐标、按键或分辨率。
- 不直接读取 SDL 事件；设备访问经 HAL。

## 测试

`tests/input/` 的时序、曲线、坐标和回放测试。
