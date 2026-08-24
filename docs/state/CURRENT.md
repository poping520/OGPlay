# 当前状态

更新：2026-08-25 · DVM-88 NetworkRuntime、VFS SQLite 与 API 19 能力栈收口

## 当前阶段

- **EGL/GLES API19**：BND-16..24 已冻结 KTU84P ABI，完成 EGL 13 项、GLES1 145 core、
  GLES2 142 core concrete handler 与 PVZ exact ELF 导入复验。关闭 survey 的 bounded run 已
  越过 native load/JNI_OnLoad/OpenGL；该闭包不等于 PVZ 可玩验收。
- **Native Boundary**：BND-1..15 已闭合 metadata catalog、dense transport、typed A32 ABI、
  liblog、AOSP OpenSL ES 与共享 EGL/GLES/PCM 状态；callback/TLS/re-enqueue 有机器门禁。
- **APK Startup**：APS-1..9 已闭合 Manifest/ABI facts、rootless `AndroidGuestProcess`、
  process-lifetime loader/JNI_OnLoad、Application root 与 Profile legacy adapter。A5 exact
  Scenario 三轮确定、无 fault 且 clean shutdown；app ELF 只由 Java `System.load*` 追加。
- **M9 DexVM**：DVM-1..46、48..69 已交付；解释仍由 `VmExecutionLock` 串行。GC-B 已闭合
  全根、精确非移动 STW mark-sweep、复用和水位触发；线程/monitor、FastCode/threaded 双后端、
  类型化 intrinsic、诊断、稳定 identity、ClassLoader 与 bounded reflection foundation 已交付。
  DVM-47 仍受 A6 DT_SONAME identity 与 DH step 漂移阻断，`dexvm.gc` 和
  `dexvm.interpreter_threaded` 保持 `partial`，threaded 生产默认关闭。
- **API 19 能力栈**：DVM-78/79 发布统一集合与 I/O/ZIP runtime；DVM-80 收敛 core/Android
  intrinsic family TU 与 ownership；DVM-81 恢复 Context 类型链；DVM-82 以 `NioRuntime`
  统一 Java/JNI direct buffer；DVM-83 发布 bounded Java GLES façade；DVM-84 让 AudioTrack、
  legacy Java/JNI 与 OpenSL ES 共用唯一 PCM mixer；DVM-85 统一 Clock/Looper/Handler/Timer/
  AsyncTask 调度；DVM-86 发布值类、Parcel/Bundle 与有界系统服务；DVM-87 发布 util algorithm、
  regex、Future/串行 executor/atomic bounded core。
- **DVM-88**：新增 per-VM `NetworkRuntime`，Socket/datagram/TLS 只消费显式
  `NetworkPolicy`/`NetworkTransport`，默认离线且 host/TLS/datagram 分别受检；不直接访问
  宿主 socket、DNS 或证书库。ContentValues/Cursor/SQLiteDatabase/SQLiteOpenHelper 发布
  create/drop/insert/update/delete/query bounded 数据面，确定性内部格式只经 guest VFS
  持久化；完整 SQLite 文件格式、SQL 长尾、ContentProvider/Binder 均 deferred。阶段 catalog
  fixture 同时覆盖 NIO、GLES20、AudioTrack、Socket 与 SQLiteDatabase 链接闭包。

## 验证基线

- `cmake --preset windows-msvc`：通过。
- `cmake --build --preset windows-msvc --config Debug`：全部目标通过。
- DVM-88 聚焦测试 3/3、受影响 GC/catalog 固定清单 4/4、architecture 6/6：通过。
- 全量 `ctest --preset windows-msvc --output-on-failure`：984/985 通过；唯一失败为既有
  `Android liblog text and A32 variadic calls enter structured guest logs`，现象为 guest tag
  `PVZ` 被读为空。该用例在 DVM-79 历史全量基线中已独立失败，与 DVM-88 网络/数据库路径
  无依赖；本 WU 未混入无关修复。DVM-88 及全部 DexVM/architecture 测试通过。

## 下一步

1. 独立排查 Windows liblog guest tag 读取失败，恢复全量绿线。
2. 通用闭合 A6 DT_SONAME identity 与 DH 当前启动阻断，复验 DVM-47 和 threaded title gate。
3. 执行 Linux M9 严格出口复验；后续 framework 长尾仅按关闭 survey 的 reached gap 排序。

## 阻塞与边界

- A6 主界面/可玩 gate 尚未完成；DVM-47 的 A6/DH exact 与长运行门禁未全部成立。
- `dexvm.api19_capability_stack=complete` 仅表示设计文档定义的 bounded 阶段闭包，不表示完整
  Android framework、真实联网、完整 SQLite 或任何 title 的可玩性。
- 未实现能力继续记账并明确失败；长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) ·
[DexVM](../tasks/dexvm/README.md) · [Layout UI](../tasks/layoutui/README.md)；
操作手册见 [docs/playbook](../playbook/README.md)。
