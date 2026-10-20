# 子模块：frontend/library

游戏库领域，编入 ogplay_library 并由 ogplaycore 的 app C++ 外观导出。拥有 APK 检视、图标提取、Profile 外观、原子导入、全局/实例 schema 与继承、ResolveLaunch。
公开头只含 std 与 app 类型；内核 Profile、Manifest、沙盒及文件锁只在实现内使用。

库根 library/.lock：导入、移除、配置写入及运行租约获取持排他锁；读全局/实例设置与运行探测持共享锁。两者有界等待约 2 秒，超时报 busy 并提示重试；List 只非阻塞尝试清理锁，冲突时跳过残留，busy 不得记为条目损坏。配置读取不改写文件，主文件缺失时只读 `.bak`，下次保存重新发布。运行租约在排他库锁内获取实例 .running.lock，保持至 RunGame 返回/异常；移除与单实例检查用同一 OS 锁。锁文件不得主动 unlink。
删除条目保留引用的外部目录，库内 `data/` 随条目删除；sandbox 默认保留，`delete_saves` 时先删除。
导入优先沿用无库条目且 meta package 一致的残留 sandbox 的最小编号 id（ReusableInstallationId，ADR-0145）。settings 校验、未知键和预留字段规则为两个 EXE 的唯一来源；ResolveLaunch 产出值，参数格式仅由 FormatGameRunArgs 维护。
验证：ogplay_app_tests 的库、锁与参数主题；真实 APK 有界复跑。

Profile 摘要只含 id 与 quirk，不提供显示名或 required external。显示名称来自
APK；数据目录由实例设置/导入选项提供，目录有效性独立于 Profile。
解释器设置用 `default` 选择通用默认值，`switch/threaded` 为用户显式覆盖。

surface_width/surface_height 默认 800/480，范围 1..4096；supersample 范围 1..4。
三者均经 EffectiveGameSetting 解析游戏 > 全局 > 默认，GUI 编辑同一 schema；不读取 Profile。
类型模板及 input_template 设置已删除，未知旧设置明确失败。

external_guest_dir 属于安装实例用户数据：导入 meta.toml 默认 /sdcard，settings.toml
同名项优先于导入值；不提供全局选项。只接受 /sdcard 或其子目录，拒绝越界、点段、
反斜线与控制字符。ResolveLaunch 仅在启用 external_dir 时转为 --external-guest-dir。
旧 meta 缺字段读取默认值；不依赖 Profile，也不移动宿主数据。

数据包挂载提示（ADR-0146）：ResolveLaunch 在非 preflight 时传条目 `last-run-missing.json`。
InferDataMount 只比对报告缺失路径与数据目录实际文件（ASCII 折叠）：`X/r` 中 r 存在则 X 为候选，
按解释的不同缺失数、再按 r 深度排序；单条且 r 只有一级的巧合与当前挂载点忽略。无数据目录且
缺失 ≥3 条时为 needs_data（最深共同目录）。SuggestDataMount 读取报告，`data_mount_hint` 关闭、
报告缺失/损坏或与当前挂载不符时为 none；用户新选目录单独评估。CLI `library data-hint` 同源。

数据包导入模式（ADR-0144）：copy（默认）把所选目录复制为条目 `data/`，meta 只写
`external_data = "library"`，加载时 external_dir 解析为该绝对路径并标记 owned；link 只记录
宿主绝对路径，无该键的旧 meta 均为 link。复制在库锁外进行：库根 `import-staging/data-N`
由同名 `.lock` 标记存活，导入开始时清理无持有者的残留；逐文件先 hal::TryCloneFile，
不支持时检查剩余空间后流式复制。只接受普通文件和目录，与库根互相包含、符号链接/特殊文件
明确失败。ImportProgress 报告字节并在发布前接受取消（`import_cancelled`）。游戏设置
external_dir 与库内路径不同时即按 link 解析。
