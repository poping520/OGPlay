# GUI-23 · 运行结果记录与“待排查”分类

状态：一期完成（2026-11-09），macOS 定向测试与真实 APK 复跑通过；OGPlay.app 内点击待用户手测。

范围：让运行报错的游戏能被一次列出，供人和 AI 批量排查。只做自动判定，不含用户标记。
用户标记与排查清单另见 [GUI-24](GUI-24.md)（暂缓）。

实现：

- 游戏进程：库启动（非 preflight）经 `--run-report` 传入条目 `last-run.json`。`frontend::RunGame`
  开局写 `finished=false`，结束时写 ADR-0146 结局、退出码、呈现帧、首个故障（或启动失败原因）及
  退出阶段错误（`exit_error`，结局为 closed/exited 时的 teardown 异常）。写失败只记 warn。
  格式见 `include/ogplay/app/run_report.h`。
- 判定：未完成且条目无运行锁 → `interrupted`（进程中途消失）；`startup_failed/fault/crashed/no_frame`、
  `interrupted` 或带 `exit_error` 计为失败。读不出的报告视为无运行信息，不算条目损坏。
- GUI：`library.list` 条目带 `last_run`；“需检查”改名“待排查”，条件为条目问题或上次运行失败；
  主视觉在失败时于状态胶囊右侧同行显示浅红胶囊：“上次运行…· 时间”与“日志 →”（打开日志目录），
  错误全文只在悬停提示，不增加主视觉高度。
- CLI：`library list [--filter all|attention|launchable]`，条目增加 `running`、`data_missing`、
  `launchable`、`attention`、`log`、`last_run`。CLI 不加载 Profile 目录，Profile 不可用不计入 attention。

## 验证

- `ogplay_app_tests`：`GUI-23*`、CLI library、GUI LaunchPlan、Game run args 共 14 例通过；
  library/GUI/launch 主题 85 例通过。webui `tsc` 与 `vitest apps/gui` 45 例通过。
- 真实 APK（`ogplay-cli run-apk --run-report`，600 帧）：Angry Birds `closed`、退出码 0；
  PVZ `closed` 且 `exit_error` 为 onDestroy 的 `stopService` 未实现；Angry Birds 运行中 SIGKILL 保留
  `finished=false`；非 APK 文件为 `startup_failed`，error 为 “APK has no ZIP end record”。

## 待人工验证

1. 从 OGPlay.app 启动一个会出错的游戏（如 PVZ 关闭后），回到游戏库，“待排查”计数加一，
   主视觉显示“上次运行退出时出错 · 时间”，“日志 →”可打开日志目录。（2026-11-09 用户实测已入“待排查”，
   提示与状态粘连已改为同行分隔的胶囊；独立一行会把按钮挤下，已弃用。）
2. 再次正常运行并关闭后，该游戏移出“待排查”。
