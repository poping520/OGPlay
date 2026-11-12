# 可观测性与测试体系

诊断体系让人和工具读取同一份运行事实，并在主循环停滞时仍能取得有界证据。
core 提供日志和账本，runtime 发布线程与资源快照，agent 提供结构化查询和控制，
场景 runner 把输入、检查点和断言组织成可复跑的验收过程。

## 日志与账本

Logger 输出固定 message 和结构化 fields。
变量值放在 field 中，不能靠拼接文本建立隐含协议。
环形缓冲保存近期记录，sink 决定文本或 JSONL 展示。
符号化由 Loader 注入 provider，core 不反向依赖 ELF。

CapabilityLedger 记录未实现命中、次数和调用位置。
warning 与 unimplemented 是不同通道。
展示降级记录不能冒充底层能力已实现。
动态命中说明执行触达，静态表面只说明声明或绑定存在。

capabilities.toml 是能力范围和验证入口账本。
运行时账本是一次会话的观测结果，两者不能互相替代。
状态推进需要相应证据，类型可链接不能自动记为 complete。
退役项按现行规则删除，测试入口保留可执行命令。

## 同源状态

```text
CPU / DexVM / JNI / VFS / 图形 / 音频
               ↓ 显式快照接口
         diagnostics 与 agent
            ↙             ↘
       MCP / Dashboard    文件诊断包
```

前端只装配来源，不通过自由文本日志推导资源状态。
快照携带来源状态、时间和 generation。
锁忙或未连接返回 unavailable/null，而不是伪造全零。
部分集合与截断显式标记 partial。

不同 section 不是跨模块原子事务。
不能把采样时间相近当作确认的因果关系。
累计计数差分形成 observed-only 事件，不伪造其真实发生时间。
诊断读取不能消费 GL error、PCM、输入或 guest 队列。

## 停滞快照

GuestStallSnapshot 聚合生命周期、执行、等待与最近边界记录。
线程记录同时保存 guest_tid、host_tid 与 context token。
Java 栈、ARM PC 和路径类型帮助定位正在等待哪一层。
退役线程保留有界 tombstone，避免关键退出事实立即消失。

所有来源采用 atomic、短 try-lock 或有界复制。
取证线程不能等待 VM 执行锁或主循环确认。
忙碌字段保留未知，不能为了填满表格反向卡死诊断。
快照不执行 guest，不触发绘制、解码或普通资源清理。

等待环只由明确 owner 边建立。
monitor entry waiter 与 Object.wait 的 notify set 不同。
futex 地址相同不证明谁负责唤醒，更不自动证明死锁。
观察到超时也不能替代真实 owner 关系。

关闭阶段有开始、最近进展和预算事实。
首错与清理失败分别保存，后者不能覆盖前者。
有界取证失败报告 partial，而不是宣布系统无问题。

## 触发与证据包

进程内 diag.snapshot 是一个入口，不是唯一入口。
外部 CLI 或 OS 触发可在主循环无响应时请求协调器落盘。
如果连安全快照都无法获得，可用平台 debugger/dump 获取宿主栈。
这些操作的精确命令见排障手册。

证据应保存 APK 身份、启动选项、结构化状态和原始错误。
截图读取最近已呈现帧，不为了截图再 step。
日志、JSON 与图片按同一会话归档，不混用复跑结果覆盖首轮故障。
凭据、私有外部路径和敏感数据不写入仓库文档。

## Agent、JSON-RPC 与 MCP

ControlService 不依赖传输，分派 session、日志、GPU 与账本请求。
JSON-RPC adapter 只处理 envelope 与错误码。
MCP adapter 描述工具和 schema，不承担 guest 生命周期。
未知方法、字段和重复 JSON 键明确失败。

控制请求从 worker 排队给主线程。
返回排队序号不表示 guest 已经完成操作。
step、suspend、resume 与 shutdown 需要读取后续同源状态确认。
手动步进无许可时不能推进输入、Clock 或呈现计数。

pointer gesture 按独立阶段交付。
click 的 down/up 跨 step，swipe 保留起终点与离散步数。
坐标使用最近帧的逻辑边界，越界或无帧时拒绝。
重置清除待处理和已持有触点，不能留下隐藏的按住状态。

FrameSnapshotStore 保存最近呈现的拥有型 RGBA。
截图编码只在请求时进行，坐标网格加在临时副本上。
干净截图与加网格截图不改变原始像素和尺寸。
截图成功不表示画面内容已经验收。

## Dashboard

DashboardSources 由 frontend 显式注入。
dash.snapshot 按 section 返回有界快照，thread 按 guest 身份关联。
events 用 sequence 续读，stream identity 改变后客户端清除旧游标。
覆盖和丢失返回 gap/dropped，不能静默补出历史。

Dashboard 只读，不调用 guest、写盘或初始化 decoder。
HTTP 层限制 loopback Host/Origin、路径和 body 大小。
启动器验证 PID，避免端口复用时连接到错误进程。
不能从 Dashboard 的空字段推断对应资源不存在。

## 测试层次

格式解析与资源边界通过离线夹具验证。
CPU 指令使用解释器/JIT 受检对照。
DexVM 使用确定性 DEX 夹具比较寄存器、堆、异常和返回值。
JNI 探针检查 ABI、引用和跨方向调用。

AOSP Dalvik 是语义资料和独立 oracle 来源。
不整体移植其对象模型、线程或 JIT。
普通 Java 算法由 ROM 执行，不在 C++ 测试中重写一份算法当裁判。
原版算法未改动时不默认要求新增完整语义测试。

双解释后端共享语义，但仍需在相关风险处比较结果。
静态 catalog、制品身份和真实调用分别验证不同问题。
一个声明存在不能替代实际 native/JNI 执行证据。
具体 dexasm 与源码对照操作见开发参考手册。

## 场景与兼容验收

runner 使用现有 MCP 工具执行场景，不另建测试后门。
精确 APK 身份、初始化沙盒和启动参数决定复跑条件。
checkpoint 断言帧、故障、像素或结束状态，失败必须机器可判定。
watch 模式可增量编写操作，正式重放仍使用固定场景。

首错修复优先重走同一触发路径一次。
记录原错消失与下一首错，属于 reached-fault 证据。
正式 title gate/golden 才要求三轮场景复跑。
不能因到达第一帧就声称游戏全关卡或跨平台兼容。

## 验证成本与停止

先选直接受影响的主题、制品检查或有明确判据的复现。
只构建受影响目标，不用全量测试掩盖局部证据缺失。
纯文档只做 UTF-8、链接、锚点与差异检查。
相关检查通过且无新疑点后停止，未验证部分明确记录。

基线差异必须保留失败原因，不能修改 oracle 让候选变绿。
已知失败也不能冒充本次通过。
性能采样说明实际路径与条件，不在设计文档维护测量数据。
过程留在提交说明，现行边界写入相应契约或已知问题。

## 契约、决定与操作

- [core](../../src/core/MODULE.md)、[agent](../../src/agent/MODULE.md)
- [debug](../../src/runtime/debug/MODULE.md)、[session](../../src/session/MODULE.md)
- [诊断决定](../adr/diagnostics.md)、[开发规则](../../AGENTS.md)
- [MCP](../playbook/MCP-TESTING.md)、[场景 runner](../playbook/SCENARIO-RUNNER.md)
- [Dashboard](../playbook/DASHBOARD.md)、[排障](../playbook/TROUBLESHOOTING.md)
- [当前缺口](../state/KNOWN-ISSUES.md)
