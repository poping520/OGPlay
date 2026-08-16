# M10 · Layout UI

M10 按 `docs/design/layout-ui/` 实施有界 Android 4.4 XML UI。依赖方向固定为
`loader/core → runtime/ui → runtime/integration/dexvm_android → session/frontend`，
真实 title 只作为 scenario/evidence，不进入生产代码分支。

| Work Unit | 设计追踪 | 状态 | 目标 |
| --- | --- | --- | --- |
| [WU-M10-001](WU-M10-001.md) | LUI-1 | 完成 | Generic compiled AXML |
| [WU-M10-002](WU-M10-002.md) | LUI-2 | 完成 | UiTree 地基 |
| [WU-M10-003](WU-M10-003.md) | LUI-3 | 完成 | DexVM View binding |
| [WU-M10-004](WU-M10-004.md) | LUI-4 | 完成 | Inflater 与 merge |
| [WU-M10-005](WU-M10-005.md) | LUI-5 | 完成 | MeasureSpec + FrameLayout |
| [WU-M10-006](WU-M10-006.md) | LUI-6 | 完成 | Horizontal LinearLayout + visibility |
| [WU-M10-007](WU-M10-007.md) | LUI-7 | 完成 | Bitmap UI renderer |
| [WU-M10-008](WU-M10-008.md) | LUI-8 | 完成 | Present composition |
| [WU-M10-009](WU-M10-009.md) | LUI-9 | 完成 | Generic UI pointer dispatch |
| WU-M10-010 | LUI-10 | 待办 | Asphalt 6 P0 exact gate |
| WU-M10-011 | LUI-11 | 待办 | 通用 LinearLayout + 动态 hierarchy |
| WU-M10-012 | LUI-12 | 待办 | TextView / Button |
| WU-M10-013 | LUI-13 | 待办 | RelativeLayout 核心规则 |
| WU-M10-014 | LUI-14 | 待办 | include / scale / resources |
| WU-M10-015 | LUI-15 | 待办 | 多 title 收敛与遗留清理 |

每个 WU 单独提交，完成时同步任务单、模块契约、能力账本与当前状态。
