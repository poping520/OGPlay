# DVM-90 · 动态 SurfaceView attach/detach 生命周期

## 目标（一句话）

让运行期加入或移除 live View 树的 `SurfaceView` 按 Android 4.4.4 语义获得独立的
SurfaceHolder generation，并准确分派 created/changed/destroyed 回调。

## 依赖

- DVM-18：dex_activity 与 managed surface 生命周期。
- DVM-89：稳定 SurfaceHolder、多 callback 与软件 Canvas 发布。
- LUI-3/LUI-5：guest View identity、UiTree attach 与布局尺寸事实。

## AOSP 4.4.4 依据

- `.local/aosp/framework/base/core/java/android/view/ViewGroup.java`：`addViewInner()` 仅在父
  ViewGroup 已有 `mAttachInfo` 时调用子树 `dispatchAttachedToWindow()`；`removeViewInternal()`
  对已 attach child 调用 `dispatchDetachedFromWindow()`。
- `.local/aosp/framework/base/core/java/android/view/SurfaceView.java`：attach 注册 traversal
  listener；可见且拿到有效 Surface 后先置 `mSurfaceCreated=true` 并依次回调
  `surfaceCreated`、`surfaceChanged`；detach 令 requested visibility 为 false，经
  `updateWindow()` 在回调前清除 `mSurfaceCreated` 并调用 `surfaceDestroyed`。
- `SurfaceView.getSurfaceCallbacks()` 在一次事件前取得 callback 数组快照；
  `SurfaceHolder.addCallback/removeCallback` 修改后续快照，重复 add 去重、remove 幂等。

OGPlay 不复制 ViewRootImpl、WindowSession 或 SurfaceControl；其等价边界是 live UiTree、
唯一 managed host surface 和每个 holder 的 active-generation 状态。

## 范围

- context 明确记录 managed surface 是否存在，以及当前 active holder 闭集。
- lifecycle 初次 created 只激活已连接到 live UiTree 的 holder；changed/destroyed 只分派给
  active holder，重复创建或销毁不重复回调。
- `ViewGroup.addView()` 在 attach 后遍历新增子树；父树 live 时为其中已有 holder 的
  SurfaceView 同步分派 created→changed，父树 detached 时不伪造事件。
- `removeView/removeViews` 在 UiTree detach 前遍历子树，对 active holder 分派 destroyed。
- 运行期 `Activity.setContentView` 替换同样走旧子树 detach、新子树 attach。
- callback 分派使用稳定快照；发布 API 19 `removeCallback`，null/未注册移除为无操作。
- 保留 view→holder 和 callback guest identity；generation 状态只记 holder handle，不建立
  第二套 Surface 或像素存储。

## 不做

- 不引入 ViewRootImpl、WindowManagerService、Binder、SurfaceFlinger 或多宿主窗口 surface。
- 不模拟独立 SurfaceView 合成层、Z-order、transparent region、fixed-size/format 重建。
- visibility、window visibility、format/size mutation 导致的 generation 重建留待真实 reached
  行为；本 WU 只闭合 attach/detach 与已有 managed surface 尺寸事实。
- 不加入游戏名、包名、调用栈或 title 专属分支。

## 验收

- 初始 lifecycle 不向 detached SurfaceView 投递回调。
- live parent 动态 add 触发一次 created/changed，尺寸等于 managed surface；remove 触发一次
  destroyed，重复 generation 不串扰。
- 先在 detached parent 中构造子树不会提前回调；整个 parent 接入 live tree 后递归激活。
- `removeCallback` 幂等，且移除后不接收后续 destroyed。
- Windows Debug 增量构建及 SurfaceHolder/ViewGroup/架构定向测试通过；按要求不跑全量测试。

## 验证结果

- Windows Debug `ogplay`/`ogplay_tests` 增量构建通过。
- SurfaceHolder identity、初始 generation、动态子树 attach/detach、late callback 与既有
  ViewGroup mutation 定向 4/4、183 assertions 通过。
- architecture 定向 6/6 通过。
- Windows Release `ogplay` 增量构建通过；按给定 PVZ 命令复跑时，本次执行未到达动态
  `createView(true)` 的第二 holder，约 f=2351 先遇到 A32 native Thread-2 fault
  （PC `0x6045be18`）。因此该次运行不构成 title 进入游戏的验收，也不反证已由机器测试
  闭合的 attach/detach 能力。
- 按要求未执行全量测试，未提交代码。

状态：已完成（title 复跑受独立 native fault 阻断）。
