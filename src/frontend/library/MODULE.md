# 子模块：frontend/library

游戏库领域，编入 ogplay_library 并由 ogplaycore 的 app C++ 外观导出。拥有 APK 检视、图标提取、Profile 外观、原子导入、全局/实例 schema 与继承、ResolveLaunch。
公开头只含 std 与 app 类型；内核 Profile、Manifest、沙盒及文件锁只在实现内使用。

库根 library/.lock：导入、移除、配置写入及运行租约获取持排他锁；读全局/实例设置与运行探测持共享锁。两者有界等待约 2 秒，超时报 busy 并提示重试；List 只非阻塞尝试清理锁，冲突时跳过残留，busy 不得记为条目损坏。配置读取不改写文件，主文件缺失时只读 `.bak`，下次保存重新发布。运行租约在排他库锁内获取实例 .running.lock，保持至 RunGame 返回/异常；移除与单实例检查用同一 OS 锁。锁文件不得主动 unlink。
删除条目保留引用的外部目录，库内 `data/` 随条目删除；sandbox 默认保留，`delete_saves` 时先删除。
`ClearSaves` 在排他库锁内持实例运行锁清空 sandbox 覆盖层（SandboxStore::Clear），保留 meta 身份（id、ANDROID_ID）、库内 APK/数据包、设置与日志；运行中或损坏条目拒绝。
导入优先沿用无库条目且 meta package 一致的残留 sandbox 的最小编号 id（ReusableInstallationId，ADR-0145）。settings 校验、未知键和预留字段规则为两个 EXE 的唯一来源：读取 config/settings/meta 文件时跳过未知键，写入与修改请求仍拒绝未知键；ResolveLaunch 产出值，参数格式仅由 FormatGameRunArgs 维护。
验证：ogplay_app_tests 的库、锁与参数主题；真实 APK 有界复跑。

Profile 摘要只含 id，不提供显示名或 required external。显示名称来自
APK；数据目录由实例设置/导入选项提供，目录有效性独立于 Profile。
APK label 为资源时，默认配置值为 `display_name`，各语言资源（`zh-TW` 形式）存入 meta.toml
`display_name.<locale>`。`LocalizedDisplayName` 按启动器语言依次取完全匹配、仅语言、同语言其他地区、
默认值；运行窗口标题和 GUI（webui `gameName` 同规则）都按此显示。
解释器设置用 `default` 选择通用默认值，`switch/threaded` 为用户显式覆盖。

内置 Demo（`demo_games.h`）：随包数据 `demos/` 下的 *.apk 只放 OGPlay 自己的原创示例游戏；包名与其一致的
条目即该 Demo，不论是自动放入还是手动导入。`SeedBundledDemos` 仅在全局 `show_demo_games`（常规，默认开）
为开时，把库中尚无同包名条目的 Demo 按普通导入（无数据包、/sdcard，沿用残留存档 id）放入库，关时不动游戏库。

screen_width/screen_height（屏幕宽高）默认 1280/720，范围 1..4096；density_dpi 默认 320，范围 72..1280；
supersample 范围 1..4；max_fps（帧率上限）取 30/60/90/120/144/unlimited，默认 unlimited，转为 GameRunOptions.max_fps
（unlimited 为 0）。五者均经 EffectiveGameSetting 解析游戏 > 全局 > 默认，GUI 编辑同一 schema；不读取 Profile。
类型模板及 input_template 设置已删除，未知旧设置明确失败。

数据包配置只有一个来源：meta.toml 只记录导入事实，不含任何数据包字段；external_guest_dir
与引用的 external_dir 只存于 settings.toml，导入时写入选择的值（/sdcard 不写），之后同样经
设置修改。external_guest_dir 默认 /sdcard，不提供全局选项。只接受 /sdcard 或其子目录，拒绝越界、
点段、反斜线与控制字符。ResolveLaunch 仅在启用 external_dir 时转为 --external-guest-dir。
不依赖 Profile，也不移动宿主数据。目录存在性只校验本次修改的值，已引用目录暂时缺失不阻止其他修改。

数据包挂载提示（ADR-0146）：ResolveLaunch 在非 preflight 时传条目 `last-run-missing.json`。
只推断失败运行（end 为 startup_failed/fault/crashed/no_frame）；closed/exited 表示游戏已跑起来，
其缺失视为可选探测，恒为 none。InferDataMount 只比对报告缺失路径与数据目录实际文件（ASCII 折叠）：
`X/r` 中 r 存在则 X 为候选，按解释的不同缺失数、再按 r 深度排序；忽略单条且 r 只有一级的巧合
（r 为 Android 扩展文件 `main|patch.<版本>.<包名>.obb` 时除外）、当前挂载点，以及解释数不超过游戏已从
当前挂载读到的数据包文件数（found 中属于数据包者）的候选。无数据目录时，缺失含扩展文件即为
needs_data（示例与最深共同目录只取扩展文件），否则缺失 ≥3 条才是。SuggestDataMount 只读 schema 2 报告，
`data_mount_hint` 关闭、报告缺失/损坏/旧 schema 或与当前挂载不符时为 none；用户新选目录单独评估。
CLI `library data-hint` 同源。
导入前 GuessDataMount 只看宿主路径：最内层 `Android/data|obb|media`、`sdcard`、`emulated/0`
之下的部分映射到 /sdcard 下，目录名等于包名时映射 Android/data（含 .obb 为 Android/obb），其余为
`/sdcard/<目录名>`；顶层含 Android/ 的 sdcard 副本保持 /sdcard。

数据包导入模式（ADR-0144）：copy（默认）把所选目录复制为条目 `data/`，不写配置，加载时条目
存在 `data/` 目录即解析为该绝对路径并标记 owned（目录被手动删除后视为无数据包）；link 把宿主
绝对路径写入 settings.toml 的 external_dir。复制在库锁外进行：库根 `import-staging/data-N`
由同名 `.lock` 标记存活，导入开始时清理无持有者的残留；逐文件先 hal::TryCloneFile，
不支持时检查剩余空间后流式复制。只接受普通文件和目录，与库根互相包含、符号链接/特殊文件
明确失败。ImportProgress 报告字节并在发布前接受取消（`import_cancelled`）。游戏设置
external_dir 与库内路径不同时即按 link 解析。

图标：入库与导入分析从 application icon 的全部密度配置中选面积最大且可解码的 PNG（跳过 XML、.9.png），
存方形 RGBA PNG，不放大、超过 kLauncherIconMaxSize（256）才缩小。`RefreshIcons`/CLI `library refresh-icons`
在库锁内按当前规则从各条目 game.apk 重提 icon.png，原子替换，读不出的条目保留原图标。
运行中游戏图标（GUI-17）：`ComposeRunningIcon` 把条目图标与随包 OGPlay 卡带合成为方形 RGBA，`badge`（默认，圆形角标）或
`cradle`（卡带插槽）；游戏图标不加底，裁透明边/软投影后放大至多 20% 直到 macOS 圆角内无透明像素。全局 `running_icon`
选择样式；ResolveLaunch 仅在条目有 icon.png 时传 window_icon 与样式。
Windows 合成使用更紧凑的留白（底板占边长约 91%、卡槽宽度占 98%），与裁透明边后的应用 ICO 保持接近的视觉尺寸，角标保持在画布内；macOS 沿用原有 Dock 网格。设置预览与运行图标使用相同的平台参数。
