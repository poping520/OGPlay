# BND-57 · 虚拟 Cortex-A9 用户态同步 SIGILL

状态：实现完成，macOS 首错复现通过（2026-11-08）。

依赖 [BND-41](BND-41.md) 的 ARM EABI 信号帧、[BND-44](BND-44.md) 的精确协处理器停止和
[BND-42](BND-42.md) 的虚拟 CPU 身份。范围见 [ADR-0079](../../adr/runtime.md#adr-0079)。

## 根因与设计

- Bad Piggies HD 的 `libRCSSDK.so` 在 `_armv7_tick`（ELF 地址 `0x4a4b88`）执行
  `0xec510f1e`（`MRRC p15,1,r0,r1,c14`）。本地反汇编确认调用方安装 SIGILL handler，
  用 `sigsetjmp` 的非零返回跳过探测结果，handler 在 `0x4a4b08` 调用 `siglongjmp`。
  虚拟 Cortex-A9 无 Generic Timer。[Linux ARM 3.4 的 undef 路径](https://github.com/torvalds/linux/blob/v3.4/arch/arm/kernel/traps.c#L350-L402)
  使用 ILL_ILLOPC 和故障 PC。
- integration 根据 Cortex-A9 PL0 语义分类，execution 在 CPU 停止边界共用同步投递入口。
  仅非法编码、PL0 不可访问的 CP15 和不存在的协处理器可触发 SIGILL。
  TPIDRURW、CP14、VFP/NEON 的后端缺口、其他未实现指令及访存故障维持原诊断。
- 同步信号仅投给未屏蔽的用户 handler，复用原 ARM 帧与返回桥，保存故障 PC/CPSR。
  默认、忽略和屏蔽仍走原 fault 归因。
- Interpreter 区分显式 UDF 与未实现解码，统一 T32 指令字顺序。Dynarmic 为非法/不可预测
  停止保留故障指令的 PC 和 IT，信号策略仍在 CPU 层外。

## 验收边界

双 CPU 后端的 A32/T32 guest 信号 ABI 和分类负例、现有 BND-44 回归，以及原 APK 一次有界
首错复现。只判定是否越过原首错及下一阻塞，不做正式 title gate、游戏交互或跨平台验收。

## 实测

```sh
CCC_OVERRIDE_OPTIONS='# +-Wno-error=unused-result' cmake --build build/release --target ogplay_tests ogplay-cli --parallel 8
build/release/ogplay_tests -tc='*signal*,*BND-44*,*guest cpu environment*,*BND-57*,*BND-42*,*interpreter reports budgets*'
./build/release/ogplay-cli library run com.rovio.BadPiggiesHD
python3 tools/capabilities.py check
```

- C/C++ 改动行经 clang-format 格式化后，两个 Release 目标构建通过（83.58 秒）。沿用
  DVM-374 对既有 `unused-result` 构建告警的局部放宽。定向测试 41 用例、1511 断言通过
  （0.95 秒），能力账本 440 项检查通过。
- 原命令运行一次，9.29 秒后退出 1。日志确认 `libRCSSDK.so` 构造与 JNI_OnLoad 完成，
  原 `0xec510f1e` 首错消失。结合本地反汇编，加载完成与 SIGILL handler 经
  `siglongjmp` 跳过探测吻合。此次没有单独记录 handler 指令轨迹。
- 下一致命首错在 `f=3951`，UnityMain 的 `UnityPlayer.nativeRender()Z` 报
  `JNI object is not a string in this store`。此前有 Flurry 捕获的 `ThreadGroup(String)`
  缺口；未再出现此前 `f=3542` 的 FMOD `fmodGetInfo` 退出。证据在 `.local/bnd-57/`。
- CP14、TPIDRURW、通用后端缺口及访存 fault 到 SIGSEGV 仍在本任务范围外。
  既有活动帧跟踪通过 sigreturn 或线程退役清理，反复从 handler 非局部跳转的帧回收仍有限制。
