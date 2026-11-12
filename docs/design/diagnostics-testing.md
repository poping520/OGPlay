# 可观测性与测试体系

诊断体系让开发者、Dashboard 和自动化工具读取同一份运行事实，
并在主循环停滞时仍能取得有界证据。
core 提供结构化日志和命中账本，runtime 发布线程与资源快照，
agent 提供查询及排队控制，场景 runner 复用 MCP 工具组织操作与断言。

以“画面停止更新”为例，仅看最后一条日志无法判断是 guest 在等 futex、
Java 在等 monitor，还是图形生产者正在等待帧许可。
本体系把执行、等待与生命周期事实分开采集，再用明确身份关联，
目标是保留可以继续推理的证据，而不是把所有停顿都归类为死锁。

## 日志与两种账本

Logger 保存固定 message 和结构化 fields，变量放在 field 中。
环形缓冲保留近期记录，sink 决定文本或 JSONL 展示，符号化通过 Loader 注入的 provider 完成。
这样机器和界面不需要从一整句文本中反向解析地址与线程，
代价是日志调用处需要稳定字段和明确身份。

[结构化可观测性决定](../adr/diagnostics.md#adr-0006)把这些事实作为一等接口。
CapabilityLedger 是一次运行的未实现命中账本，记录入口、次数和位置；
warning 则是另一通道，UI 展示降级不会伪装成底层能力完成。
一次真实命中说明执行触达，静态表面只说明声明或绑定存在。

capabilities.toml 是跨会话的能力范围、状态和验证入口账本，
运行时 ledger 是单次观测，两者用途不同。
例如一个 native 未被本场景调用，不等于已经实现；
一个类可链接，也不等于其全部行为可以标为 complete。

## 同源快照怎样组合

```text
CPU / DexVM / JNI / VFS / 图形 / 音频
               ↓ 显式事实源与有界快照
         diagnostics 与 agent
            ↙             ↘
       MCP / Dashboard    文件诊断包
```

前端装配来源，下层通过窄接口提供强类型事实。
快照携带来源状态、采样时间与 generation，generation 是识别来源更新代际的标记。

查询失败本身也是结果。一个 section 不可用，可能是未装配来源、锁忙、预算超限或采集异常；
把这些都写成空对象会让客户端误以为“没有资源”。
因此锁忙、未连接或异常用 unavailable/null 表达，截断和不完整集合标 partial，
而不是用全零冒充“采到了空状态”。客户端展示缺口，不反向执行 guest 查询去填补它。

不同 section 是各来源的局部快照，不是跨模块原子事务。
例如音频队列与会话 Clock 可能在相邻采样间变化，
时间接近不能证明因果关系。累计计数差分形成 observed-only 事件，
它说明何时观察到变化，不伪造变化实际发生时间。

诊断读取不消费 GL error、PCM、输入或 guest 队列。
否则打开监控就可能改变首错或让队列前进，复现条件也会随观察动作变化。
保持只读的代价是有些来源只能返回未知，
精确状态边界见 [agent 契约](../../src/agent/MODULE.md)。

## 停滞时为什么使用 try-lock

GuestStallSnapshot 汇总生命周期、执行、等待和最近边界记录。
线程同时保存 guest_tid、host_tid 与 context token，分别表示 Android 线程、宿主线程和执行上下文身份。
ARM PC、Java 栈及路径类型帮助区分当前正在计算、等待还是退出；
退役线程保留有界 tombstone，即简短退出记录，避免关键状态刚结束就消失。

来源使用 atomic、固定环或短 try-lock 复制。
若取证再等待 VM 执行锁，真正持锁停滞时诊断也会一起卡住，
所以繁忙字段保留未知，其余 section 继续输出。
代价是证据可能 partial，不能假设一次快照就包含所有栈与资源。

[进展与停滞决定](../adr/diagnostics.md#adr-0026)采用这种有界采集，
完整宿主栈留给进程外 debugger/dump 工具。
进程内不会为了填满快照执行 guest、触发解码或进行普通资源清理，
因此取证与修复动作可以分开。

## 调查一次无帧停滞

```text
无新帧
  → 会话阶段 / 首错
  → Java 与 ARM 线程位置
  → monitor / futex / pacer 等待事实
  → 必要时外部宿主栈
  → 结合代码建立原因假设
```

首先读取会话是否仍 running、最近源帧和成功 present 是否变化，
再看图形生产者、主 Looper 与 native worker 的执行和等待记录。
如果某来源 busy，就保留未知并继续获取其他 section，
不为了取得完整表格等待可能正是故障来源的锁。
图中的箭头表示调查所需事实的层次，而不是自动诊断算法。

等待环只由明确的 owner 边建立。owner 在这里指已知持有某同步资源的执行者，
不是任意使用过该地址的线程。
monitor entry waiter 正在竞争持有权，Object.wait 的 notify set 则等待通知，
两者分别记录，避免把所有 wait 都连成同一种依赖。

futex 地址本身没有“谁负责唤醒”的信息。
两个线程等待同一地址，或某等待超时，都不足以证明已确认死锁。
只有采集到可信的持有者关系并形成相应环，才有依据发布 confirmed cycle。
又如主线程等待 GL，GL 又等待下一帧许可，需要结合生命周期和 pacer 关系判断。
把未知保留为未知，比构造一条未经证实的 owner 边更有利于后续核实，
所以很多实际停滞仍需结合 guest 代码与外部宿主栈分析。

进展也不是“经过任意 syscall”。分派结果区分未处理、已处理但 idle、真实 advanced，
正字节 IO、驻留等待、swap 或音频 enqueue 等按集中规则分类。
查询或 EOF 不能无条件续 watchdog，退出预算又与运行进展预算独立。
这减少空转循环借频繁查询无限延长执行的机会，精确准入见对应运行契约。

## 故障触发与证据包

进程内 diag.snapshot 是一个入口，外部 CLI 或 OS 触发也可唤醒独立协调器。
协调器不依赖 SDL 主循环确认，关闭时 stop、唤醒并 join。
如果安全快照仍不足，就在进程外抓宿主栈，按 host_tid 与 guest 线程对齐。
命令和平台工具选择见[排障手册](../playbook/TROUBLESHOOTING.md)。

证据包关联 APK 身份、启动选项、结构化状态、原始错误与最近帧。
截图读取已呈现图像，不为了拍照再 step；加网格只修改临时副本。
同一轮采集还应保留故障前的输入和启动条件。如果再 step、重新打开资源或重建窗口，
就已经开始新试验，新的截图不能覆盖旧故障帧。
保留首轮证据而不被复跑覆盖，可以分辨原始失败与后续试验条件。
敏感值、凭据与私有外部绝对路径不进入仓库文档。

关闭开始时间、最近进展和预算也进入快照。
运行首错与清理失败分别保存，避免最后一个析构错误掩盖真实故障。
这是[有界协作停止决定](../adr/diagnostics.md#adr-0025)的取舍：
清理可以失败，但不能为了看似成功吞掉预算超限或改变错误归因。

## 控制协议为什么只确认排队

ControlService 不依赖传输，分派 session、日志、GPU 和账本请求。
JSON-RPC adapter 只处理 envelope 与错误码，MCP adapter 提供工具及 schema。
未知方法、额外字段和重复 JSON 键在边界失败，
控制服务不因协议格式不同而另建 guest 生命周期。

网络 worker 将 step、suspend、resume 和 shutdown 排队给主线程，
返回序号仅说明请求已接收，完成状态从后续同源快照确认。
若把排队当作完成，工具可能在实际 DOWN 前就截图，或在关闭尚未执行时宣布退出。
调用方因此要区分接收确认与执行结果。

click 的 DOWN/UP 跨独立阶段，swipe 保留端点和离散步数。
坐标按最近帧逻辑边界校验，无帧或越界拒绝，重置同时处理待发和已持有触点。
手动模式没有 step 许可时不推进输入、Clock 或呈现计数，
让自动化操作有可解释的时间边界。

FrameSnapshotStore 移动持有最近已呈现 RGBA，编码只在截图请求时执行。
新帧发布可回收旧存储，网格与干净截图都不改原像素。
这样每帧不承担截图编码成本，代价是读取时需要临时副本，
截图取得成功也只证明有图像，内容是否正确仍由断言或人工验收判断。

## Dashboard 的游标与身份

DashboardSources 由 frontend 注入，dash.snapshot 按 section 返回有界结果，
thread 查询用 guest 身份关联；来源锁忙时仍保留 unavailable。
HTTP 层限制 loopback Host/Origin、路径和 body，
启动器核对进程 PID，避免端口复用后展示另一进程状态。

事件通过 sequence 续读，stream identity 改变后清空旧游标。
环形缓冲覆盖历史时返回 gap/dropped，不能为了画完整时间线补出不存在的事件；
差分观察也可能只知道计数变化而不知道真实发生时间。
这样多个客户端可以独立续读，自动化也只对可判定事实断言；
相应地，客户端要处理截断、重建和观测时间，无法仅靠一条连续时间线还原所有并发细节。

Dashboard 的普通查询只读已有事实，不调用 guest、写盘或初始化 decoder。
文件诊断包是显式取证动作，二者用途分开。
更详细的数据边界见 [debug 契约](../../src/runtime/debug/MODULE.md)，
操作流程见 [Dashboard 手册](../playbook/DASHBOARD.md)。

## 测试层次怎样选择证据

格式解析和资源边界用离线夹具，CPU 用解释器/JIT 指令对照，
DexVM 用确定性 DEX 比较返回值、异常、堆与寄存器，JNI 探针检查 ABI、引用和双向调用。
这些检查分别覆盖解析、执行与跨边界风险，
不会因为一个静态 catalog 检查通过就省略真实调用证据。

AOSP Dalvik 是固定语义参考和独立 oracle 来源。
oracle 是用于比较预期行为的独立参照，不把其完整对象模型、线程和 JIT 移植进项目。
普通 Java 算法由 ROM 执行，测试不再用 C++ 重写同一算法当裁判，
以免实现和测试共享同一种错误。

两种 DEX 内核共用语义，相关风险仍需要比较实际输出。
制品身份检查确认输入，静态表面检查确认声明，native/JNI 探针确认执行，
真实游戏场景确认组合路径。每一层都提供有限结论，
精确命令与夹具选择见[开发参考](../playbook/DEV-REFERENCE.md)。

## 从首错复现到正式场景

runner 复用现有 MCP 工具，不新增测试后门。
精确 APK、初始化沙盒和启动选项决定复跑条件，
checkpoint 用帧、fault、像素或结束状态形成机器可判定结果。
watch 模式方便增量编写，正式重放仍采用固定场景。

机器断言比“没报错”更具体，但每种断言只回答自己的问题。
要求已有呈现帧且没有 guest fault 的 checkpoint，证明执行达到有画面的阶段，
不证明图像内容正确；像素或 golden 断言回答具体画面是否符合预期，
退出状态则验证对应关闭路径。正式场景通过时，结果也只覆盖该精确条件，
并发压力或另一尺寸未测，就不能一起标为成功。

首错修复重走相同触发路径，确认原错消失并记录下一首错，称为 reached-fault。
它能说明执行前进了一步，却不证明整款游戏可玩。
正式 title gate/golden 验收才要求三轮场景复跑，
规则与判据见[场景手册](../playbook/SCENARIO-RUNNER.md)及[开发约定](../../AGENTS.md)。

## 验证成本与当前边界

检查按实际风险选择直接相关主题、制品校验或明确失败判据的复现，
纯文档仅做 UTF-8、链接、锚点与差异检查。
相关检查通过且没有新疑点就停止，未验证部分明确保留，
避免用更多无关测试替代缺少的直接证据。

基线失败保留原因，oracle 不为候选结果修改。
当前回归并非全绿，跨进程 OS 触发限时落盘、关闭稳定性与跨平台仍有未验收项，
见[已知问题](../state/KNOWN-ISSUES.md#title-validation)。
本文描述证据如何产生，不把历史局部通过当作当前全量结论。

## 契约、决定与操作

- [core](../../src/core/MODULE.md)、[agent](../../src/agent/MODULE.md)
- [debug](../../src/runtime/debug/MODULE.md)、[session](../../src/session/MODULE.md)
- [诊断决定](../adr/diagnostics.md)、[开发规则](../../AGENTS.md)
- [MCP](../playbook/MCP-TESTING.md)、[场景 runner](../playbook/SCENARIO-RUNNER.md)
- [Dashboard](../playbook/DASHBOARD.md)、[排障](../playbook/TROUBLESHOOTING.md)
- [当前缺口](../state/KNOWN-ISSUES.md)
