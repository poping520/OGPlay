# 10 · AI 自动化测试

本篇冻结 M6 的方向：把当前 exact-APK smoke、人工点击、日志观察和截图目测收敛成
AI 与 CI 共用的、确定性、有界、可机器判定的兼容性测试执行层。

当前可直接使用的截图与点击步骤见 [OGPlay MCP 测试使用](../playbook/MCP-TESTING.md)；
该文档描述现状，本篇定义最终自动化出口。

---

## 1. 当前基础与缺口

已有基础：

- Agent Control 已有传输无关分派、JSON-RPC、`session/run/sym/hle/gpu` 基础契约。
- 固定步长 Clock、能力账本、结构化日志、GPU 指标和 ANGLE readback 已存在。
- Software/ANGLE 黄金帧比较已有像素差与感知哈希基础设施。
- Profile 已能按 package、versionCode、`.so` SHA-256 精确选择真实 APK guest 路径。
- `run-apk` 已能启动 loopback MCP，通过同一 guest loop 提供最新帧截图和有界点击。

仍缺少的闭环：

- `agent-stdio` 尚未拥有 `run-apk` 使用的 Profile-backed exact guest session。
- `step/until`、结构化检查点、场景断言和退出状态尚未在同一真实会话中组合。
- exact-APK smoke 仍依赖 CLI 参数、人工操作或自由文本观察，不能作为稳定 CI 断言。
- 失败没有统一的结构化证据包，AI 仍需从长日志中重建首个阻塞。

---

## 2. 设计原则

1. **单一执行路径**：自动化 runner 必须组合现有 Profile session、lifecycle、ANGLE、输入和
   音频接口，不复制第二套 guest loop。
2. **纯数据场景**：标题差异只进入受检场景/Profile 数据；生产代码不得识别游戏名或包名。
3. **全程有界**：open、step、until、input、capture、shutdown 都有 frame/tick/wall-time 上限。
4. **fail closed**：fixture 缺失、身份不匹配、查询未实现、超时、无 frame 或断言缺失都失败。
5. **状态同源**：AI 调试与 CI 断言必须读取同一 Agent provider，不从 stdout 正则提取事实。
6. **证据优先**：失败首先报告最早未满足的动作或断言，并保留足够的复现输入和状态快照。

---

## 3. 场景数据

建议在 `data/scenarios/` 保存不含受版权保护内容的场景 TOML。场景只引用精确 Profile
identity、外部 fixture logical id、动作和断言；APK、OBB、存档及机器绝对路径不得入库。

每个场景至少包含：

- schema 版本、场景 id、适用 Profile identity 与所需资源 logical id；
- 启动上限、逐动作上限和总 wall-time 上限；
- `step/until/tap/swipe/key` 等按逻辑 surface 坐标描述的动作；
- lifecycle、movie request、process exit、GPU/HLE、frame/readback 与音频状态断言；
- 成功 shutdown 条件和需要保存的证据类型。

场景加载必须拒绝未知字段、重复 id、无界等待、越界坐标、空断言和模糊 APK 身份。

---

## 4. 执行层

M6 按以下依赖顺序交付：

1. 场景 schema、严格加载器与 self-test。
2. 结构化 action/assertion/result 模型及稳定 JSON 输出。
3. Profile-backed Agent session，把 exact APK 启动事实接入现有 Control Service。
4. 有界 `run.step/run.until` 与输入注入；推进 guest 使用统一 Clock，不用 `sleep` 判成功。
5. `frame.capture/diff/assert_golden`，保存原图、差分图、阈值和 readback 元数据。
6. GPU/HLE/fs/lifecycle/movie/audio 查询断言与首个失败聚合。
7. 首个 exact-title 场景和连续三次确定性回归。
8. 多场景批处理、趋势汇总和后续远端 CI 接入。

每个条目继续拆成单会话 Work Unit；不以一个 WU 同时实现 schema、runner 和首个题库。

---

## 5. 结果与证据包

一次运行必须产生稳定 schema 的结果，至少包含：

- runner/build 标识、Profile 三重身份、场景 id、后端与 fixture logical id；
- 每个动作的开始/结束 frame、guest ticks、wall time、状态和错误分类；
- 每条断言的期望、实测与证据引用；
- 首个失败、未实现能力命中、null-call、GL error、guest fault 与退出状态；
- 末帧、失败帧、差分图、结构化日志尾部和可选 GPU trace。

证据包不得写入 APK 内容、凭据、测试机地址或宿主绝对 fixture 路径。成功与失败都必须
正常关闭 guest、音频设备、managed surface 和窗口资源；清理失败需要独立记入结果。

---

## 6. M6 出口检查

- [ ] 至少一个 exact APK 场景完成启动→输入→检查点→shutdown。
- [ ] 同一场景连续运行三次，动作顺序、断言结果和检查点 frame 可复现。
- [ ] 主界面或等价稳定检查点由 readback/golden 自动判定，无人工视觉步骤。
- [ ] 人为制造缺 fixture、超时、未实现查询和黄金帧差异时均得到精确失败。
- [ ] AI 可只读取结构化结果定位首个阻塞，不需要解析自由文本日志。
- [ ] runner 的 focused tests、全量 CTest、warnings-as-errors 与目标平台 exact smoke 通过。

M6 不以“能启动脚本”作为出口，也不要求把所有历史标题一次性录入题库。批量兼容性覆盖、
按影响标题数补能力以及公开兼容性数据库属于 M8。
