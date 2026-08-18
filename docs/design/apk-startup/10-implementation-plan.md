# 10 · 实施阶段与 WU 映射

本设计按“先事实模型 → 再 process shell → 再 dynamic load → 再生命周期 → 最后
Profile/迁移”的顺序实施。每个 WU 都应能单独 review，避免 frontend、ELF、DexVM、
Profile schema 同时大改。

## 1. 阶段划分

### 阶段 A · APK 事实准备

- APS-1：Manifest Application / launcher / alias model；
- APS-2：APK native inventory + process ABI resolver。

**出口：** 不启动 guest，也能从 APK 得到完整 `ApkStartupFacts`。

### 阶段 B · native process 可动态扩展

- APS-3：拆出无 app root 的 `AndroidGuestProcess` shell；
- APS-4：dynamic ELF namespace + `NativeLibraryLoader` + per-explicit-load JNI_OnLoad。

**出口：** host 测试能先建 process，再动态 load APK `.so`。

### 阶段 C · Java 驱动 native load

- APS-5：`System.loadLibrary` / `System.load` 接真实 loader，闭合重入。

**出口：** DexVM Java 代码决定何时 load，frontend 不再是唯一 native load 入口。

### 阶段 D · Android 应用入口

- APS-6：最小 Application startup；
- APS-7：launcher Activity + `AndroidAppProcess` orchestrator + `run-apk` generic path。

**出口：** 无 Profile fixture 可按 Application → Activity 启动。

### 阶段 E · Profile 降级与迁移

- APS-8：Profile v3/legacy adapter，`so_sha256` 退出启动门禁；
- APS-9：title gates、旧设计冲突归档、文档/ledger 收尾。

**出口：** generic path 成为默认，Profile 只剩 optional compatibility override。

## 2. WU 索引

| WU | 任务书 | 主要边界 |
| --- | --- | --- |
| APS-1 | [Manifest startup facts](../../tasks/apk-startup/APS-1.md) | loader / manifest |
| APS-2 | [APK native inventory + ABI](../../tasks/apk-startup/APS-2.md) | loader / ABI |
| APS-3 | [rootless guest process shell](../../tasks/apk-startup/APS-3.md) | runtime integration |
| APS-4 | [dynamic native library loader](../../tasks/apk-startup/APS-4.md) | ELF / JNI |
| APS-5 | [System.load* bridge + reentry](../../tasks/apk-startup/APS-5.md) | DexVM / runtime boundary |
| APS-6 | [Application startup](../../tasks/apk-startup/APS-6.md) | lifecycle |
| APS-7 | [launcher + AndroidAppProcess](../../tasks/apk-startup/APS-7.md) | session/frontend |
| APS-8 | [Profile v3 + legacy migration](../../tasks/apk-startup/APS-8.md) | profile/schema |
| APS-9 | [integration gates + design migration](../../tasks/apk-startup/APS-9.md) | regression/docs |

## 3. WU 边界红线

- 单 WU 预估触及超过 10 个 code/test 文件时，**先拆任务书再编码**；
- APS-3 不顺手实现 `System.loadLibrary`；
- APS-4 不顺手改 Application lifecycle；
- APS-6 不顺手做完整 ActivityThread/Provider；
- APS-8 不为迁移方便恢复 root-so preload；
- 每 WU 先立失败/fixture，再实现；
- 本地 AOSP 参考结论按 09 的格式写回任务书。

## 4. 允许的过渡态

实施期间允许短期双路径，但必须受 WU 出口约束：

- APS-3 后：旧 title path 仍可通过 legacy root module adapter 启动；
- APS-4 后：dynamic loader 可先由测试直接调用，frontend 尚未切换；
- APS-5 后：DexVM 可真实 load，但 Application 尚未接入；
- APS-7 后：generic path 成为默认，legacy profile 只做 override；
- APS-8 后：新 profile 不要求 `so_sha256`。

过渡 adapter 必须有明确删除/维护归属，不能成为第二套永久架构。

## 5. AI 上下文装载顺序

执行任一 APS WU：

```text
AGENTS.md
→ docs/state/CURRENT.md
→ docs/design/apk-startup/README.md
→ 当前 WU 对应设计篇
→ docs/tasks/apk-startup/<APS-N>.md
→ 相关 MODULE.md
→ 09-aosp-reference.md 指定的本地 AOSP 文件
```

不要一次性把整个 `docs/design/dexvm/` 当作上下文；只按当前 WU 引用读取相关章节。
这正是本目录拆篇的目的。

## 6. 任务完成后的记录

每个 APS 任务完成后：

1. 原任务书留在原路径，不移动、不重编号；
2. 追加/改写 `结果（机器可判定，已达成）`，记录测试、关键代码边界、语义出处；
3. 大体积运行证据放 `.local/...`，任务书只记录路径/摘要；
4. 同步 `MODULE.md`、capability ledger、`CURRENT.md`（若项目流程要求）；
5. 新发现设计冲突补入本目录对应章节，不能只留在 commit message。
