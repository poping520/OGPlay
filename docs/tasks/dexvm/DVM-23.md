# DVM-23 · 入口覆盖与 provisioned 前提

## 目标（一句话）

DexActivityLifecycle 使用 Profile 入口覆盖 manifest launcher，且 required data 未就绪时明确失败。

## 验收

- 无覆盖时保持 manifest launcher；有覆盖时只实例化声明入口。
- 声明启动作用域但 required manifest 缺失时，在 Activity 构造前失败。
- launch activity 不在 DEX 时明确失败。

## 结果（已完成）

- `ResolveProfileLaunchDescriptor` 实现 override-first、manifest fallback 和双缺失失败。
- `run-apk` 在窗口/Activity 前验证 external mount 与 required manifest，再把入口 descriptor
  交给 `DexActivityLifecycle`；DEX 不含入口时由真实 linker 明确失败。
- A6 exact 日志证明实际启动 `GLGame` 且没有进入 Manifest installer Activity。
