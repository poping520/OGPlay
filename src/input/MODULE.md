# 模块：input

## 职责

把 HAL 键鼠/手柄事件映射为触摸、传感器、物理按键和轨迹球事件。

## 公共 API

- `InputTemplateCatalog`：M5 注册不可变的通用 mapper，以稳定 id 精确选择并转换 HAL
  输入事件；构造时必须指定已注册的默认模板。
- `MouseTouchMapper`：把宿主鼠标主键的 down/drag/up 转为固定 pointer id 0 的单点触摸；
  只在有效内容区开始手势，拖出内容区后仍以夹紧坐标闭合 release。
- `TouchMapper`：复用鼠标映射，并接收 HAL 原生 finger 事件，生成平台无关 TouchSnapshot。
  同一逻辑触屏同时捕获一个物理触摸设备或鼠标；保留 64 位宿主身份，最多 16 个触点。
  接触期间 pointerId 稳定，UP 快照包含离开的触点，后续快照才删除；Android action
  编码归 session；压力与中性修饰键状态随快照保留。窗口重置、后台及 finger cancel
  终止整个捕获手势。
- `ControlMapper`（[Input 设计](../../docs/design/input/README.md)、ADR-0133）：一块逻辑触屏，
  宿主触摸、鼠标主键、绑定按键与外部（MCP）触点共用最多 16 个触点，pointerId 取最小
  空闲值且存续期间稳定，溢出拒绝并计数。`Accept` 记录 HAL 事件，`Flush` 每 guest 帧
  调用一次，按“按键输出 → MOVE → UP → DOWN”顺序返回本帧输出；同帧多次移动合并为一次
  MOVE。原语为 `direct`、`button`（tap/hold/repeat）、`stick`（四键组、固定原点、8 向）、
  `key`（Android keycode）、`mode`（toggle/hold）。`ControlConfig` 为已解析的数据；
  `DefaultControlConfig` 为内置默认（鼠标/触摸直通、Esc→`KEYCODE_BACK`）；
  `ParseInputSource`/`ParseAnchor` 解析物理键名、鼠标键名与九宫格锚点。
- M6 在此基础上定义完整映射 schema、逻辑/物理坐标转换与录制回放。

## 不变量

- 坐标换算只有一处实现。
- 触摸 down/up 成对且保留帧时序；中断用 cancel 闭合。传感器扩充不属于本阶段。
- 鼠标悬停、非主键及其他设备的事件不得进入已捕获的单点触摸序列。
- 模板 id 唯一且只接受规范小写标识；未知模板明确失败，不猜测相似名称。
- `ControlMapper` 的合成触点按下与抬起至少相隔一帧；`stick` 在原点按下并停留
  `stick_settle_frames`（默认 2）帧，之后每帧最多移动 `travel / stick_ramp_frames`（默认 4）
  滑向当前方向，换向同样渐进，不以单次 MOVE 跳到目标。区域按“区域自身锚点 = surface 锚点 + 偏移”解析，越界、未知区域、
  同模式源冲突、`*` 模式与其他模式源冲突均在构造或 `SetSurface` 时明确失败。
- 未被当前模式绑定的键直通；已直通按下的键其抬起与重复始终直通；绑定在按下时
  才接管源，模式切换停用的绑定立即释放其触点，需重新按键才生效。
- `input_reset`、后台、宿主触摸 cancel 与 surface 尺寸变化取消全部触点并释放直通键；
  `Reset` 仅在宿主已取消 guest 输入流时静默清空。

## 禁止

- 不包含特定游戏的坐标、按键或分辨率。
- 不直接读取 SDL 事件；设备访问经 HAL。

## 测试

`tests/input/` 的时序、曲线、坐标和回放测试。
