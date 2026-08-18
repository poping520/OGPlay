# Layout UI · 有界 Android XML UI

本专项按 `docs/design/layout-ui/` 实施有界 Android 4.4 XML UI。依赖方向固定为
`loader/core → runtime/ui → runtime/integration/dexvm_android → session/frontend`，
真实 title 只作为 scenario/evidence，不进入生产代码分支。

| Work Unit | 状态 | 目标 |
| --- | --- | --- |
| [LUI-1](LUI-1.md) | 完成 | Generic compiled AXML |
| [LUI-2](LUI-2.md) | 完成 | UiTree 地基 |
| [LUI-3](LUI-3.md) | 完成 | DexVM View binding |
| [LUI-4](LUI-4.md) | 完成 | Inflater 与 merge |
| [LUI-5](LUI-5.md) | 完成 | MeasureSpec + FrameLayout |
| [LUI-6](LUI-6.md) | 完成 | Horizontal LinearLayout + visibility |
| [LUI-7](LUI-7.md) | 完成 | Bitmap UI renderer |
| [LUI-8](LUI-8.md) | 完成 | Present composition |
| [LUI-9](LUI-9.md) | 完成 | Generic UI pointer dispatch |
| [LUI-10](LUI-10.md) | 完成 | Asphalt 6 P0 exact gate |
| [LUI-11](LUI-11.md) | 完成 | 通用 LinearLayout + 动态 hierarchy |
| [LUI-12](LUI-12.md) | 完成 | TextView / Button |
| [LUI-13](LUI-13.md) | 完成 | RelativeLayout 核心规则 |
| [LUI-14](LUI-14.md) | 完成 | include / scale / resources |
| [LUI-15](LUI-15.md) | 完成 | 多 title 收敛与遗留清理 |
| [LUI-16](LUI-16.md) | 完成 | dirty/cache 与 pointer dispatch 验收收口 |
| [LUI-17](LUI-17.md) | 完成 | Content View 触摸分发与手势捕获 |

每个 WU 单独提交，完成时同步任务单、模块契约、能力账本与当前状态。
