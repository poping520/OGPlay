# M9 交接：把 Dungeon Hunter 与 Asphalt 6 带上 dexvm 路线

本文件是给下一位接手 AI 的**上下文根节点 + 工作队列**。目标：让
`DungeonHunter_Samsung_P1000.apk` 与
`Asphalt6/Asphalt_6_Adrenaline_HD_GLstore_v1.3.2.apk` 在
schema v2 `dex_activity`（dexvm 解释执行）下启动并进入游戏界面。

阅读顺序：`AGENTS.md` → `docs/state/CURRENT.md` → `docs/design/dexvm/README.md`
→ 本文件 → 相关 `MODULE.md`。设计冲突一律以 `docs/design/dexvm/` 为准
（ADR-0017）。

---

## 1. 可信基线（已验收，勿回退）

| 事实 | 证据 |
| --- | --- |
| dexvm 全链路可跑通一款 exact title | Asphalt 5 以 v2 `dex_activity` 进入主界面，`asphalt5.title_flow` gate 三轮 + 迁移后复验 passed，golden SHA `9ee57323…` 与 v1 路线逐位一致 |
| Asphalt 5 已正式迁移 | `data/profiles/com.gameloft…GloftAsphalt5…toml` 从 201 行 v1（16 条 `native_call` + 33 条 `[[java.class]]`）变为 25 行 v2 |
| 全量测试 | macOS/arm64 `ctest --preset dev` **558/558**（commit `20fbba0`） |
| WU 记录 | `docs/tasks/m9/WU-M9-001..021`，索引见 `docs/tasks/m9/README.md` |

dexvm 子系统组成（`src/runtime/dexvm/`、`src/runtime/integration/dexvm_*`、
`src/session/dex_activity_lifecycle.cpp`）与契约见
`src/runtime/dexvm/MODULE.md`、`src/runtime/integration/MODULE.md`。

---

## 2. 两款目标 title 的当前阻塞点（已定位，带证据）

### 2.1 Asphalt 6 — `loader.dex_l1` 过严，拒绝数组 owner 的 method_id

**现象**（DEX 解析期，进不到 dexvm）：

```
ogplay: DEX method_id contains an invalid index
```

**根因（已用独立脚本证实）**：A6 的 `classes.dex` 有 **4 个** `method_id`，其
`class_type_index` 指向**数组类型**：

```
[Lcom/boku/mobile/android/ui/b;->clone
[Lcom/boku/mobile/android/ui/f;->clone
[Lcom/boku/mobile/api/ClientState;->clone
（共 4 项，均为 clone）
```

`src/loader/dex.cpp` 当前要求 method_id/field_id 的声明类必须是
`L…;` class descriptor（见 `src/loader/MODULE.md` 不变量），但
**dex-format 允许数组类型作为 method 引用的 owner**（数组继承
`Object.clone()`）。这是我方解析器的过严规则，不是坏 dex。

**修法**：放宽 `dex.cpp` 中 method_id owner 的校验为「class descriptor 或
数组 descriptor」；field_id 保持只允许 class descriptor（字段不能挂数组）。
必须同步：
- `src/loader/MODULE.md` 的不变量措辞；
- 新增反例/正例测试（`tests/loader/dex_analysis_tests.cpp` 或
  `tests/dexvm/`）：数组 owner 的 `clone()` 可解析、非法 descriptor 仍拒绝；
- AOSP 依据：`libdex/DexFile.h` + `docs/dex-format`（07 §2 模式 B，测试注释
  记录出处）。

**这是 A6 的第一道门，必须先做**，之后才能看到真正的 intrinsic 缺口。

### 2.2 Dungeon Hunter — 链接期缺 android.* intrinsic（层级引用）

**现象**（链接期，装配失败，符合设计 03 §6：super/接口缺失 = 明确装配失败）：

```
ogplay: interface is not available: Landroid/view/View$OnClickListener;
        (required by Lcom/gameloft/…/GLiveMain$1;)
```

**缺口清单（机器生成，链接阻塞项 = 被当作 super/接口引用的平台类）**：

DUNQ 链接阻塞 **11** 项：

```
Landroid/content/DialogInterface$OnClickListener;
Landroid/media/MediaPlayer$OnCompletionListener;
Landroid/opengl/GLSurfaceView$Renderer;      ← 注意：目录里已有，descriptor 需核对
Landroid/os/Handler;
Landroid/text/TextWatcher;
Landroid/view/View$OnClickListener;
Landroid/view/View$OnTouchListener;
Landroid/webkit/WebViewClient;
Ljava/util/TimerTask;
Ljavax/net/ssl/HostnameVerifier;
Ljavax/net/ssl/X509TrustManager;
```

A6 链接阻塞 **29** 项（含 `Landroid/app/Dialog;`、`Landroid/os/AsyncTask;`、
`Landroid/view/SurfaceView;`、`Ljava/lang/Enum;`、`Ljava/io/ObjectInputStream;`、
`Ljava/util/zip/ZipInputStream;`、`Landroid/os/CountDownTimer;`、
`Landroid/telephony/PhoneStateListener;` 等，完整清单用第 4 节脚本重算）。

运行期（仅在真正命中时才失败）缺口：DUNQ **79** 项、A6 **172** 项 method
引用。**不要预先全实现**——按设计 03 §3「只实现真实命中」，逐轮跑、逐轮补。

**关键分层策略**（设计 03 §6）：
- **纯层级占位**（listener/callback 接口、`TimerTask`、`Enum` 等）：只需在目录里
  声明类/接口即可让链接通过，方法留空——命中未实现方法时按现有机制记账 +
  `UnsatisfiedLinkError`，不伪造成功。**这是解锁两款游戏最省力的一步。**
- **真实语义**（`Handler`、`AsyncTask`、`SurfaceView`、`Dialog`、
  `ObjectInputStream`、`ZipInputStream`）：需要真实实现或明确失败，按命中批次做。
- **非目标**（`javax.net.ssl.*`、`WebViewClient`、`WebChromeClient`）：只声明类型
  占位，任何方法命中一律记账 + 明确失败（网络/WebView 属 01 §3 非目标）。

### 2.3 一个必须先复核的不一致（诚实记录）

在更早一轮调试中（当时的 staging profile 已被清理），DUNQ **越过了链接**并
执行到解释期，停在：

```
DungeonHunter;.obfuscateData pc 13: register v6 out of range
（registers=6，instructions[13]=0x4d4d）
```

而**当前**这款 title 在更早的链接期就失败。两者不应同时成立（`Link()` 对全部
dex 类解析层级，是确定性的）。接手第一步应当：

1. 用第 4 节命令复现当前链接失败，确认 2.2 是真实的第一道门；
2. 待链接缺口补齐后，**重新验证** `obfuscateData` 是否仍失败。

关于 `obfuscateData` 已查明与未查明：
- 已确认：该方法 `registers=6 / ins=3 / insns_size=150`；解释器读到的指令流
  与 dex 文件字节**逐一相同**（已用「C++ dump 指令流 vs python 读文件」对拍）；
  顺序解码下该方法**没有任何寄存器越界**。
- 未查明：运行期 pc 走到 13 时，`instructions[13]=0x4d4d` 被当作
  `aput-object vAA=77` 执行 → 越界。领先假设是**控制流落入 payload 数据区**
  （该方法是解密循环，含分支/回边；`fill-array-data` / `packed-switch` /
  `sparse-switch` 的 payload 若被当作指令执行就会出现这种"合法 opcode、
  荒谬寄存器"的特征）。
- 建议下一步：给解释器加一个**受环境变量控制的执行轨迹**（pc/opcode/宽度），
  跑一遍取 pc 序列，检查是否有 pc 落在「顺序解码边界集」之外，以及紧邻的
  分支/switch 指令的目标计算。**顺手把这个轨迹做成正式的
  `dexvm.trace` 诊断能力**（对齐 04 §8 的 `dexvm.stack`/`dexvm.stats`），
  别再用一次性探针——本轮的调试低效主因就是缺这个。

---

## 3. 工作队列（按依赖顺序，每项都是一个 WU）

| # | 内容 | 出口（机器可判定） |
| --- | --- | --- |
| 1 | `loader.dex_l1` 允许数组 owner 的 method_id（2.1） | A6 越过 DEX 解析；新增正/反例测试；full CTest 不回归 |
| 2 | 正式 `dexvm.trace` / `dexvm.stack` 诊断能力（04 §8） | 可用环境变量或 Agent 查询导出 pc/opcode/栈；有单测 |
| 3 | android.* intrinsic 批次 A：**纯层级占位**（两款共 40 项链接阻塞类） | 两款 title 均越过 `Link()`，进入解释执行 |
| 4 | 复核 `obfuscateData`（2.3），按轨迹定位并修 | DUNQ 越过解密流程；若确认是 payload/分支 bug，补 dexasm 一致性夹具（「关闭即失败」） |
| 5 | android.* intrinsic 批次 B：按真实命中补 `Handler`/`AsyncTask`/ `SurfaceView`/`Dialog`/IO 流族 | 逐轮 run-apk 推进，缺口列表单调收敛 |
| 6 | GC-B（精确非移动 STW 标记清除，04 §5 B 期） | 两款厚 title 长时运行不再靠加大堆预算；`dexvm.gc` 能力条目推进 |
| 7 | 两款 title 的 Scenario + 三轮 gate，迁移 v2 profile 进 `data/profiles/` | 各自 gate 三轮通过、clean shutdown；profile 行数净减 |

**GC-B 的优先级已被实证抬高**：Asphalt 5 试玩时 64 MiB GC-A 预算在换语言/
进赛道路径被瞬态资源数组耗尽（现用 512 MiB 兜住）。两款更厚的 title 几乎
必然更早撞上，见 `docs/tasks/m9/WU-M9-019.md` 追加段。

---

## 4. 复现与工具（精确命令）

**构建**（macOS/arm64；Windows 用 `windows-msvc` 预设）：

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

**跑 title**（注意：必须在 `build/dev` 下执行，ANGLE 动态库按相对路径加载；
`--system-dir` 指向 Bionic 的 **lib 子目录**）：

```bash
cd build/dev
# Dungeon Hunter
./ogplay run-apk ../../docs/demo/games/DungeonHunter_Samsung_P1000.apk \
  --system-dir ../../.local/bionic-oracle/api19/lib \
  --profiles-dir ../../data/profiles-dexvm \
  --external-dir ../../docs/demo/games/DungeonHunter \
  --exit-after-frames 3
# Asphalt 6（外部数据在 files 子目录，不是包名目录）
./ogplay run-apk ../../docs/demo/games/Asphalt6/Asphalt_6_Adrenaline_HD_GLstore_v1.3.2.apk \
  --system-dir ../../.local/bionic-oracle/api19/lib \
  --profiles-dir ../../data/profiles-dexvm \
  --external-dir ../../docs/demo/games/Asphalt6/com.gameloft.android.GAND.GloftAPHP/files \
  --exit-after-frames 3
```

**staging profile**：`data/profiles-dexvm/` 下已备好两款的 v2 profile（
`dex_activity` + 512 MiB 堆 + 100 亿 tick）。它**故意不在** `data/profiles/`，
以免 `tools.title_profiles_current` 门禁与生产中的 v1 profile 冲突；title 通过
gate 后再迁移（参考 Asphalt 5 的迁移方式）。

**Scenario 自动化**（本轮新增易用性，见 WU-M9-020/021）：

```bash
cd build/dev && python3 ../../tools/run_scenario.py \
  --scenario <scenario.toml> --profiles ../../data/profiles \
  --ogplay ./ogplay --system-dir ../../.local/bionic-oracle/api19/lib \
  --fixture <id>=<apk> --evidence-dir <dir> --fresh
```

- `--watch`：**增量编写模式**。会话保活，向 scenario 末尾追加 checkpoint 只
  执行增量（实测 2ms vs 全量重放 ~30s）；改动已执行前缀或出现失败则自动
  重启并重放到新的 `gen<N>/`；Ctrl-C / SIGTERM 优雅收尾并产出合法 Result v1；
  每步额外落 `frame_overlay.png` 坐标网格图供点击坐标标定。
- 失败的 checkpoint 一定会留 `failure_logs.txt`（stdout/stderr 末 200 行）。
- 校验失败时 stdout 输出机读 JSON（`{"schema":1,"status":"invalid",...}`）。

**分析工具**：
- `tools/dexdis.py --apk <apk> --class-prefix <L…> [--method <name>]`：
  反汇编辅助。**注意：它的 pc/宽度显示有已知瑕疵，不可作为权威**；权威事实
  请用 C++ 侧 dump 或 `tools/dex_survey_lib.py` 直读文件字节。
- `tools/dex_dependency_survey.py --apk … --profiles … --output …`：题库测量。
- 重算 intrinsic 缺口清单（本文件第 2 节数据的来源思路）：解析 APK 的
  平台类引用，减去 `src/runtime/dexvm/core_catalog.cpp` 与
  `src/runtime/integration/dexvm_android.cpp` 里 `descriptor = "L…;"` 的声明集，
  并按「super/接口引用（链接阻塞）」与「method 引用（运行期命中）」分层。

---

## 5. 工程约束（AGENTS.md 之外，本轮踩到的）

1. **诚实失败**：任何未实现的 opcode/intrinsic/反射面必须记账 + 明确失败。
   本轮修过一个反例：`AssetManager.open` 缺条目原先抛宿主 C++ 异常，已改为
   游戏真实可捕获的 `java.io.IOException`。同类问题要按此处理。
2. **JNI pending exception 会远点爆雷**：解释方法抛出的 Java 异常按 JNI 语义
   置为 pending，native 侧下一次调用会被严格门禁挡下，报出的却是
   `GetObjectClass` 之类无关的错。入向桥现在会把 pending 异常打成结构化
   `warn`（类/消息/解释器栈）——**遇到"莫名 JNI 阻塞"先看这条 warn**。
3. **`registers_size` 与 wide 访问**：`GetWide(vN)` 需要 `vN`/`vN+1`，越界消息
   报的是 `vN+1`。看到「register vX out of range」先核对方法的
   `registers`/`ins`。
4. **不要相信手算 hex**：本轮大量时间浪费在人工数 code unit 上。要么让 C++
   dump 二进制、python 结构化比对，要么用正式 trace 能力（队列第 2 项）。
5. **v1 冻结**：`[[java.class]]` 胶水目录冻结增长（裁决 14）；不要为让新 title
   跑起来去补 v1 profile——直接走 dexvm 路线。
6. **每个 WU 收尾**：同步 `MODULE.md`、推进 `capabilities.toml`（状态只前进）、
   滚动更新 `docs/state/CURRENT.md`（≤6144 字节），提交前跑
   `cmake --build --preset dev` + `ctest --preset dev`。

---

## 6. 本轮已交付清单（供追溯）

- 阶段 0：AOSP `android-4.4.4_r2` 参考基线（哈希门禁）、opcode 目录（218 项，
  三锚点机器比对）、`dexasm` 确定性汇编器、题库测量工具。
- 阶段 1：`loader.dex_code`、类链接、`JavaObjectModel`、GC-A、tagged 帧解释器
  （全 dex 035 家族）、异常展开、`<clinit>`。
- 阶段 2：JNI 出向编组 + 入向第三路由、`loader.arsc`、java.* P1 intrinsic。
- 阶段 3：android.* intrinsic 首批、`dex_activity` 生命周期反转、
  Title Profile v2（Python/C++ 双校验）、Asphalt 5 pilot 迁移与 gate。
- 工具：Scenario runner 易用性批次（`--fresh`、失败必留日志、机读失败输出、
  预算报错带算术、`--watch` 增量编写）。
