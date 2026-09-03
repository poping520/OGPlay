# 04 · 运行时集成

## 1. 入口：双击可执行为主，子命令为辅

产品入口是**可双击启动**的独立 GUI 可执行 `ogplay-gui`，用户全程不接触
终端；`ogplay gui` 子命令保留为开发/测试入口。两者是同一个
`frontend/gui` shell 入口函数的两层薄包装，无独立逻辑：

```
ogplay-gui                                  # 双击/图形环境启动，零参数
ogplay gui [--library-root <dir>] [--smoke-frames <n>]   # 开发与 CI
```

按平台的双击形态（全部为 CMake 目标属性/打包配置，不产生平台分支代码）：

| 平台 | 形态 |
| --- | --- |
| Windows | `ogplay-gui.exe` 以 `WIN32` 子系统链接（`wWinMain` 薄包装转调同一入口），双击不弹控制台窗口 |
| macOS | `MACOSX_BUNDLE` 打出 `OGPlay.app`（Info.plist 最小集）；`ogplay-cli` CLI 二进制一并放入 `Contents/MacOS/` 与 GUI 并排，供子进程 spawn（避免大小写不敏感文件系统上的 `OGPlay`/`ogplay` 冲突） |
| Linux | 普通可执行，文件管理器双击即可；`.desktop` 条目与图标属发行打包项，不在本方案 |

约束与细节：

- `ogplay-gui` 不接受任何参数（双击场景没有参数）；带参数需求一律走
  `ogplay gui`。`--library-root` 覆盖默认平台数据目录
  （[02 §4](02-architecture.md)），`--smoke-frames <n>` 渲染 n 帧后正常
  退出供 CI 有界冒烟（[05 §1](05-verification.md)），参数解析遵循
  `run-apk` 现行风格（重复出现、缺值、尾随字符均明确失败）。
- **无控制台的日志去向**：GUI 进程自身的结构化日志写
  `<library-root>/gui.log`（覆盖写，启动即建）；双击启动时 stderr 无人
  接收，不得作为 GUI 日志的唯一落点。`ogplay gui` 开发入口同时输出
  stderr 与 gui.log，两处同源。
- 未签名 `OGPlay.app` 首次双击受 Gatekeeper 拦截属分发问题
  （[05 §4 风险](05-verification.md)），不影响本地构建产物双击。
- `ogplay` usage 字符串同步登记 `gui` 子命令。

## 2. 启动参数装配（LaunchPlan）

模型层把库条目 + `config.toml` 装配为子进程 argv，规则固定且可契约测试：

| argv 片段 | 来源 | 规则 |
| --- | --- | --- |
| `<ogplay> run-apk` | `SDL_GetBasePath` 同目录下的 `ogplay` CLI 二进制 | 不查 PATH；同目录缺失该二进制 = 安装损坏，启动前校验明确失败 |
| `<apk>` | `library/<pkg>/game.apk` | 绝对路径 |
| `--profiles-dir <dir>` | `config.toml: profiles_dir` | 仅在非空时传递 |
| `--external-dir <dir>` | `meta.toml: external_dir` | 仅在条目声明时传递 |

Android guest 系统库不进入 LaunchPlan；`run-apk` 根据匹配 Profile 的 API 从随程序交付
的 bundled data 自动选择，缺少对应发行集时明确失败。

不传递任何其他旗标：超采样、MCP、preflight、survey 等均为 CLI/自动化
面，主面板不暴露（[01 §3 非目标](01-scope.md)）。装配函数是纯函数
（条目 + 配置 → argv 向量），CTest 逐字段断言。

子进程管理用 SDL3 `SDL_CreateProcess`：stdin 关闭、stdout 继承、stderr
重定向至 `last-run.log`（覆盖写）。主循环每帧非阻塞查询存活状态与退出
码，无独立等待线程。主面板退出时若有运行中的子进程，提示用户确认；
确认退出**不杀死**游戏子进程（孤儿化继续运行），主面板不负责游戏的
生命周期管理。

## 3. loader 增量：application icon/label

现状：`loader.apk_manifest_identity` 只产出 package/version/SDK 事实与
launcher activity；`<application>` 的 `android:icon`、`android:label`
两个 attribute 未读出。

增量（一个小 WU，复用既有严格解析器）：

- `loader::AndroidManifestFacts` 新增 `application_icon`、
  `application_label` 两个可选事实；icon 恒为 resource id，label 为
  resource id 或字面量字符串二选一（typed value 区分）。
- 解析失败语义不变：结构损坏即失败；attribute 缺失是合法事实（回退链
  处理），不是错误。
- 之后的 resid → 文件路径/字符串解析完全复用 `loader.arsc` 已有双向
  查询，不扩展 arsc 能力（复杂值/多 locale 仍明确不支持，走占位回退）。

## 4. Profile 事实消费

导入摘要页需要两个事实，全部来自既有 session 面、只读消费：

1. **精确匹配结果**：复用 `profiles.apk_exact_identity_match` 的唯一
   候选发布（同 `run-apk` 路径）；主面板不实现第二套匹配。
2. **是否声明必需 external mount**：读匹配 Profile 的 external 声明
   （`frontend.run_apk_external` 所依赖的同一事实）。Profile 格式正处
   v2-only 迁移（ADR-0022），主面板只经 session 公共 API 取"是否需要
   数据包"这一布尔事实，不触碰 TOML 细节，迁移对本方案透明。

## 5. 契约修订清单

实施时须同步的契约（对齐"契约与代码冲突时以契约为准"）：

| 契约 | 修订 |
| --- | --- |
| `src/frontend/MODULE.md` | GUI 从"未来"转为在编；补 `ogplay-gui` 双击入口与 gui 子命令、模型/视图分层、子进程启动、库存储不变量 |
| `src/frontend/gui/MODULE.md` | 新建（职责/公共 API/不变量/禁止/测试） |
| `src/loader/MODULE.md` | manifest application icon/label 事实增量 |
| `docs/modules/INDEX.md` | 登记 `frontend/gui` |
| `capabilities.toml` | 新增 `frontend.gui_shell`（窗口壳 + 冒烟）、`frontend.gui_library`（库存储/导入/删除）、`frontend.gui_launch_spawn`（argv 装配 + 子进程）、`loader.apk_application_visuals`（icon/label 事实） |
| `THIRD_PARTY_NOTICES.md` | Dear ImGui 从"规划中"转入组件表（版本、来源、MIT） |
| `README.md` / usage | `ogplay-gui` 双击入口与 `ogplay gui` 子命令；构建产物说明（macOS `OGPlay.app`） |

既有规则的对齐核对：

- `src/` 零游戏名：库数据全部在用户目录，代码只有 package name 变量。
- 结构化日志：gui 生产代码不裸 `printf`；用户可见输出经 UI 呈现。
- 视图层按页面职责拆分，避免无关页面共享可变状态。
- 依赖方向：`frontend/gui` → session/loader/core 公共 API，无反向引用；
  ImGui 只出现在 `frontend/gui/view` 与 shell，模型层禁止 include。
