# 子模块：frontend/cli

ogplay-cli 拥有子命令语法、usage、帮助、error [ogplay] 错误块、退出码与 JSON 输出。命令实现编入 ogplay_cli_app，系统入口只链接此库与 ogplaycore。
只通过 ogplay::app 公开值类型/Pimpl 外观调用运行、诊断及游戏库服务，不包含或链接内核静态库。
顶层命令为 run-apk、library、dexvm、diag；不提供 agent 控制入口，运行中游戏的自动化控制走 `run-apk --mcp`。
`--help` 输出全部帮助；`<command> --help`（`--` 之前）只输出该命令的帮助并退出 0。
结构化结果默认输出单行 JSON（机器格式优先）；library 与 diag snapshot 的 `--human`/`-h` 改为两空格缩进的同一文档。
diag snapshot 只确认请求已投递，输出 pid、trigger、diag_dir、requested；快照由游戏进程异步写出。
library list/show/inspect/import/remove/settings/run 失败 stderr 明确错误且非零退出。import 同包要求 --new-instance，remove 要求 --yes 并保留存档。
show、settings、data-hint、run 接受 installation_id 或包名：先精确匹配 installation_id，否则包名须恰好匹配一个实例，多实例报歧义；
remove 删除数据，只接受 installation_id。
list 可选 `--filter all|attention|launchable`：attention = 条目损坏/缺数据包或上次运行失败，launchable = 无条目问题且未运行；
不加载 Profile 目录。list 每项只给 installation_id、package、display_name、version_name、status
（ready|running|damaged|data_missing|last_run_failed，按此优先级取第一项）；show <installation-id|package>
给单个条目全部字段：另含 directory、damaged/damage_reason、localized_names、version_code、running、data_missing、
launchable、attention、status、log 及与 GUI 同源的 last_run。
settings set 经库内原子 patch 合并/撤销覆盖，校验和预留字段语义与 GUI 共用。library run 当前进程内 ResolveLaunch → RunGame，状态行写 stderr、结果 JSON 写 stdout，运行锁由共享服务持有。
验证：ogplay_app_tests 的 cli_library_tests、frontend.app_entrypoints、frontend.run_apk_options 与真实 APK 有界复跑。

dexvm dump-surface [--framework <dir>] 输出单份 JSON；缺省使用 bundled 启动类路径，显式目录
不依赖旁置 data。只经 app 外观调用，参数/归档失败非零退出；链接缺口作为 JSON 诊断保留。
dexvm run-dex <dex|jar> 按 dalvikvm 语义运行独立 main：guest System.out/err 原样写 stdout，
退出码为 main 返回 0、未捕获 1、System.exit(n)；VM/装配错误以 error 块和 70 区分。

run-dex 可选 `--native-lib-dir <dir>` 提供 ARM 应用库目录；未指定时不提供应用库。
可重复传入 `--vfs-file <host-file> <guest-path>`，将指定文件只读注入 VFS。相对 guest 路径
从 guest 进程初始 cwd 解析。

顶层失败统一经 app 的异常字符串外观输出；已知非 std 运行时异常保留类型/消息，
标准异常错误块与退出1保持。未知异常继续明确报告 unknown CLI failure。
