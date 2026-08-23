# DVM-70 · PVZ API19 启动闭环设计

## 目标（一句话）

冻结 BND-24 之后的首个 DexVM 阻断、API19 权威语义、迭代边界与
真正可运行的 exact-title 验收标准。

## 依赖

- BND-16..24 EGL/GLES API19 闭集与 exact ELF/run 证据
- `docs/playbook/NEW-TITLE.md`
- AOSP `android-4.4.4_r2.0.1` Window/Activity 源码

## 验收

- [x] 关闭 survey 的首 fault 固定为 `Window.setSoftInputMode(I)V`；
- [x] diagnostic survey 证明相邻缺口为 `Activity.getRequestedOrientation()I`；
- [x] 设计明确 Window/LayoutParams 与 per-Activity orientation 的可观察状态；
- [x] 设计排除 Binder、宿主 IME/旋转伪实现和 title-specific 生产分支；
- [x] 最终 gate 要求 non-survey 可见帧、无 fault 与 clean shutdown。

状态：完成。
