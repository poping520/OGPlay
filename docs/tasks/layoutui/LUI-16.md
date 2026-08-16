# LUI-16 · Layout UI 验收收口修复

## 目标（一句话）

修正 dirty/cache 与 pointer dispatch 的既有语义，使 Layout UI 已声明 complete 的能力通过正式验收。

## 边界

- 不新增 Widget/Layout/resource 能力，不修改 golden，不加入 title-specific runtime 分支。
- hit-test 继续只读 UiTree `screen_frame`；旧并行 bounds 事实源保持删除。

## 验收与结果

- `UiTree` 删除模糊 `ClearDirty`，拆为 `ClearLayoutDirty/ClearDrawDirty`：layout 只消费前者，
  renderer 只在 overlay 成功重建后消费后者。
- cache-level 回归锁定 TextView `A → BBBB` 经 layout 后 BuildCount +1 且输出更新，以及
  VISIBLE → GONE 后 BuildCount +1 且 child pixels 消失。
- gesture ownership 与 click eligibility 分离；真实 guest listener 计数覆盖 touch-only 与
  touch+click 的 true/false 四组合，另锁定 hidden/removed/UP-outside 取消 click。
- touch-only false 返回 Activity fallback，不调用不存在的 click listener；touch-only true 消费
  gesture；touch+click 只有 onTouch=false 且 UP-inside 时调用 click。
- macOS dev configure/build、full CTest 与关闭 survey 的 A5/A6 各三轮 exact scenario 通过；
  A6 `c85f6587a5ea55b519b5c8fa0cabca87a5589075f6b20bd85af35d3f2e454280`、
  A5 `9ee57323dae576c38d4d29984c067b5bceaa86f77724c8f3b174bcd1a81962b8`，
  两个既有 golden 未修改；full CTest 765/765。
