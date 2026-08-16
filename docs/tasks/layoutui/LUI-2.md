# LUI-2 · UiTree 地基

## 目标（一句话）

建立唯一的 View hierarchy、id、visibility、layout params、geometry 与 dirty 状态容器。

## 依赖

- LUI-1（generic typed AXML，可并行但已完成）。

## 范围

- 新增 `runtime/ui` module、公共 `UiTree`/`UiNodeId`/`UiNode` API 与模块索引。
- synthetic content root、稳定 child order、attach/detach/destroy、id index。
- visibility/enabled/clickable、layout/draw dirty propagation、generation reset。
- 4096 node、1024 child、128 depth 的明确资源上限。

## 验收

- pure unit tests 覆盖 hierarchy 顺序、id 查找/更新、三态 visibility、dirty propagation。
- detach/destroy 后不再 find；reset 后旧 node 不可访问。
- cycle、重复 attach、root detach 明确失败。

## 实施结果

- `runtime/ui` 不依赖 DexVM/session/video，content root 及 live node 身份带 generation。
- hierarchy/state/layout/geometry 字段集中到唯一 `UiNode`；后续 WU 在同一事实上实现算法。
