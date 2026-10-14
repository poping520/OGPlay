# INP-6 · Web 按键编辑器

状态：完成。依赖：INP-5。设计：[Input 设计](../../design/input/README.md) 第 10 节；
决策：[ADR-0134](../../adr/session.md#adr-0134)。

目标：玩家在冻结的真实游戏画面上完成按键配置，无需手写文件。

范围：
- 前端模块放在 `webui`，沿用现有技术栈与构建（ADR-0072），由 run-apk 的 loopback 服务
  以静态页面提供（与 Dashboard 相同的发布与校验方式）。
- 以 `control.frame` 为背景按 guest 坐标等比显示；显示现有区域与键名。
- 选择类型模板；新建/删除按钮与摇杆；拖动、缩放圆形或矩形区域；摇杆可拖出行程半径并调整
  停留/滑动帧数；按钮选择点按/按住/连发。
- 选中控件后按键绑定：使用 `KeyboardEvent.code`，映射到物理键名；支持鼠标键；冲突当场标出。
- 锚点按区域离屏幕各边的距离自动选择，用户不接触坐标。
- “应用”调用 `control.apply`；“继续”恢复游戏试玩；“放弃”回到上次保存的配置；“重置”
  回到所选模板。

不含：导入导出、社区预设、离线编辑。

验收：前端单元测试覆盖坐标换算、锚点选择、键码映射与冲突检测；页面构建清单受现有
Web UI 校验；在真实会话中用编辑器完成一款摇杆动作类游戏的配置。

实现：`webui/apps/controls`（Preact，经 `vite.controls.config.ts` 构建为 `data/webui/controls`，
manifest 与 Dashboard 同一脚本生成）。`keys.ts` 键码映射与宿主保留键，`model.ts` 坐标换算、
锚点选择、冲突检测与保存层生成（完整布局前置 `none` 解绑），`rpc.ts` 片段令牌与 Bearer 客户端。
frontend `/controls/` 静态页（与 Dashboard 共用白名单预载与 CSP，页面无需令牌，未构建时
404）；run-apk 打印 `/controls/#token=…`。CMake 校验、拷贝、安装加入 controls，新增
`frontend.key_editor_manifest`（Windows 与 Dashboard 同组）。页面关闭时 keepalive 恢复游戏。

验证（2026-10-14，macOS）：`npm run check` 通过（tsc；vitest 42 例，其中编辑器 12 例覆盖坐标、
锚点、键码/保留键、冲突、保存层解绑、令牌与 RPC）；CheckWebUi 校验 controls 制品通过；dev
`INP-*`/MCP/Dashboard HTTP 用例 34 个通过（新增页面路由、白名单、Host、缺制品 404 与不完整
制品启动失败）。Release 原 APK（Dungeon Hunter 1024×600，内置浏览器打开编辑器）：打开即
冻结并显示真实画面与 5 个区域；拖动 primary、改绑 Space、新增按钮绑 H 并缩放、删除 skill_2、
F1 被拒；W 与摇杆冲突时红色标出且不能应用；应用后文件为完整布局并含 J/L 解绑，运行中布局
同步更新；重启后从文件装载相同布局；摇杆行程手柄改为 0.54、方向键逐个绑定且拒绝重复；
“放弃”回到上次应用，“重置”回到模板并保留 Esc→Back；“继续”与离开页面后游戏恢复推进。
实测发现并修复：按键捕获监听依赖渲染后注册导致快速按键丢失、Space 抬起再次触发焦点按钮、
圆形缩放与行程手柄重合。

缺口：Windows WebView/浏览器未实跑；macOS 未自动拷贝 `data/webui/controls` 到构建目录
（与 Dashboard 相同，本次手工拷贝）；未在游戏窗口实按新键位试玩（P0.5 归 INP-7）。
