# DIAG-10 · 加载层 Android 4.4 一致性审计

状态：WU11-1 审计与 WU11-2 五机制离线对齐验收完成（macOS，2026-10-23）。
实施任务：[DVM-269](../dexvm/DVM-269.md)。

## 审计结论

基点 `193dfc0f0cddcfcae405957ebff25fb112f972f0`；判据为本地 AOSP Android 4.4
framework/base、dalvik 和 libcore。原始证据：
`.local/analysis/wu11-1/REPORT.md`、`checks.json`、`apk-results.json`。

按显式拒绝源站点计数：SAME 125、STRICTER 108、SAFETY 41、OGPLAY-SCOPE 16，
共 290。复合 guard 含 STRICTER 子条件时归 STRICTER，不意味着可删除整个 guard。
静态审计不是运行兼容验收，也不证明放宽首错后没有其他运行缺口。

31 个 APK、27 个目录中，实际命中五个 STRICTER 站点，涉及六个 APK、五款游戏：

| 机制 | 主要站点（审计基点行号） | 已知命中链 |
| --- | --- | --- |
| metadata 类型 | apk_manifest.cpp:363 | NFS17 两版、SonicDash float |
| 资源属性 | apk_manifest.cpp:339 | SonicDash enabled 引用，须排除 v23 配置 |
| 重复组件 | apk_manifest.cpp:780 | Asphalt6 同名 receiver |
| 身份默认值 | apk_manifest.cpp:1011 | DeadSpace versionCode=0 |
| 参数注解数量 | dex.cpp:1324 | Shadowgun；SonicDash 清单首错之后的 DEX 后继 |

本单按这五个共享机制收拢全部相关 STRICTER 条目，不能只删上述五个 throw。
metadata 下游 Bundle 类型、资源选择、组件按名覆盖与过滤器去重、身份消费者及反射
数组形状均在对齐范围。SAME、SAFETY、OGPLAY-SCOPE 的真实失败继续保留。
Unicode descriptor 身份、资源/XML 惰性投影和其余机制不在本单范围。

## WU11-2 对齐结果

五个主题共对齐 15 个拒绝源站点：metadata 3、可资源化属性 5、重复组件 2、
身份默认值 4、参数注解 1。各站点 id、实际支持边界与下游语义见 DVM-269；
`.local/analysis/wu11-2/checks-after.json` 为完整审计副本，仅上述项标为 ALIGNED，
其余 93 个 STRICTER 及 SAME/SAFETY/OGPLAY-SCOPE 原样保留。
Unicode descriptor 身份、资源/XML 惰性投影与独立数值域校验等后续机制不在本单中修改。

基点 loader 114/114，候选 119/119；五个主题都有最小样例，metadata、重复组件、
参数注解恢复旧检查后各自失败，机器判据见 `negative-controls.json`。
下游 42 个主题用例仍为 41 通过、1 个既有失败，零新增失败；不宣称全量测试全绿。
31 个 APK 均经 library inspect 和只读 Manifest/DEX/class_data/ARM ELF probe 通过：
五款目标游戏六个 APK 解除了加载首错，其余 25 个 APK 加载结果与基点相同。
部分缺省 targetSdk 的 inspect 事实按 4.4 改为 minSdk 默认值，单独列出预期差异。
Shadowgun 的 DEX 首错以 probe 验证，未用不解析 DEX 的 inspect 冒充解除证据。
SonicDash 全部三份 DEX、四个 API19 enabled 引用与 float 位值均有明确断言。

初次 Dalvik 074-gc-thrash/switch 超时曾触发停止；编排方在沙盒外单独复跑三次全部通过
（11.22/11.28/11.25 秒），确认高负载噪声后授权继续。`accepted-baseline.json` 保存原始
报告与三轮证据哈希；基线文件和预算没有修改，也没有重跑整套基点。
按用户新增规则，后续单个 fail:timeout 必须独立三轮全部通过才能视为噪声。

最终 Dalvik：switch 85/91、threaded 84/91，唯一 074/threaded 超时按新增规则独立复跑
三次全通过（11.19/11.27/11.12 秒），记为噪声后双后端均 85/91、零持续回归；原始门禁
退出 1 与三轮通过证据都保留。JNI 双后端各 399/399，静态覆盖 233/233，退出 0、零回归。
所有证据放在 `.local/analysis/wu11-2/`。
未完整启动游戏、未执行 APK 的 guest 字节码或 git commit；加载通过不等于游戏兼容验收。
