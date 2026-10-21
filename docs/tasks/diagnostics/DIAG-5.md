# DIAG-5 · WU4 survey 与 API19 骨架共存

状态：实现及定向验证完成；试运行数据见下。依赖 DVM-253/254（骨架及 JNI/反射通道）、ADR-0147/0148、
`run-apk --survey-gaps`。不实现任何被命中成员的行为，不改变非 survey 语义，不放宽 BootDex 准入。

## 问题

骨架合并后，survey 打开时骨架整体不生效（`ResolveDescriptor`/`MergeSkeletonMembers` 绕过），常量变中性值、
类层次变假，survey 运行与正常运行不一致；正常模式未实现调用直接失败，每次只得到一个首错。

## 语义

- survey 保留骨架：类层次、常量、成员声明与正常模式一致（Class.forName 对骨架类同样可物化）。
- 中性值（0/null/void）只用于**被调用的未实现方法/构造器**（骨架、handwritten intrinsic、BootDex
  类内声明；含 JNI `CallXMethod`、反射 `invoke/newInstance`）与**被访问的非恒定未实现字段**
  （解释器两后端、JNI Get/Set、反射 get/set；读取落在零初始化槽位，写入照常存槽）。
  每次命中仍写原有 ledger（`dexvm.skeleton.field.*`、`dexvm.intrinsic.*`）。
- API19 不存在的方法/类沿用既有合成中性桩（来源 `missing`）；JNI/反射对不存在成员的查找语义不变（NULL/NoSuch*）。
- survey 结果永远不是兼容性证据：中性返回后的行为可能已偏离，首次命中之后的记录只说明“碰到了”。

## 记录（JSON schema 2，仍带 `"survey": true`）

`missing_members`（按次数降序，旧工具兼容）每项：`class`、`member`（方法 `name(desc)ret`，字段 `name:desc`）、
`kind`、`source`（skeleton/intrinsic/bootdex/missing，按声明方归属：BootDex 类内 > 生成骨架 > 手写 intrinsic）、
`channel`（首次命中通道）+`channel_hits{java,jni,reflection}`、`first_hit_index`（命中流中的序号）、
`first_uptime_ms`（guest uptime Clock）、`hits`、`first_caller_bootdex`、`first_stack`（首次命中 guest 栈前 5 帧）。
`members_by_first_hit` 给出同一批成员按首次命中顺序（越靠前越可能是正常运行的真实首错）。
summary 带来源/通道分布。`tools/dexvm_stub_gen.py --survey` 只取 `source=missing` 的方法。

## 实现点

`GapSurveyHit/Context`（class_linker.h）；`DexClassLinker::RecordGapSurveyHit`；解释器 `RecordSurveyHit`/
`RequireImplementedField(field, channel)`；`GapSurveyChannelScope`（线程局部，仅被紧随其后的顶层 `Call` 消费，
嵌套 guest 调用回到 java）由 JNI bridge 与反射在直接调用处设置；`run-apk` 在拆除前先写一次 JSON
（macOS 退出可能卡在 FMOD/AudioTrack），拆除后再写最终版。

## 验证

- 新增定向用例：`gap survey*`（skeleton_surface_tests、gap_survey_tests）与
  `gap survey records JNI-channel…`（jni_bridge_objects_tests）。覆盖：survey 下常量为真实值、层级正确；
  骨架方法/构造器/虚方法、骨架字段读写、handwritten 未实现方法、missing 合成桩、JNI 通道命中均记账并返回中性值；
  两个解释器后端；通道只归属直接调用；各用例含“关闭 survey 即失败”的对照。
- 既有 `gap_survey_tests` 通过。同过滤器中另有 17 个用例失败，均为本任务之前的基线问题
  （BootDex 全量链接 `Activity.nativeFinish` 未绑定、DVM-103/DVM-254 旧断言与 ADR-0148 准入后的 BootDex/
  PointerCoords 状态不符），与 survey 无关，未修。

## 试运行（macOS，临时沙盒，--exit-after-frames 600；证据与汇总 `.local/analysis/wu4/SUMMARY.md`）

静态基准为 wu3 32 APK 预检的 6,524 项 distinct declared-unimplemented。

- 要求的三次空闲运行：TML、PVZ、Dead Trigger 均 **0 命中**（分别静态项 156/96/199）。
- 带输入补充：TML 3 次点击命中 3 个成员/8 次（全部 skeleton、java 通道）——`View.dispatchTouchEvent`（首错）、
  `WindowManager$LayoutParams.screenBrightness` 字段、`Window.setAttributes`；占 TML 静态项 3/156=1.9%。
  PVZ 4 次点击 0 命中。
- 其他 5 款库内游戏空闲 600 帧合计 2 个成员（WMW、Fruit Ninja 各 1，均 startup 致命失败前）。
- 全部运行共 5 个不同成员，全在静态 6,524 项内，占 0.08%；来源 100% skeleton，jni/reflection 无真实命中。
- 结论：静态缺口中实际被走到的比例极低，UI 类缺口集中在输入之后；空闲运行样本小，不能外推整体可达比例。
- 同时修正 `run-apk`：startup 失败与拆除前先写 survey JSON（macOS 拆除可能卡在 FMOD，丢失整份产物）。
