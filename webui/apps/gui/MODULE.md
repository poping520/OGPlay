# 模块：Web 启动器

Preact 游戏库与导入向导，只显示宿主返回的事实，经 closed-schema JSON-RPC 操作宿主。
不执行 guest，不判断游戏兼容性。以 installation id 区分实例，不能按包名合并。
主题位于 packages/ui-kit，产物由 Vite 生成，Node 仅构建期使用。
GUI 制品在 manifest 标记目标平台；Windows 与 Android 构建内嵌 SN Pro 字体（中文回落系统字体），macOS 使用系统圆体，宿主打包前校验平台匹配。许可由统一 `data/credits.json` 声明适用平台。

## 目录

| 目录 | 职责 |
| --- | --- |
| 根 | `main.tsx` 启动（闪屏前语言预设、屏蔽右键菜单、按布局挂载）；`app.tsx` 游戏机布局外壳；`kit.tsx` 启动器共享片段 |
| `shared/` | RPC、界面语言、本地偏好缓存（[MODULE](shared/MODULE.md)） |
| `library/` | 两种布局共用的启动器状态（`launcher.ts`），游戏库主界面及其菜单、弹窗、闪屏（[MODULE](library/MODULE.md)） |
| `phone/` | 手机竖屏布局（[MODULE](phone/MODULE.md)） |
| `import/` | 导入向导、数据包挂载与运行后数据建议（[MODULE](import/MODULE.md)） |
| `settings/` | 全局与每实例设置页（[MODULE](settings/MODULE.md)） |
| `cartridge/` | CSS/3D 卡带、卡带展示及独立的 cart3d 包（[MODULE](cartridge/MODULE.md)） |
| `assets/` | logo 与卡带模型、AO 贴图 |

功能代码、模型、测试与样式放在所属目录；两个及以上功能用到的放 `shared/`（逻辑）或 ui-kit（无资源的样式与组件）。

## 平台能力

`shared/features.ts` 在首帧前读取 `host.features`；宿主未提供的能力（按键设置、Dashboard、打开目录、拖放、预检/诊断等）
不渲染入口、按键提示与说明，只属于这些能力的设置分组（输入、开发者工具、高级）一并隐藏。字段裁剪由宿主完成。
Android 宿主发出可取消的 `ogplay-back`：依次关闭最上层对话框、全屏视图、菜单或筛选，均无则不取消，由宿主退出。

## 布局

两种布局，按构建目标选定，各自的产物只含自己一端：桌面构建（入口 `main.tsx`）为游戏机布局（`app.tsx`，键盘/手柄提示），
Android 构建（入口 `phone/main.tsx`）为手机布局（`phone/`，竖屏触屏，平板也用）。两个入口共用 `boot.tsx` 启动流程；共用代码按
构建常量 `__OGPLAY_PHONE__` 分支（设置页框架、选项控件、卡带展示），构建时删去另一端；手机构建中 `has()` 恒为假，桌面独有能力
（Dashboard、按键设置等）的代码随之删去。样式同样分开：`library/console.css` 只由 `app.tsx` 引入，`phone/phone.css` 只由手机端引入，
`library.css` 只放两端共用的部分。i18n 英文词表仍是两端共用的一份。两者的游戏库状态与操作
（列表、筛选、设置与皮肤、3D 卡带、运行实例、启动/删除/清空、导入与数据提示、错误队列）都来自 `library/launcher.ts` 的
`useLauncher()`，布局只决定怎么显示；弹窗、设置控件与行、卡带组件也共用，手机只换排布与样式。

## 启动器外壳

`app.tsx` 持有游戏库、运行实例与设置状态，渲染主界面；全局设置、每实例设置与卡带展示打开时整窗交给对应视图，
全屏视图打开时主界面不响应按键。键盘对应手柄提示：←/→ 选择、Enter 主操作、Esc 返回、X 按键设置、Y 游戏设置、
V 查看卡带、Q/E 切换分类、⌘K 或 / 搜索、⌘O 导入（Windows 用 Ctrl）。
启动器页面屏蔽 WebView 自带右键菜单（重新载入等），仅文本输入框与已选文字保留（剪切/拷贝/粘贴）。
运行实例/选中实例可打开宿主核对 PID 后就绪的独立 Dashboard 窗口，以及就绪后的“按键设置”窗口（`controls.open`，ADR-0134）。
退出日志提示与导入目录预填读取全局值。
全局 `settings_style`（界面风格）对主界面、其弹窗/菜单、闪屏与设置页统一生效：`console`“磨砂黑”或 `glass`“玻璃紫”，
视图根加 `skin-*` 类；玻璃紫不用图标模糊图，背景为 ui-kit 共享天空（选中游戏外壳色点染）。主题/密度在新界面中为预留项。

## 代码复用原则

GUI 各视图必须复用同一套令牌、控件与组件，禁止复制后改写：
- 颜色、字体、颗粒纹理等令牌只在 ui-kit `:root` 定义；页面只引用 `var(--*)`，取值不同时在自身作用域覆盖令牌
  （如设置页 `--line`/`--panel`、玻璃紫皮肤），不复制整组令牌。
- 两个及以上视图使用的控件（`.pill`、`.seg`、`.key`、`.led`、`.kicker`、`.brand`、`.back`/`.crumb`）只在 ui-kit 定义一次，
  页面样式只补尺寸、间距等上下文差异。页面专用控件留在本页样式；第二个视图要用时先移入 ui-kit 再复用。
- 无资源依赖的组件（线性图标 `Glyph`（禁用字体符号作图标）、键位徽标 `Key`）在 ui-kit；依赖 logo/多语言的启动器片段在 `kit.tsx`
  （`Brand`、`Back`、皮肤 `skinOf`，并负责最先加载 ui-kit 主题，`app.tsx` 首个导入它）；背景 `Backdrop` 在 cartridge/cartridge.tsx；
  库条目对话框共用 library/game-dialogs.tsx 的游戏卡与弹窗逻辑，以及 library/library.css 中 `.import-wizard` 的头部、卡片、字段和按钮样式；
  不在多个文件里各写同名组件。
- 新增样式或组件前先搜索现有类名和组件。纯样式重构须外观不变，以重构前后计算样式对比或截图为证。

验证：npm run check，宿主 GUI CTest，以及 Windows 原生窗口交互。
