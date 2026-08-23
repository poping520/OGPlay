# BND-16 · EGL/GLES API19 完整性设计与 ABI 冻结

## 目标

以 AOSP 4.4.4 r2.0.1、KTU84P ROM dynsym、Khronos 规范和目标 APK import 建立本轮实现闭集。

## 验收

- [x] 设计文档明确 EGL 13、GLES1 7+62、GLES2 67 项范围与非目标；
- [x] 记录 ROM fingerprint、三个 SO hash/export count；
- [x] 证明目标 `libpvz.so` 导入 `eglGetProcAddress` 和全部 142 GLES2 core；
- [x] WU 按可独立测试/提交的职责切分；
- [x] 未修改生产行为或 capability 状态。
