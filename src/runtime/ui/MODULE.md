# 子模块：runtime/ui

## 职责

持有有界 Android View/Layout 的宿主侧 hierarchy、state、resolved geometry、dirty、
render 与 hit-test 事实。本模块不执行 guest Java，不保存 DexVM object/listener，也不拥有
SDL、ANGLE 或视频解码。

## 公共 API

- `UiTree`：每 generation 建立唯一 synthetic `ContentRoot`；创建、按稳定顺序 attach、
  detach/destroy node，并以 android id 查找已接入 content root 的 node。
- `UiNode`：唯一保存 parent/children、class、android id（NO_ID=-1）、visibility、enabled/clickable、
  layout params、padding、measured/frame/screen frame、alpha 与 dirty state。
- `SetVisibility`：VISIBLE/INVISIBLE 只标 draw dirty；任意 GONE 转换同时从 node 到 root
  标 layout/draw dirty。
- `Reset`：推进 generation，销毁全部旧 node/id index 并创建新的 content root；旧
  `UiNodeId` 永不重新变为有效。

## 不变量

- hierarchy、android id、visibility、layout params 和 geometry 只有 UiTree 一份权威事实。
- renderer 与 input 后续只能读取同一 `screen_frame`；不得各自推导 bounds。
- 只有接入当前 content root 的 node 进入 id index；detach/destroy/reset 后立即不可查找。
- tree 最多 4096 node、每 parent 最多 1024 child、深度最多 128；超限或 cycle 明确失败。
- 本模块可依赖 core/loader 等下层事实，不得依赖 DexVM integration、session、frontend、
  SDL、ANGLE 或 video。

## 测试

`tests/runtime/ui_tree_tests.cpp` 锁定 hierarchy 顺序、id 更新、visibility dirty、detach、
destroy、generation reset 与非法 mutation。
