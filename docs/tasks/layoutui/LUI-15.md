# LUI-15 · 多 title 收敛与遗留清理

## 目标（一句话）

以真实旧 title 数据裁定 P1 最小能力集，并让 UiTree 成为唯一 UI hierarchy/geometry 事实源。

## 依赖

- LUI-10..14。

## 验收与结果

- 修复 `dexvm_gap_report.py` 对 DVM-32..38 `IntrinsicClassBuilder` 目录的发现与成员归属，
  并增加 CTest self-test；APK 派生报告只保存在 `.local/layout-ui-survey/`。
- 静态预检覆盖 Asphalt 5、Asphalt 6、Dungeon Hunter、Tales From Deep Space：分别含
  1/43/20/3 个 compiled layout；静态 UI 候选缺口分别为 1/102/43/117 个方法。后两者
  没有本 WU 的执行命中证据，因此不据此扩张 capability。
- 关闭 survey 的真实日志中，Asphalt 5 title-flow 与 Asphalt 6 video-skip 均无 UI
  unimplemented/unsupported/unresolved 命中；无需新增 P1 行为。
- Asphalt 6 三轮均在启动视频超过 3 秒后由屏幕点击显示 bottom-center Skip，exact PNG
  SHA-256 为 `c85f6587a5ea55b519b5c8fa0cabca87a5589075f6b20bd85af35d3f2e454280`；
  点击 Skip 后进入 sequence 81、无 guest fault、clean shutdown。该 checkpoint 位于历史
  `glFlush` 阻塞之前。
- Asphalt 5 三轮均通过 468/468000 exact title-flow，Main Menu SHA-256 为
  `9ee57323dae576c38d4d29984c067b5bceaa86f77724c8f3b174bcd1a81962b8`，无 fault、
  clean shutdown；未改写 golden。
- 删除 `LayoutViewFact`/`layout_views` 类型、存储、inflater 写入及测试手造 edge-row facts；
  click/touch hit-test 只读取 UiTree `screen_frame`。architecture test 禁止其回归，并禁止
  production runtime 出现已测 title/厂商身份。
- `cmake --preset dev`、`cmake --build --preset dev` 与 full CTest 通过。
