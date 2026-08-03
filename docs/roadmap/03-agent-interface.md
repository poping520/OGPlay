# 03 · Agent Control Interface（AI 操控与调试接口）

本篇回答需求 2.2：**用什么途径让 AI 高效地操控和调试模拟器。**

---

## 1. 为什么现在的方式不够用

DEMO 阶段 AI 调试模拟器的方式是：**设环境变量 → 跑一次 → 用正则从文本日志里刮结果**。

这次排查 Asphalt 6 菜单缺失，实际付出的代价：

| 环节 | 现状 | 代价 |
| --- | --- | --- |
| 触发场景 | 靠 `TALESHLE_TOUCH_FRAME` 等一串环境变量 | 组合爆炸，已有 80 个开关 |
| 取数据 | 从几万行文本里 `rg` | 非结构化，格式一变就失效 |
| 一次迭代 | 全量重跑 90–160 秒 | 无法快速试错 |
| 地址→符号 | **人工开 IDA 查** | 每次几分钟，是最大的单点浪费 |
| 判断 RTT 是否生效 | 靠一行 `draw_fbos=...` 文本 | 靠运气有人打了这个日志 |
| 发现"被吞掉的失败" | **完全没有手段**，靠猜 | 这是本次耗时的根本原因 |

结论：**要把"模拟器内部状态"做成结构化、可查询、可断言的接口，而不是文本日志。**

---

## 2. 形态

```
┌──────────────┐   MCP (stdio)   ┌─────────────────┐
│ Cursor / AI  │ ◄─────────────► │  MCP Adapter    │
└──────────────┘                 └────────┬────────┘
                                          │ JSON-RPC 2.0
┌──────────────┐   JSON-RPC      ┌────────▼────────┐
│  CI / 脚本   │ ◄─────────────► │ Agent Control   │
└──────────────┘  (TCP / UDS)    │ Server（内置）   │
                                 └────────┬────────┘
                                          │
                                 ┌────────▼────────┐
                                 │   模拟器内核     │
                                 └─────────────────┘
```

- **内核内置 Agent Control Server**，协议 JSON-RPC 2.0，传输可选 stdio / TCP / Unix socket。
- **MCP Adapter** 是薄封装，让 Cursor 里的 AI 直接调用。CI 和脚本走裸 JSON-RPC。
- 默认只在 `--agent-port` 显式开启时监听；release 构建可编译期裁掉。

**设计原则**：

1. 所有返回值是结构化 JSON，不是给人看的文本。
2. 所有耗时操作可**按帧步进**，不用 sleep。
3. 只读查询与副作用操作分开命名，AI 可以放心大胆地查。
4. 每个接口都能被 CI 用作断言来源——调试接口和测试接口是同一套。

---

## 3. 接口分组

### 3.1 session · 生命周期

```
session.open(apk, {profile, api_level, data_root, window: bool}) -> {session_id, title_info}
session.close()
session.state() -> {phase, frame, guest_threads, wall_ms, guest_ticks}
```

### 3.2 run · 时间控制（确定性）

```
run.step(frames=1) -> {frame, events[]}      # 步进 N 帧后返回，不 sleep
run.until(predicate, max_frames)             # 例：until "gpu.draw_fbos>=2"
run.pause() / run.resume()
run.record(path) / run.replay(path)          # 输入+时序录制回放，位级确定
```

`run.step` 是整套接口的核心：AI 不再"跑 125 帧再看结果"，而是**边走边看**。

### 3.3 input · 输入注入

```
input.tap(x, y, {duration_frames})
input.swipe(from, to, frames)
input.multi_touch([{id, action, x, y}])
input.key(code, action)
input.gamepad(state)
input.sensor.accelerometer(x, y, z)          # 赛车类必需
```

坐标统一用**逻辑表面坐标**，并在返回值里回报换算后的物理坐标，杜绝 DEMO 阶段
"截图坐标 vs 逻辑坐标差 1.25 倍"那种低级错误。

### 3.4 frame · 画面

```
frame.capture({target: "screen"|fbo_id}) -> {path, width, height, phash}
frame.diff(path_a, path_b) -> {pixel_diff_ratio, phash_distance, diff_image}
frame.assert_golden(golden_id) -> {pass, ...}
```

### 3.5 gpu · 图形状态（本次 bug 的关键）

```
gpu.stats() -> {draws, clears, shader_compiles, program_links,
                draw_targets: [{fbo, draws, attachment}], gl_errors}
gpu.render_targets() -> [{fbo, size, format, attachments, created_by_guest: bool}]
gpu.capabilities() -> {reported_extensions[], reported_limits{}, host_backend}
gpu.trace({filter, limit}) -> [{call, args, error}]     # 结构化，不是文本
gpu.dump_texture(id) / gpu.dump_fbo(id)
gpu.shader(id) -> {stage, source, translated_source, log}
```

> `gpu.render_targets()` 里的 `created_by_guest` 一个字段，就能直接回答
> "游戏到底有没有建过自己的 FBO"——本次排查花了很久才靠日志间接推断出来。

### 3.6 hle · 系统调用面（"游戏要了什么我们没有"）

```
hle.calls({filter, since_frame}) -> [{symbol, count, last_lr}]
hle.unimplemented() -> [{symbol, count, first_lr, category}]
hle.null_calls() -> [{lr, symbol, count}]
hle.capabilities() -> capabilities.toml 的运行时视图
```

**这是最重要的一组。** 它把"静默失败"变成可查询的事实：

- `hle.unimplemented()` —— 游戏调了但我们没实现的
- `hle.null_calls()` —— 空指针调用被兼容层吞掉的（DEMO 阶段刚补的
  `TALESHLE_TRACE_NULL_CALLS` 就是它的前身）

### 3.7 sym · 符号化（收益最高的单个功能）

```
sym.resolve(addr) -> {module, symbol, demangled, offset, source_hint}
sym.backtrace({thread}) -> [frame...]
sym.lookup(name) -> [{addr, size}]
```

内置 ELF 符号表 + C++ demangle。

> 本次排查里 `lr=0x107149f8` → `render_handler_glitch::begin_display` 这一步是**人工开
> IDA 做的**，重复了四次。做进接口后是一次调用的事。**这个功能应该最先做。**

### 3.8 mem / cpu · 底层调试

```
mem.read(addr, size) / mem.write(addr, bytes)
mem.regions() -> [{base, size, perm, name}]
mem.search(pattern, range)
cpu.regs({thread}) / cpu.threads()
cpu.breakpoint(addr) / cpu.watchpoint(addr, size, rw)
cpu.step_instructions(n)
```

### 3.9 fs · 虚拟文件系统

```
fs.accesses({since_frame}) -> [{guest_path, host_path, op, result}]
fs.missing() -> [{guest_path, count}]          # 游戏找了但不存在的文件
fs.map() -> [{guest_prefix, host_root, source: apk|obb|external}]
```

`fs.missing()` 直接回答"资源是不是没放对"，这是老游戏最高频的用户问题（见 [06](06-user-experience.md)）。

### 3.10 log

```
log.tail({level, category, limit}) -> [{ts, frame, level, category, message, fields{}}]
```

日志内部就是结构化记录，文本输出只是它的一个渲染器。

---

## 4. 如果当初有这套接口：A6 菜单问题的排查流程

```python
session.open("Asphalt6.apk", window=False)
run.until("phase == 'main_menu'", max_frames=200)

gpu.render_targets()
# → []  ——— guest 一个渲染目标都没建过。方向立刻确定。

hle.null_calls()
# → [{lr: 0x107f2bf8, count: 42}, {lr: 0x107149f8, count: 42}, ...]

sym.resolve(0x107f2bf8)
# → glitch::video::IVideoDriver::pushRenderTarget + 0x68
sym.resolve(0x107149f8)
# → render_handler_glitch::begin_display + 0x14c
# 结论：Flash UI 的渲染目标是空指针。

gpu.capabilities()
# → reported_extensions: [... 没有 GL_OES_rgb8_rgba8 ...]
# 对照游戏二进制里 createRenderTarget 的格式检查 → 根因确定
```

**四次调用定位根因。** 实际耗时是数小时的量级差。

---

## 5. 与测试体系共用

Agent Control API 同时是 L3/L4 测试的执行层。题库回归脚本就是一串
`input.tap` / `run.until` / `gpu.stats` 断言，**AI 调试用的接口和 CI 断言用的接口是同一套**，
不会出现"调试能看到但测试断不了"的割裂。

---

## 6. 实施优先级

| 优先级 | 内容 | 理由 |
| --- | --- | --- |
| P0 | `sym.*` | 单位投入收益最高，立刻消除人工 IDA 环节 |
| P0 | `hle.unimplemented` / `hle.null_calls` | 把静默失败变成显式事实 |
| P0 | `run.step` / `run.until` + `session.*` | 确定性步进是一切自动化的地基 |
| P1 | `gpu.stats` / `gpu.render_targets` / `gpu.capabilities` | 图形问题占比最高 |
| P1 | `frame.capture` / `frame.assert_golden` | 支撑黄金帧测试 |
| P1 | `fs.missing` / `fs.accesses` | 支撑资源导入体验 |
| P2 | `mem.*` / `cpu.*` / 断点 | 深度调试 |
| P2 | `run.record` / `run.replay` | bug 复现与二分 |
| P2 | MCP Adapter | 有了 JSON-RPC 之后是薄封装 |
