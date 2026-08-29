# DVM-47 · GC-B exact-title 与长运行 gate 复验

## 目标（一句话）

以 A5/A6/DH 三轮 exact gate 和至少一次真实长运行回收，决定 `dexvm.gc` 能否推进为 complete。

## 依赖

- DVM-42..46
- `docs/design/dexvm/09-gc.md` §11、§12
- `docs/playbook/SCENARIOS.md`

## 交付

- 新增 `asphalt6.gc_long` 长运行 Scenario 和 `dungeonhunter.bootstrap` 启动 Scenario；
  DH 当前只断言可见帧与无 guest fault，不把未稳定的宿主呈现时点伪装成 golden。
- 同步 `MODULE.md`、`CURRENT.md`、`capabilities.toml` 与任务索引。
- 本地证据保存在 `.local/evidence/gc-b/`，不进入版本库。

## 本轮机器证据（2026-08-19）

- A5 `asphalt5.title_flow`：默认 75% 水位连续 3/3 通过，均为
  `468/468000`、SHA-256 `f91150b4...`、无 fault、clean shutdown。
- A5 强制回收探针：临时 Profile 使用 16 MiB 堆与 1% 水位，Scenario 通过且保持
  同一 `f91150b4...`；`runtime.dexvm.gc` 日志中 `freed_bytes`、
  `freed_objects` 多轮非零，证明真实 title 已执行 GC-B。
- A6 `asphalt6.bootstrap`：APS-4 已移除过严的 SONAME/inventory equality 门禁。随后
  snapshot 证明主线程同步停在 `GLSurfaceView$GLThread.a(II)`，GLThread 已完成
  `nativeRender` 却在 33 ms 帧限速 `Thread.sleep` 等待只有 lifecycle 才推进的 Clock。
  通用修复让 worker 只在 EGL pacer 明确报告 clock driver blocked 时补到自身 deadline；
  release 有界复跑现已返回 `surfaceChanged`、启动 lifecycle、切换 `MyVideoView`、解码
  `intro.mp4` 并以 3/3 presented clean stop。该次仍不代替 Scenario 三轮与长运行 gate。
- A6 视频结束后的 GLES2 首错已定位为 active uniform type `0x8B5F`：API19
  `GL_SAMPLER_3D_OES`，并非未知类型。uniform shape 发现按 sampler 单值登记后，带 FFmpeg
  的 release exact run 完成 GLGame/GameRenderer `nativeInit`，持续到 2.5 万余 draw；停止时
  才暴露独立 AudioTrack PCM queue 满。此实跑证明旧崩溃消失，仍不替代三轮 Scenario gate。
- DH：两轮到达可见帧且 clean；第三轮同一宿主 step 预算下呈现序列由 100 漂移到 97，
  触发 frame budget/golden 不一致。无 guest fault，但尚不满足三轮逐位持平。
- Scenario schema/current 校验通过；DVM-42..46 的 GC 单元/DEX 夹具通过。

## 状态裁决

**受阻，不能验收为完成。** GC-B 机制与真实 A5 强制回收已经兑现，但设计规定的
A5/A6/DH 三轮持平和长运行水位回落尚未全部成立。因此 `dexvm.gc` 保持 `partial`，
不增加 A6/DH 专属分支来绕过门禁。

## 解除条件

1. 已通用修复 A6 native identity/DT_SONAME 与启动时钟自锁；bootstrap 仍需三轮通过；
2. 将 DH 的呈现时点纳入确定性控制后，DH 三轮同 golden 通过；
3. `asphalt6.gc_long`（或经设计认可的等价 exact title 长运行 gate）证明 GC 至少发生
   一次且回收后水位下降；
4. 以上满足后把本任务与 `dexvm.gc` 单调推进为 complete。
