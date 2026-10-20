# 子模块：frontend/cli

ogplay-cli 拥有子命令语法、usage、error [ogplay] 错误块、退出码、JSON 输出及 stdin/stdout JSON-RPC 循环。命令实现编入 ogplay_cli_app，系统入口只链接此库与 ogplaycore。
只通过 ogplay::app 公开值类型/Pimpl 外观调用运行、agent、诊断及游戏库服务，不包含或链接内核静态库。
library list/inspect/import/remove/settings/run 输出单行 JSON；失败 stderr 明确错误且非零退出。import 同包要求 --new-instance，remove 要求 --yes 并保留存档。
settings set 经库内原子 patch 合并/撤销覆盖，校验和预留字段语义与 GUI 共用。library run 当前进程内 ResolveLaunch → RunGame，状态行写 stderr、结果 JSON 写 stdout，运行锁由共享服务持有。
验证：ogplay_app_tests 的 cli_library_tests、frontend.app_entrypoints、frontend.run_apk_options 与真实 APK 有界复跑。

dexvm dump-surface [--bootdex <jar>] 输出单份 JSON；缺省使用 bundled BootDex，显式归档
不依赖旁置 data。只经 app 外观调用，参数/归档失败非零退出；链接缺口作为 JSON 诊断保留。
