# ADR-0008 · 里程碑出口不得依赖后续阶段能力

- 状态：Accepted
- 日期：2026-08-03

## 背景

原 M1 出口要求最小 NDK 样例在三平台跑出画面并响应输入，但 NativeActivity APK 的运行
同时依赖 M2 的 ELF/Bionic/syscall、M3 的生命周期边界和 M4 的 EGL/GLES/ANGLE。
该出口无法在 M1 能力范围内达成，也会诱导提前实现后续模块。

## 决定

每个里程碑的出口只能依赖本阶段及此前已经完成的能力：

- M1 使用无 APK/ELF/Bionic/JNI/EGL/GLES 依赖的裸 guest mailbox 样本，不要求画面；
- M2 使用导出普通 C 入口的无界面 NDK `.so` 验证 ELF/Bionic 与 syscall；
- M3 使用无界面生命周期/JNI 契约样本，不要求真实 present；
- M4 才以 NativeActivity NDK APK 三平台画面和输入作为累积集成出口。

## 后果

- 各阶段出口可独立、机器化验收，不需要临时桩冒充后续能力。
- 已完成的 `minimal_ndk` APK 保留，但重新归类为 M4 出口载荷。
- M1 新增裸 guest 样本，以标准化 mailbox 输入和确定性状态写回连接 CPU 与 memory；
  SDL 窗口/输入、线程和时钟仍由各自契约测试验证。
