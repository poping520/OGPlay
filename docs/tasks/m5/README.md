# M5 Work Unit 拆分索引

M5 共包含 `WU-0199..0327`，合计 129 个已创建 Work Unit。历史任务创建后不移动、不重编号；
本索引只把它们拆成三个可独立追溯的交付批次，降低后续阅读和验收的上下文规模。

| 批次 | 编号范围 | 数量 | 主题 | 主要机器证据 |
| --- | --- | ---: | --- | --- |
| M5-A | WU-0199..0223 | 25 | Profile/Quirk schema、装配、APK 身份、Bionic 闭包与启动 preflight | schema self-test、session/loader tests、exact preflight |
| M5-B | WU-0224..0276 | 53 | 首个 exact Profile、native-call/A32/JNI guest ABI、GLES1 bring-up | runtime/session tests、ANGLE CTest、bounded exact smoke |
| M5-C | WU-0277..0327 | 51 | SoundPool/SDL 音频、输入与呈现、第二 Profile、性能、退出与 suspend/resume | audio/input/runtime tests、exact frame/sound/lifecycle smoke |

边界按依赖闭环划分，不表示批次内每个能力都达到 `complete`。`capabilities.toml` 仍是能力
现状的唯一机器账本，M5 是否正式完成由后续 `M5-ACCEPTANCE.md` 汇总证据后判定。

M5 的新能力 WU 冻结在 `WU-0327`。发现属于历史 M5 范围的缺口时，应先判断它是否是
M6 自动化所需基础或 M8 兼容性题库暴露的通用缺陷，再放入对应新里程碑，不回填 M5 编号。
