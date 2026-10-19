# 子模块：frontend/library

游戏库领域，编入 ogplay_library 并由 ogplaycore 的 app C++ 外观导出。拥有 APK 检视、图标提取、Profile 外观、原子导入、全局/实例 schema 与继承、ResolveLaunch。
公开头只含 std 与 app 类型；内核 Profile、Manifest、沙盒及文件锁只在实现内使用。

库根 library/.lock：导入、移除、配置写入及运行租约获取持排他锁；读全局/实例设置与运行探测持共享锁。两者有界等待约 2 秒，超时报 busy 并提示重试；List 只非阻塞尝试清理锁，冲突时跳过残留，busy 不得记为条目损坏。配置读取不改写文件，主文件缺失时只读 `.bak`，下次保存重新发布。运行租约在排他库锁内获取实例 .running.lock，保持至 RunGame 返回/异常；移除与单实例检查用同一 OS 锁。锁文件不得主动 unlink。
删除条目保留 sandbox 和外部数据。settings 校验、未知键和预留字段规则为两个 EXE 的唯一来源；ResolveLaunch 产出值，参数格式仅由 FormatGameRunArgs 维护。
验证：ogplay_app_tests 的库、锁与参数主题；真实 APK 有界复跑。

Profile 摘要只含 id 与 quirk，不提供显示名或 required external。显示名称来自
APK；数据目录由实例设置/导入选项提供，目录有效性独立于 Profile。
解释器设置用 `default` 选择通用默认值，`switch/threaded` 为用户显式覆盖。

surface_width/surface_height 默认 800/480，范围 1..4096；supersample 范围 1..4。
三者均经 EffectiveGameSetting 解析游戏 > 全局 > 默认，GUI 编辑同一 schema；不读取 Profile。
类型模板及 input_template 设置已删除，未知旧设置明确失败。
