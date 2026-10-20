# DIAG-4 · WU1 静态兼容体检与历史回测

状态：限定边界内完成；动态 JNI/可达性保留 partial。依赖：真实 core/Android intrinsic 绑定、DexClassLinker、curated BootDex、
API19 ROM JAR、dex_survey_lib、BoundaryCatalog 与 ELF/DT_NEEDED 规则；不依赖游戏运行。

## 范围与验收边界

- CLI 导出注册后实际链接表面、来源、层级、字段/常量与方法绑定状态；不执行 clinit/游戏。
  保留链接失败和无法静态核实的动态 guest JNI 状态，不以声明代替行为兼容结论。
- 参考表面从固定 core/framework/framework2/ext JAR 提取 public/protected 成员与常量；
  本地产物保留来源哈希；原源码提取模式继续可用。
- 体检 APK 全部 DEX 引用/应用父类型闭包、ARM SO 的 JNI 字符串启发式/可识别注册表及
  DT_NEEDED/强弱符号；区分 API19 缺失、非 API19 探测与已声明未绑定；不生成桩。
- 全题库频次与引擎分组只存 `.local/analysis/preflight/`。
- 4034054b 独立 worktree 构建仅所需目标并回测 TML；结束删除 worktree。
  再抽取 10 个 high/medium M1–M3 单元，优先旧版本；成本过高时显式声明移除模拟。
- 定向导出测试、scanner/API19 自测、capabilities check；不跑全量测试，不按结果修缺口。
  BACKTEST.md ≤80 行，报告命中、总噪声及来源/算法的误报漏报边界。

## 结果（2026-10-20，macOS）

- 新命令 `ogplay-cli dexvm dump-surface [--bootdex <jar>]` 及独立 JSON 扫描器已交付；
  不执行 guest/类初始化。完整字段/参数/边界见对应 MODULE 与 tools/README。
- Release 只构建 ogplay-cli、ogplay_app_tests；导出定向 1 用例/11 断言通过，
  scanner/API19 自测、capabilities check 与差异/UTF-8 静态检查通过，未跑全量测试。
- 4034054b 独立 worktree 仅移植导出机制，原 BootDex 重建与该提交 manifest 的
  JAR/DEX/来源哈希逐项一致；TML 三项 Java/JNI 形状首错 3/3 命中，两个 syscall 参数
  缺口按预期未命中，原始盘点 427 条（16 条 weak、0 条强符号缺失）。工作树已删除。
- 从历史 55 个 high/medium M1–M3 单元固定种子抽 10 条，不重抽：7 个 HEAD 表面 JSON
  声明消融命中；3 个实际为非缺声明问题，保留漏报。总体 7/10；平均非目标项 2034.4。
  这不是十个历史二进制回测，完整方法/偏差见 `.local/analysis/preflight/BACKTEST.md`。
- `.local/games` 32 个 APK 全部扫描完成，无解析不完整；逐 APK 及类/成员/引擎频次均留本地。
  不由缺口清单推断可达/可玩；未修扫描出的缺口，未生成任何运行时声明/桩。
- 动态 guest JNI admission 不等于后端已实现，指针表 owner 未恢复；静态配对仅为启发式。
  不覆盖 syscall 参数、动态名称、完整 BootDex 调用图与资源中 SO，能力账本保留 partial。
