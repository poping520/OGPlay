# ADR-0029 · DexVM 宿主状态三类所有权

- 状态：Accepted
- 日期：2026-08-29
- 关联：[DVM-95](../tasks/dexvm/DVM-95.md)

## 背景

以裸 handle 为 key 的 Android side map 曾靠枚举全部 key 为 GC root 避免悬挂。这会让 owner
永久存活，child edge 与真正 process root 混在一起，并在句柄复用时存在继承旧状态风险。

## 决定

宿主状态分为三类：

1. session/process root：Application、当前 Activity/Intent、main Looper、scheduler work 等，
   由 session root 明确枚举；
2. owner-attached state：只接受 `VmObjectRef` owner，注册具名 trace/sweep 和可选 clone；owner
   被标记时 trace child，owner 死亡时在 handle 回收前 sweep，禁止 state 反向保活 owner；
3. 非对象 identity：thread token、UiNodeId、resource id、路径和 process singleton，不放进
   owner table，按各自生命周期管理。

所有 owner state 最终经 `RegisterIntrinsicStateTable` 唯一入口进入 GC；无 sweep 不得注册，
声明含 guest reference 的 policy 无 trace hook 时构造失败。clone 默认关闭。

## 后果

session root 不再枚举 owner-map key。死亡 owner 的 child 仅由该 state 持有时可同步回收，句柄
复用不会看到旧状态。新增 API family 必须先选择上述所有权类别，不能用兼容 root 绕过。
