# 02 · 核心架构

## 1. 进程模型：主面板与游戏分进程

**决定**：点击启动 = 主面板 spawn 一个运行 `run-apk` 的子进程（复用自身
可执行文件），等待其退出并回收退出码；游戏不在主面板进程内运行。

| 依据 | 说明 |
| --- | --- |
| 崩溃隔离 | 兼容层的常态是游戏会崩；guest fault 只终结子进程，库视图与其他游戏不受影响 |
| 复用受检路径 | `run-apk` 的 Profile 匹配、quirk 交叉验证、external 挂载、窗口/输入/音频全部原样复用，GUI 零第二套运行时（对齐 roadmap 07"自动化 runner 不得复制第二套运行时"的同一原则） |
| 架构红线天然满足 | "GUI 不得拥有内核里没有的行为"退化为进程边界事实：主面板只会装配 argv |
| 帧循环所有权 | `run-apk` 的 `gl_surface_view` 帧循环独占窗口与 guest 主线程；进程内共存需要重构该循环，收益不成比例 |

被否决的替代：**进程内启动**（把 guest 会话嵌入主面板帧循环）。除帧循环
重构外，它还使"一个 guest 线程对应一个宿主线程"的线程模型与 ImGui 主循环
耦合，且游戏崩溃即杀死启动器。未来 M7 的游戏内叠加 UI（映射编辑器）本就
必须画在游戏 surface 上，属 `run-apk` 进程侧的独立工作，不构成进程内启动
的理由。

并发规则：同一 package 同时最多一个运行实例（磁贴运行中再点只提示）；
不同 package 并行启动不禁止，子进程模型天然支持。

## 2. 模块布局

```
src/frontend/
  cli/            既有；新增 gui 子命令入口（薄转发，开发/测试用）
  gui/            新增，自带 MODULE.md
    main.*          ogplay-gui 可执行入口（双击启动；Windows WIN32 子系统）
    shell.*         SDL3 窗口 + ANGLE EGL context + ImGui 帧循环
    model/          模型层：LibraryStore、ImportPlan、LaunchPlan、GuiConfig
    view/           视图层：库网格、导入向导、设置页、确认对话框
```

`ogplay-gui` 与 `ogplay gui` 是同一 shell 入口的两层薄包装
（[04 §1](04-integration.md)）；前者是用户的双击入口，后者服务开发与
CI 冒烟。

分层规则（可测试性的根）：

- **模型层不 include ImGui**，不触碰窗口；全部行为由 CTest 直接驱动。
- **视图层不持有业务状态**：每帧从模型层读事实、把用户动作转成模型层
  调用；视图层代码不出现文件 IO 与进程管理。
- 结构化日志复用 core Logger；用户可读的失败文案由视图层从模型层的
  结构化错误（错误码 + 参数）渲染，不解析自由文本。

## 3. 依赖与渲染栈

- **Dear ImGui**：`third_party/imgui` 固定提交 submodule（MIT），编译
  core + `imgui_impl_sdl3` + `imgui_impl_opengl3`（`IMGUI_IMPL_OPENGL_ES2`）。
- **渲染**：与游戏窗口同一条栈——SDL3 窗口 + ANGLE EGL（`angle-prebuilt`）
  + GLES2；不引入第二条 GL 初始化路径，hal 既有 EGL 装配复用。
- **文件/目录对话框**：SDL3 `SDL_ShowOpenFileDialog` / `SDL_ShowOpenFolderDialog`
  （异步回调，回调只投递事件到主循环）；零新增依赖。
- **子进程**：SDL3 `SDL_CreateProcess` 家族，stderr 重定向到库条目日志
  文件；零平台分支代码。
- **PNG**：图标解码复用已入库的 stb（`third_party/stb`）。

## 4. 游戏库存储

默认库根为平台数据目录（macOS `~/Library/Application Support/OGPlay`、
Windows `%APPDATA%/OGPlay`、Linux `$XDG_DATA_HOME/OGPlay`，回退
`~/.local/share/OGPlay`），可用 `ogplay gui --library-root <dir>` 覆盖
（测试与自动化用）。布局：

```
<library-root>/
  config.toml                 # GuiConfig：可选 profiles_dir
  library/
    <package-name>/
      game.apk                # 导入时复制入库的 APK 副本
      meta.toml               # 条目元数据（见下）
      icon.png                # 图标提取缓存；缺失即用占位图标
      last-run.log            # 最近一次子进程 stderr（覆盖写）
      sandbox/                # 预留：ADR-0020 每游戏沙盒同根落位，本方案不实现
```

`meta.toml` 字段（全部为导入时一次性固化的事实 + 少量可变引用）：

```toml
schema = 1
package = "com.example.game"          # 目录名与此字段必须一致，不一致即条目损坏
display_name = "…"                    # 提取链结果；不可解析时等于 package
version_code = 42
version_name = "1.2.3"
imported_at = "2026-08-12T12:00:00Z"  # 仅展示用途，不参与任何判定
profile_id = "…"                       # 精确匹配的 Profile 标识；未匹配时缺省
external_dir = "/abs/path"            # 可选：用户指认的数据包目录（原地引用）
```

关键决定：

1. **键为 package name**，与 ADR-0020 沙盒键一致；同 package 再导入明确
   失败并提示先删除（非目标：多版本共存）。
2. **APK 复制入库**：APK 体积小（老游戏普遍 <50 MB），复制换得自包含的
   删除语义与"源文件被移动后游戏仍可玩"。
3. **数据包原地引用不复制**：对齐 roadmap 06 §1.2"不复制大文件"。
   `external_dir` 记绝对路径；启动前校验存在性，缺失即明确失败并给出
   记录的路径。删除游戏绝不触碰该目录。
4. **匹配结果缓存但不作准**：`profile_id` 只用于磁贴状态展示；每次启动
   由 `run-apk` 按当时的 Profile 目录重新匹配，主面板不复制匹配逻辑、
   不因缓存过期伪造可启动。
5. 条目校验 fail closed：`meta.toml` 缺失/损坏/包名不一致的目录在库视图
   中显示为"条目损坏"磁贴（只可删除），不静默跳过。

## 5. 图标与名称提取链

导入时执行一次，结果缓存为 `icon.png` 与 `meta.toml.display_name`：

```
binary Manifest <application android:icon android:label>   ← loader 增量（04 §3）
        │ resid                    │ resid 或字面量
        ▼                          ▼
resources.arsc 解析（loader.arsc，已完备）
        │ res/… 文件路径            │ string pool 字符串
        ▼                          ▼
统一 APK 条目读取（loader.apk_stored_entry，已完备）
        │ PNG 字节
        ▼
stb_image 解码 → 缩放为固定尺寸 RGBA → stb_image_write 存 icon.png
```

回退规则（每级失败记结构化日志，不中断导入）：

- 图标：resid 缺失 / arsc 为复杂值或多 locale（`loader.arsc` 明确不支持）/
  路径非 PNG（如 .9.png、xml drawable）/ 解码失败 → **占位图标**。
- 名称：label 为字面量直接用；为 resid 且 string pool 可解析则用默认
  配置字符串；否则 → **package name**。

不引入新的图片格式支持、不做多 DPI 选择：老游戏图标以 PNG 为绝对主流，
非 PNG 一律占位，属可接受折衷，后续按真实命中再议。

## 6. 字体

磁贴名称含 CJK。方案：启动时按每平台固定候选清单加载一个宿主系统字体
（macOS：PingFang/Hiragino；Windows：`msyh.ttc`/`simhei.ttf`；Linux：
Noto Sans CJK 常见安装路径），合并 ImGui 默认 ASCII 字体与 CJK 常用区
glyph range。全部候选缺失时回退纯 ASCII 字体，非 ASCII 字符按 ImGui 默认
以 `?` 呈现，并记一条结构化警告——不因字体阻塞可用性。不随仓库分发字体
（体积与许可证代价，基础版不值）。
