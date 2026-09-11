# WU-PERF-07 · JNI 重入 Dynarmic executor 复用

目标：消除高频 native → Java → native 同步重入时逐调用构造 Dynarmic JIT 的开销，同时
保持外层暂停现场与每层重入寄存器状态隔离。

背景（macOS Release，pvz-amaz 8.1.0 稳定期 5 秒采样）：

- 修复前 603/847 个游戏线程样本位于 `DynarmicCpu` 构造、JIT prelude 与 icache 初始化；
- 同期窗口呈现约 2.6 FPS，`MainActivity.onUpdateJNI()` 为持续高频重入入口；
- 对照 pvz-291 不含该更新桥，持久 executor 内运行可达 470+ FPS。

验收：

- [x] nested executor 按 `(guest thread id, reentry depth)` 缓存；同层再次调用复用同一
  Dynarmic 实例与 code cache，不同重入深度仍使用不同实例。
- [x] 每次调用重新装载 thread id、TPIDRURO 与完整 A32 状态，nested 栈顶仍取外层暂停 SP。
- [x] DexVM thread 释放时清除该线程的全部 nested executor。
- [x] 定向测试覆盖同层复用、不同层隔离、状态重置与线程释放清理；既有嵌套
  `JNI_OnLoad → Java → native` 回归测试通过。
- [x] pvz-amaz 8.1.0 修复后完成全部 LoadTask；42.743 秒呈现 17660 帧，平均约
  413 FPS。复验 5 秒采样中 `DynarmicCpu` 构造由 603 个样本降至 6 个启动/线程建立样本。

未改变能力边界、Profile、DexVM 后端或游戏专属数据。
