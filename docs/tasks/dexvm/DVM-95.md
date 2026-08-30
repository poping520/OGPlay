# DVM-95 · Intrinsic owner state 与 GC 生命周期统一

## 目标

以 guest object 为 owner 的宿主状态不再通过兼容 key root 永久保活；owner 存活时只 trace
真实 child edge，owner 死亡时在句柄复用前 sweep。

## 交付

- 新增强类型 `OwnedStateTable<State>`：只接受 `VmObjectRef` owner，构造时必须声明唯一名称、
  trace policy 与 clone policy，自动生成标准 trace/sweep/clone hooks。
- Android value/database/scheduler/AudioTrack 延续唯一 state-table 注册入口；新增
  `android.owner-attached` 合并 Context/Intent/receiver、surface/view/listener、bitmap/canvas、
  media/video 等剩余 owner 生命周期 hooks。
- session root 只保留 process/lifecycle/scheduler/UiTree 等真实根，删除 owner-key map 的
  `key_root` 与 Bundle child 的全局反向保活。
- 所有权分类冻结于 ADR-0029。

## 验收

- [x] owner 存活 trace child、死亡 sweep、child 同步回收及句柄复用无旧状态测试。
- [x] 缺少 trace hook 的 traced table 注册前失败；不支持 clone 的 state 不复制。
- [x] GC、Bundle/Parcel、scheduler、AudioTrack、widget/video 定向回归通过。

状态：已完成。
