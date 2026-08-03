# OGPlay M1 裸 guest 样本

这是 M1 的无画面出口载荷。它不是 APK 或 ELF，不依赖 Android SDK/NDK、Bionic、syscall、
JNI、Activity、EGL 或 GLES；同一组指令和 mailbox 契约可直接交给解释器与 JIT 后端。

## Mailbox ABI

宿主将 r4 指向一个读写页，并按小端写入：

| 偏移 | 方向 | 含义 |
| --- | --- | --- |
| `+0` | host → guest | 标准化输入值 |
| `+4` | guest → host | 确定性输出 |
| `+8` | 双向 | 输入序号，guest 完成后加一 |

A32 程序写回 `input * 3 + 7`，Thumb 程序写回 `input + 7`，随后以带固定立即数的
SVC 结束。不同公式可防止测试壳误把两种执行状态当成同一路径。

## 验收方式

`tests/cpu/m1_guest_sample_tests.cpp` 在强类型 guest 地址空间装载样本，注入输入与线程号，
验证取指/访存事件、tick、SVC、状态写回，并从 CPU+内存快照复跑得到相同结果。

样本就绪不代表 M1 已达到出口；M1 收尾还必须让解释器与 JIT 对拍，并在 Windows、Linux、
macOS 执行相同测试。窗口/输入 HAL、线程和时钟使用各自契约测试，不由本样本伪装覆盖。
