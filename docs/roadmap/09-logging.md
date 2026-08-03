# 09 · 日志与诊断系统

排查问题的效率上限，由能拿到的信息质量决定。这一篇定义 OGPlay 的日志体系。

---

## 1. 现状：没有日志系统

当前代码里**没有任何日志抽象**——没有 Logger、没有级别、没有分类，
只有 **275 处**散落的裸输出：

| 文件 | `std::cerr` / `std::cout` / `printf` 调用点 |
| --- | --- |
| `run_main.cpp` | 150 |
| `gl_bridge.cpp` | 42 |
| `dispatcher.cpp` | 38 |
| `open_sl_bridge.cpp` | 14 |
| 其余 5 个文件 | 31 |

由此产生的实际痛点：

- **开关只能靠环境变量**，已经累积 80 个，且大多是一次性的
- **输出无结构**，AI 与脚本只能用正则去刮，格式一动就失效
- **没有帧号**，看到一条 warning 不知道它发生在第几帧、和画面上哪一现象对应
- **没有限流**，开了 GL trace 就是几十万行，淹没真正的信息
- **地址不可读**，`lr=0x107149f8` 要人工去 IDA 里查
- **崩溃时什么都没有**，只能加日志重跑一次（一次 90–160 秒）

---

## 2. 设计目标

1. **结构化优先**：日志内部是记录（record），文本只是它的一种渲染。
2. **帧号是第一时间轴**：这类项目里"第几帧"比"几点几分"有用得多。
3. **默认常开、零成本禁用**：不需要为了排查而重跑一次。
4. **guest 地址自动可读**：日志里出现的 guest 地址一律自动符号化。
5. **同一份数据服务三方**：人看的终端、AI 用的接口、CI 用的断言。

---

## 3. 日志记录模型

```cpp
struct LogRecord {
    Timestamp   wall;          // 墙钟
    uint64_t    frame;         // 当前帧号  ← 主时间轴
    uint64_t    guest_ticks;   // guest 时钟
    ThreadId    thread;        // 宿主线程 + guest 线程号
    Level       level;         // Trace/Debug/Info/Warn/Error/Fatal
    Category    category;      // 见 §4
    string_view message;       // 固定文本，不做字符串拼接
    FieldSet    fields;        // 结构化键值对
};
```

**关键约定：消息是固定字符串，可变部分一律进 `fields`。**

```cpp
// 反例：拼接后无法聚合、无法过滤
LOG_WARN("guest called null pointer at 0x" << std::hex << lr);

// 正例
OGP_LOG_WARN(hle.null_call, "guest called through null pointer",
             OGP_FIELD(lr, GuestAddr{lr}),
             OGP_FIELD(count, count));
```

这样才能做到「按 message 聚合出现次数」「按 field 过滤」「输出成 JSON 给 AI」。

---

## 4. 分类体系

分类是**层级**的，可按前缀整体调级：

```
loader.apk      loader.elf      loader.link
cpu.jit         cpu.fault       cpu.thread
mem.map         mem.fault
hle.libc        hle.syscall     hle.jni        hle.unimplemented
hle.null_call   hle.quirk
gl.context      gl.draw         gl.state       gl.shader      gl.error
audio.sink      audio.opensl    audio.track
vfs.open        vfs.missing
input.touch     input.map
guest.log       ← 游戏自己打的日志（见 §7）
agent           session         profile
```

运行时调级，不必重启：

```sh
ogplay run game.apk --log "warn,gl.*=debug,hle.unimplemented=trace"
```

Agent 接口同样可以在运行中改：`log.set_level("gl.shader", "trace")`。

---

## 5. 输出后端

一条记录可同时进入多个 sink：

| Sink | 用途 | 默认 |
| --- | --- | --- |
| **Console** | 人看的彩色文本 | 开（Info 及以上） |
| **File** | 完整文本日志，自动轮转 | 开（Debug 及以上） |
| **Ring buffer** | 内存环形缓冲，保留最近 N 条 | **常开（Trace 全量）** |
| **JSONL** | 结构化，给 CI / 离线分析 | 按需 |
| **Agent** | `log.tail()` 实时推送给 AI | 接口连接时 |

### 环形缓冲是重点

**常开、全级别、只在内存里**。出问题时（崩溃、断言失败、Agent 请求）把最近
N 条一次性转储。

这解决了本项目最大的时间浪费：**"看到问题 → 加日志 → 重跑 90 秒 → 再看"** 的循环。
有了环形缓冲，第一次出问题时现场就已经被记录下来了。

---

## 6. 必须具备的能力

### 6.1 guest 地址自动符号化

任何 `GuestAddr` 类型的字段，渲染时自动查符号表：

```
WARN  [f=1284] hle.null_call  guest called through null pointer
        lr = 0x107149f8  (libasphalt6.so + 0x7149f8  render_handler_glitch::begin_display+0x14c)
```

> 这一条的价值在 Asphalt 6 那次排查里已经量化过：地址到符号的转换当时是**人工开
> IDA 做的，重复了四次**。做进日志系统后是零成本的。

### 6.2 限流与去重

同一处日志高频刷屏是这类项目的常态（每帧每个 draw call 都可能打一条）。内建三种策略：

| 策略 | 说明 |
| --- | --- |
| **首次 + 计数** | 同一 (category, message, 关键 field) 只打第一次，之后累计，退出时汇总 |
| **每 N 帧一次** | 周期性指标 |
| **令牌桶** | 限定每秒最大条数，超出部分记为 "suppressed: N" |

**默认对 Warn 以下全部启用"首次 + 计数"**，杜绝刷屏。

### 6.3 帧边界标记

每帧开始/结束插入标记记录，使日志天然按帧分段，可以直接问
"第 1284 帧发生了什么"。

### 6.4 崩溃/异常时自动转储

进程崩溃、guest fault、断言失败时，自动生成诊断包：

```
crash-2026-08-02-153412/
  summary.txt          崩溃点、guest 调用栈（已符号化）、宿主栈
  ring.jsonl           崩溃前最近 N 条全量日志
  unimplemented.json   本次运行触发的未实现 API 清单
  gpu-state.json       GL 状态与最近的帧统计
  last-frame.png       最后一帧画面
  session.json         游戏、profile、quirks、宿主环境、后端信息
```

用户提交 issue 时附上这个目录，问题基本就定位了一半。

---

## 7. 接住 guest 自己的日志

老游戏普遍会调 `__android_log_print` 打大量日志，**这是最被低估的信息源**——
它直接告诉你游戏认为自己在干什么。

- guest 日志归入 `guest.log` 分类，与宿主日志**同一条时间轴**
- 保留原始 tag 与 priority，映射到我们的级别
- 默认开启（老游戏的日志量不大）

宿主日志和 guest 日志交织在一起看，往往一眼就能看出
"游戏说它加载失败了，而我们的 VFS 说文件打开成功了"这类矛盾。

---

## 8. 日志 ≠ 指标 ≠ 断言

三者分工必须清楚，否则会互相污染：

| 机制 | 用途 | 消费者 |
| --- | --- | --- |
| **日志**（本篇） | 叙事：发生了什么，顺序如何 | 人、AI |
| **指标**（[03 · gpu.stats](03-agent-interface.md)） | 量化：draw 数、FBO 归属、shader 编译数 | 断言、趋势 |
| **能力账本**（[02 · 7](02-ai-workflow.md)） | 覆盖度：哪些 API 实现了 | CI 门禁 |

**不要用 grep 日志来做 CI 断言**——那是指标和账本的职责。日志格式必须可以自由演进。

---

## 9. 性能

- **编译期裁剪**：release 构建可去掉 Trace/Debug 级别，宏展开为空
- **运行期短路**：级别检查是一次原子读 + 分支，未启用时不构造任何参数
- **异步落盘**：格式化与 IO 放到独立线程，热路径只做记录入队
- **guest 热路径零分配**：`fields` 用小对象优化，避免每条日志堆分配

要求：全量 Trace 打开时性能下降 < 30%；默认配置下 < 2%。

---

## 10. 从 275 处裸输出迁移

1. 先建 Logger 与分类体系（M0）
2. 按文件批量替换，**替换时顺手归类并定级**——这一步不能机械替换，
   需要判断每条输出是 Debug 还是 Warn
3. 加 CI 门禁：`src/` 中禁止出现 `std::cout` / `std::cerr` / `printf`
   （测试与 CLI 前端的用户可见输出除外，走白名单）
4. 把现有的 80 个 `TALESHLE_TRACE_*` 环境变量映射成日志分类：
   `TALESHLE_TRACE_VFS=1` → `--log vfs=trace`。**开关的数量因此从 80 降到 1。**

---

## 11. 禁止事项

1. **禁止裸 `printf` / `std::cerr`**（CI 强制）
2. **禁止把可变数据拼进 message**，一律用 fields
3. **禁止无分类的日志**
4. **禁止在日志里静默吞掉错误**——日志是记录，不是错误处理。
   打了一条 Warn 不等于处理了这个错误（这正是当前 181 个 JNI 空桩的问题：
   连 Warn 都没有，直接返回 0）
5. **禁止用日志做 CI 断言**（见 §8）
