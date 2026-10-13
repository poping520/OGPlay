# Input · 通用 PC 操控映射方案设计

本文定义 OGPlay 把键盘、鼠标和手柄转换为触屏游戏输入的**通用方案**。目标是让任意
为手机触屏设计的老游戏都能在 PC 上用键鼠或手柄游玩：零配置也能点得动，按玩法套用
模板即可上手，单款游戏只需提供数据。决策见 [ADR-0133](../../adr/session.md#adr-0133)。

## 效力声明

- 本文是输入映射子系统的长期设计上下文。实施后由 `src/input`、`src/session` 的
  MODULE 契约接管接口细节；本文保留设计溯源与阶段边界。
- 产品代码不得出现游戏名、包名或某款游戏的坐标、键位；游戏差异只能是
  `data/` 下的配置数据或用户覆盖。
- 若实施需要改变模块依赖方向、发布新的 guest 设备事实（如加速度计）或扩展到宏/脚本，
  先追加 ADR。

## 1. 目标与非目标

**目标**

1. 键鼠和手柄可以同时、多点地驱动 guest 触屏，满足“边走边打”。
2. 用一组封闭、通用的**操控原语**覆盖手机触屏游戏的常见交互形态。
3. 配置分四级：内置默认 → 类型模板 → 游戏配置 → 用户覆盖；后一级只写差异。
4. 合成在统一 Clock 的帧边界上进行，可确定地测试、录制和回放。
5. 玩家能看到当前键位（提示叠加层），并能自行修改（GUI 编辑器）。

**非目标**

- 宏、脚本、连招序列和定时自动操作；自动瞄准；读取游戏内存或画面来识别 UI。
- 宿主全局键盘钩子、修改宿主系统输入设置。
- 不改变已有的 Android 输入投影：MotionEvent/KeyEvent/AInputQueue 契约以
  [ADR-0086](../../adr/session.md#adr-0086) 与 session MODULE 为准。
- 手柄震动等输出能力；倾斜（加速度计）与文本输入法本方案只定义接口位置，
  能力本身另行立项（见第 9 节）。

## 2. 现状（2026-10-13）

| 层 | 已有 | 缺口 |
| --- | --- | --- |
| hal | `InputEvent` 含键盘（物理 scancode + 布局 keysym + 修饰键）、指针移动/按键（含 delta）、宿主触摸、手柄轴/按键/插拔、`input_reset`、前后台 | 无滚轮事件；无相对鼠标（指针锁定）接口；手柄事件无消费者 |
| input | `MouseTouchMapper` 鼠标主键→单点触摸；`TouchMapper` 合并宿主触摸，最多 16 触点，但同一时刻只捕获一个物理来源；`InputTemplateCatalog` 按 id 选择 mapper | 模板目录未接入运行路径；没有按键/手柄到触摸的合成 |
| session | `MapAndroidTouch` 把 `TouchSnapshot` 投影为 API19 动作；`MapAndroidInput` 把物理键直通为 Android KeyEvent；`AndroidInputTimeline` 用 guest Clock 重定时 | 无操控配置的装载与合并 |
| frontend | run-apk 事件循环：`TouchMapper` → 触摸，其余键 → KeyEvent；MCP 指针手势仅在本地触摸未激活时注入 | MCP 与本地触摸互斥；无提示叠加层与编辑器 |
| profile | v2/v3 schema 的 `input.profile` 只是模板 id，现有 Profile 均未使用 | 无区域、绑定和参数的数据模型 |
| 设备事实 | 传感器为“无设备”facade；输入法无会话阶段 | 倾斜、文本输入依赖独立能力 |

## 3. 架构

```text
hal::InputEvent（键盘 / 鼠标 / 滚轮 / 宿主触摸 / 手柄）      MCP 手势
        │                                                        │
        ▼                                                        ▼
input::ControlMapper ─────────────────────────────────────────────┐
  ├─ 绑定层：当前模式下的 binding 表（源 → 原语 + 参数）           │
  ├─ 原语状态机：每个激活的原语实例                                 │
  └─ ContactAllocator：所有来源共享的触点与 pointerId 分配  ◄──────┘
        │  每帧输出
        ├─► TouchSnapshot   ──► session::MapAndroidTouch（不变）
        ├─► 直通/重映射按键 ──► session::MapAndroidInput（不变）
        └─► SensorSample    ──► （预留，待加速度计能力）
```

| 模块 | 职责 |
| --- | --- |
| hal | 补充滚轮事件与相对鼠标模式开关；继续不暴露 SDL 类型。手柄沿用 SDL GameController 映射库 |
| input | 原语、绑定解析、模式、触点分配与合成；纯逻辑、可确定测试；只依赖 hal 类型，不依赖 session/runtime |
| session | 装载并合并操控配置（数据→`ControlConfig`），提供 guest surface 尺寸、Activity 切换与失焦信号；Android 投影保持不变 |
| frontend | 驱动事件泵与帧节拍，绘制提示叠加层，GUI 编辑器读写用户覆盖；MCP 手势作为一个输入源进入 `ControlMapper` |
| agent | MCP 继续提供 click/swipe；后续可增加按“绑定名”触发的工具，便于 AI 测试 |

依赖方向保持 `frontend/agent/session → input → hal`。`input` 不得读取 guest 状态、
Java 对象或画面像素。

## 4. 操控原语

原语是封闭集合，新增原语需更新本文与 input MODULE。所有坐标为 guest 逻辑坐标
（第 6 节）。

| 原语 | 输入源 | 输出与语义 | 主要参数 |
| --- | --- | --- | --- |
| `direct` | 鼠标键、宿主触摸 | 光标或手指位置即触点，按下、拖动、抬起原样转换 | 生效按键 |
| `button` | 任意按键/手柄键 | 在区域内点按：`tap` 在下一帧抬起；`hold` 跟随源的按住；`repeat` 按帧间隔重复点按 | 区域、模式、重复间隔（帧） |
| `stick` | 四键组、手柄摇杆 | 虚拟摇杆：首帧在原点按下，后续帧移到“原点 + 方向 × 半径”；改变方向只发移动；归零后抬起 | 区域、半径比例、`fixed`/`floating` 原点、`digital`（8 向）/`analog` |
| `look` | 相对鼠标、手柄右摇杆 | 在视角区域内持续拖动；累计位移接近区域边界时抬起并在原点重按 | 区域、灵敏度、反转、重按阈值 |
| `swipe` | 任意按键 | 一次滑动：起点→终点，在指定帧数内完成 | 起点、方向/终点、距离、帧数 |
| `pinch` | 滚轮、修饰键+拖动 | 合成双指，以光标或区域中心为心缩放/旋转 | 中心、步长、帧数 |
| `key` | 任意按键/手柄键 | 发送 Android KeyEvent（如 Esc→`KEYCODE_BACK`） | Android keycode |
| `mode` | 任意按键/手柄键 | 切换或按住期间启用一个模式（第 5 节） | 目标模式、`toggle`/`hold` |
| `tilt` | 四键组、摇杆、鼠标 X | 预留：产生加速度计样本，带死区与回中曲线 | 轴、量程、曲线（待加速度计能力） |

`stick` 的 `floating` 原点：按下时在区域内取“上次原点”或区域中心，适配“手指落在
哪里摇杆就出现在哪里”的游戏。`analog` 时摇杆幅度按源的轴值线性映射，并带死区。

## 5. 绑定与模式

- **输入源命名**：键盘使用物理 scancode 名（如 `key:w`、`key:space`），与键盘布局无关；
  鼠标为 `mouse:left|right|middle|x1|x2`、`wheel:up|down`、`mouse:motion`；手柄使用 SDL
  GameController 标准名（`pad:a`、`pad:left_stick`、`pad:right_trigger`）。四键组写作
  `keys:w,a,s,d`（上、左、下、右）。
- **绑定**：`{ 源, 原语, 区域引用, 参数, 所属模式 }`。同一模式内同一源只能绑定一次，
  配置装载时校验冲突并明确失败。
- **模式**：命名的绑定集合，例如 `default`、`menu`。任一时刻有一个基础模式，`hold`
  模式可临时叠加。切换模式时，被停用绑定持有的触点立即发出抬起。不按游戏状态自动
  切换模式（不读取 guest 状态）；需要时由用户按键切换。
- **按键直通**：未被当前模式绑定的键继续经 `MapAndroidInput` 作为物理键直通给 guest，
  保持现有行为；被绑定的键不再直通，避免重复触发。文本输入能力落地后，输入法激活
  期间所有键直通（第 9 节）。
- **鼠标默认直通**：除非配置显式占用，`mouse:left` 始终是 `direct` 触摸，保证菜单和
  未绑定的界面始终可点，不会把玩家锁死。

## 6. 坐标与区域

- 统一使用 guest 逻辑坐标（session 发布的 surface 宽高），宿主窗口的缩放/留黑边映射
  沿用现有 `MapDisplayPoint`，只有一处实现。
- 区域写作 `{ anchor, x, y, w, h }` 或圆形 `{ anchor, x, y, r }`。`anchor` 取九宫格位置
  （`top_left`…`center`…`bottom_right`），偏移相对锚点计算。老游戏的 HUD 多贴屏幕边缘
  布局，锚点让同一份配置在不同 surface 尺寸下仍然命中。
- 配置装载时把区域解析为绝对矩形；超出 surface 的区域明确失败，不静默裁剪。

## 7. 时序、触点与生命周期

- **帧对齐**：`ControlMapper` 在每个 guest 帧边界汇总本帧输入并输出快照，时间戳取
  统一 Clock；手动步进与 MCP 驱动下结果确定可复现。
- **最短按住**：任何合成触点的按下与抬起至少相隔一帧；同一帧内的按下+抬起顺延到下一帧。
- **触点分配**：`ContactAllocator` 统一管理宿主触摸、鼠标、合成触点和 MCP 手势，
  最多 16 个；触点存续期间 pointerId 稳定，溢出时新请求明确失败并记录，不抢占已有触点。
  这替代当前“本地触摸与 MCP 互斥”的做法。
- **取消**：`input_reset`、窗口失焦、切到后台、Activity 切换或 surface 尺寸变化时，
  所有触点统一发出 cancel，所有原语状态复位；恢复后需重新按键才会再次按下。
- **键盘重复**：宿主键盘的自动重复事件不产生新的按下；`repeat` 由原语自己的帧间隔控制。

## 8. 配置分级与数据格式

| 级别 | 位置 | 内容 | 维护者 |
| --- | --- | --- | --- |
| 内置默认 | `src/input` 代码常量 | 鼠标直通、滚轮→`pinch`（以光标为心）、Esc→`KEYCODE_BACK`、其余键直通 | 项目 |
| 类型模板 | `data/input/templates/<id>.toml` | 按玩法预置的模式、绑定与区域占位，如摇杆动作、跑酷滑动、双摇杆射击、塔防 | 项目 |
| 游戏配置 | `data/profiles/*.profile.toml` 的 `[input]` | 选用模板并填写区域、增删绑定 | 项目/社区 |
| 用户覆盖 | 安装实例目录中的独立操控覆盖文件，不并入 GUI 的 `settings.toml` | 用户改键、调灵敏度 | 玩家（经 GUI 编辑器） |

合并规则：按“模式 + 输入源”覆盖；后一级可以显式解除绑定（`action = "none"`）；
区域按名称覆盖。合并结果在会话启动时固定，运行中修改需重新载入并先取消全部触点。

Profile 中的形态示意（数值仅为示例）：

```toml
[input]
template = "stick_action"            # 引用 data/input/templates/stick_action.toml

[[input.zone]]
name = "move";   anchor = "bottom_left";  x = 40;  y = -40; r = 70
[[input.zone]]
name = "attack"; anchor = "bottom_right"; x = -60; y = -60; r = 50

[[input.binding]]
source = "key:k"; action = "button"; zone = "skill_1"; mode = "default"; press = "hold"
```

模板只声明绑定与区域**名称**，区域位置由游戏配置填写；缺少必填区域时装载明确失败。
现有 `input.profile`（模板 id）字段没有任何 Profile 使用，由新的 `[input]` 结构替代；
schema 与 `tools/validate_title_profiles.py` 同步更新。

## 9. 依赖与预留能力

| 能力 | 依赖 | 说明 |
| --- | --- | --- |
| 滚轮、相对鼠标 | hal 新接口 | P1 前置；SDL3 已支持，需在 `WindowInput` 上暴露 |
| 手柄 | hal 已产生事件 | 只需接入 `ControlMapper` |
| 倾斜 `tilt` | 虚拟加速度计 | 现状发布“无传感器”。改为发布虚拟设备会改变 guest 可见的设备事实，需另立 ADR |
| 文本输入 | 输入法会话 | `dexvm.input_method_sessions` 尚无 InputConnection；落地后定义“输入法激活期间键盘直通” |

## 10. 提示叠加层与编辑器

- 按热键（默认 F1，可改）显示半透明按键提示：在每个区域上标注绑定的键名和当前模式。
- 叠加层由 frontend 在 guest 画面合成之后绘制，不进入 guest 帧，也不进入 MCP
  `frame_capture` 的默认截图（可按参数请求带叠加层的截图）。
- GUI 编辑器（P3）在当前帧截图上拖放区域、绑定按键，写入用户覆盖文件，并可导入/导出
  与他人共享。

## 11. 验证

- `tests/input`：每个原语的帧级输出序列（按下帧、移动、抬起、cancel）精确断言；
  四键组组合方向、`floating` 原点、`look` 重按、触点上限与 cancel 规则。
- 配置：模板/Profile/用户覆盖的合并、冲突与缺区域的明确失败；schema 校验器自测。
- 端到端：在 DexVM 测试 fixture 中增加记录 MotionEvent 的 Activity，经 `ControlMapper` →
  session 投影验证 guest 收到的动作、pointerId 与时间；不依赖具体游戏。
- 游戏实测只作为 Scenario 证据，不作为能力完成的判据。

## 12. 分阶段

| 阶段 | 内容 | 验收边界 |
| --- | --- | --- |
| P0 | `ContactAllocator`（含 MCP 并存）、`direct`/`button`/`stick`/`key`/`mode`、按键直通规则、cancel 规则、`[input]` schema 与模板目录、F1 提示叠加层 | 单元与端到端测试通过；一款摇杆动作类游戏用模板 + 区域数据可完整操控 |
| P1 | hal 滚轮与相对鼠标、`look`/`swipe`/`pinch`、手柄接入 | 3D 视角、跑酷、缩放类各一款验证 |
| P2 | `tilt`（依赖加速度计 ADR）、更多类型模板 | 倾斜类游戏可用键盘/摇杆驾驶 |
| P3 | GUI 编辑器、用户覆盖、导入导出 | 玩家不改文件即可为新游戏配置键位 |

## 13. 未决问题

1. 提示热键与游戏自身 F1 用法冲突时的默认值。
2. 菜单场景下手柄的导航方式（虚拟光标 vs 区域跳转）。
3. 是否允许 Profile 按 Activity 类名声明默认模式（属于数据，但会引入状态依赖）。
