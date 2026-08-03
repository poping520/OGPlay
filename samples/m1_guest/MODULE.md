# 模块：m1_guest sample

## 职责

提供不依赖 Android 运行时和图形栈的 A32/Thumb 裸 guest 程序，作为 M1 CPU、memory、
标准化输入与快照确定性的纵向集成载荷。

## 公共边界

- `sample.h`：固定代码地址、mailbox ABI、A32/Thumb 指令和预期结果函数。
- r4 是唯一入口参数，指向包含 input/output/sequence 的小端读写 mailbox。
- SVC 立即数是成功停止协议，不直接调用 HLE。

## 不变量

- 不使用 APK、ELF、Bionic、syscall、JNI、Activity、EGL 或 GLES。
- 不包含宿主指针、平台分支、游戏身份或静默成功路径。
- 解释器与 JIT 必须执行完全相同的字节和 mailbox 契约。

## 验证

`tests/cpu/m1_guest_sample_tests.cpp` 验证 A32/Thumb、标准化输入、线程标识和快照复跑；
三平台及 JIT 对拍在 M1 收尾时使用同一测试晋级。
