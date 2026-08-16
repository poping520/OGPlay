# LUI-3 · DexVM View binding

## 目标（一句话）

让每个 live guest View intrinsic 与一个 UiNode 一一绑定，并让 id/visibility API 只读写
UiTree。

## 依赖

- LUI-2。

## AOSP 4.4.4 语义参考

- `View.getId/setId/findViewById`：NO_ID=-1，id 不要求 hierarchy 内唯一；find 按 hierarchy
  前序返回首个匹配，负 id 返回 null。
- `View.getVisibility/setVisibility`：VISIBLE/INVISIBLE/GONE 共享一份 View state。
- `Activity.findViewById`：查询当前 Window/content hierarchy。

## 范围

- guest object handle ↔ `UiNodeId` 双向 binding，重复/冲突绑定明确失败。
- `Activity.findViewById`、`View.getId/setId`、visibility 改读写 UiTree。
- click/touch listener map 以 UiNodeId 为 key；null listener 真实清除。
- View constructor 建 detached node；inflated View 建 node、binding 并接入 content root。
- 删除 `view_registry/widget_states` 权威 side table；`layout_views` 暂只保留 LUI-5/6 前的
  geometry adapter。

## 验收

- set id → find → getId exact，并保持同一 guest object identity。
- visibility round-trip 与 UiTree state exact；listener identity 不变。
- generation reset 清空双向 binding/listener。

## 实施结果

- id、visibility 与 listener 已迁到 UiTree/UiNodeId binding；旧两张 side table 删除。
- `setContentView(I)` 当前 inflater 同步建立 UiTree hierarchy，LUI-4 将继续提取 registry
  inflater 并严格处理 structural tag。
