# 输入映射

输入子系统把宿主键鼠、触摸和手柄事件转换成一个逻辑 Android 输入流。
HAL 读取设备，input 维护映射与触点时序，session 编码 Android 事件，
WMS 和 ROM View 层级分别负责窗口命中与树内分发。

## 一条输入路径

```text
HAL 设备事件 ─┐
用户按键绑定 ─┼→ ControlMapper → ControlOutput → session → WMS → ROM
MCP 外部触点 ─┘
```

input 不直接调用 SDL，也不读取游戏包名。
输出使用逻辑 surface 坐标，不使用宿主窗口像素作为永久配置。
同一逻辑触屏共享触点预算，外部控制不能另建一套 pointerId 空间。
Android action 编码归 session，映射器只描述通用动作。

## 设备捕获与坐标

鼠标主键可以直通触摸，down 必须从有效内容区开始。
拖出内容区后继续夹紧并闭合 release，不能遗留按住状态。
原生 finger 事件保留宿主身份到逻辑 pointerId 的稳定对应。
同一捕获序列不混入另一物理设备的悬停或无关按键。

窗口 letterbox、逻辑尺寸与呈现尺寸的变换集中处理。
用户区域按 surface 锚点和相对偏移解析。
区域自身锚点决定其哪一点落在 surface 锚点上。
布局失败在启动或重配置前报告，不能在首个按键时才产生坏坐标。

显示旋转或 surface 尺寸变化会取消已有手势。
新布局重新解析，旧触点不能无声移到另一位置。
取消先交付给 guest，再清除宿主映射状态。

## ControlMapper 的每帧模型

Accept 接收事件并保存物理源状态。
Flush 每 guest 帧调用一次，生成本帧输出。
同一帧多次 motion 可以合并，但 down/up 的帧间边界必须保留。
输出按按键、MOVE、UP、DOWN 的既定顺序交付。

pointerId 从空闲集合分配，存续期间稳定。
UP 快照包含离开的触点，之后才移除身份。
合成按下与抬起至少相隔一个 guest 帧。
溢出明确拒绝并计数，不挤掉已有手势。

Reconfigure 先用新配置完成解析和 surface 校验。
成功后才取消旧触点并释放已直通按键。
错误配置不破坏旧布局。
Reset 只在上层已经取消 guest 输入流后静默清状态。

## 操控原语

button 把物理源映射为区域内的 tap、hold 或 repeat。
stick 将方向键组合变为固定原点的触摸拖动。
key 产生 Android keycode，而不是屏幕坐标。
mode 切换 toggle 或按住叠加层，允许一个物理设备承担不同上下文。
look 把相对鼠标位移转换成视角拖动。
lock 切换相对指针捕获。

stick 起手在原点停留，然后按帧渐进到方向位置。
换向也按同一曲线推进，不能瞬间跳到对侧。
这保留按帧采样游戏识别触屏摇杆所需的时序。
具体 settle 与 ramp 参数由用户数据和契约维护。

未绑定键可以直通。
已直通按下的源，其后重复和抬起继续直通。
绑定只在按下时接管源，不在半途窃取已开始序列。
切模式停用的绑定立即释放，重新按键后才在新模式生效。

## 用户配置

配置只合并内置默认与用户 input.toml。
内置提供鼠标/触摸直通及 Esc→Back。
不加载 Profile 输入、类型模板或官方游戏坐标布局。
按游戏差异属于安装实例的用户数据，不进入 src。

区域与 binding 分别定义几何和物理源。
同模式源冲突、未知区域和越界布局明确失败。
通配模式与普通模式的冲突也要在解析时处理。
旧 input.toml 的未知键可跳过；编辑器 JSON 请求继续使用 closed schema。

GUI 为每实例显式传入 input.toml 路径。
裸 CLI 可以使用 --input-config 或约定的沙盒侧文件。
缺文件视为空用户层，不能自动寻找名称相似的游戏模板。
保存使用原子替换，配置与启动设置文件分开。

## 滚轮与触控板捏合

未被显式绑定接管的滚轮在内容区合成双指捏合。
主指落在光标处并保持位置，副指按空间选择一侧。
间距按 surface 比例计算，逐帧移动，不直接修改游戏缩放值。
触控板 pinch 使用相同几何，begin/update/end 保留手势阶段。

捏合只有在没有其他触点时起手。
鼠标直通、触摸或外部触点到来时先结束捏合。
被打断的触控板序列不在后续 update 中重新插入第三指。
到达几何限位时按明确抬起、下一帧重新起手的协议处理。

若某模式绑定滚轮方向，则滚轮只走绑定。
未绑定的另一方向也不会同时触发内置捏合。
这避免一次滚动既产生按键又改变游戏视角。

## look 与指针锁定

look 的区域中心是起手准星。
相对位移乘各轴灵敏度后累计为 guest 像素，可反转 Y。
每帧限制移动范围与待处理余量，鼠标停下后不无限排队漂移。
到边缘根据 single 或 handoff 规则重新起手。

single 先抬起，后续帧在准星按下。
handoff 让新身份先按下，再在新指开始移动时移除旧指。
同帧复用 pointerId 容易被游戏帧采样合并，因此必须保留交接时序。
锁定期间静止不自动抬指。

WantsRelativePointer 由活跃 look 和用户锁定状态决定。
前端负责请求宿主捕获；宿主拒绝时记录警告。
Esc 和 release_pointer 绑定解除捕获。
放开指针时暂停 look，让出鼠标直通与捏合。
后台、编辑冻结和失败也应放开宿主指针。

## 提示与按键编辑器

F1 提示读取当前模式中有效绑定的区域与源。
它使用同一解析布局，不复制一套坐标计算。
指针放开时，不显示当前让出的鼠标驱动区域。
提示不会向 guest 注入额外输入。

编辑器通过受令牌保护的 loopback control.* 接口操作。
freeze 在主循环冻结游戏并取消活动输入，再提供最近帧。
apply 在同一校验路径构造新配置，成功后保存。
网络 worker 只排队请求，不能直接执行 guest 或重配映射器。

## 与窗口派发的边界

ControlMapper 不知道哪个 View 被点击。
WMS 命中窗口并捕获手势，ROM ViewGroup 决定 intercept 和 CANCEL。
NDK InputQueue 与 Java MotionEvent 使用相同样本事实。
文本和 composing 由 IMMS 走活跃输入连接，不当作键位配置生成。

已实现映射不等于每个平台的手柄、相对鼠标和触控板都已实跑。
具体未验收范围集中见已知问题。
操作和复现按现有 MCP 手册进行，不另建输入驱动脚本协议。

## 契约、决定与操作

- [input](../../src/input/MODULE.md)、[session](../../src/session/MODULE.md)
- [frontend](../../src/frontend/MODULE.md)、[Android 输入服务](../../src/runtime/integration/dexvm_android/MODULE.md)
- [输入映射决定](../adr/session.md#adr-0133)、[用户输入数据](../adr/session.md#adr-0142)
- [MCP 测试](../playbook/MCP-TESTING.md)、[已知问题](../state/KNOWN-ISSUES.md)
