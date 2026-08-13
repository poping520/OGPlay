# DVM-2 · dex_dependency_survey 题库静态测量工具

## 目标（一句话）

提供纯 Python 的题库静态测量：应用类规模、Java 厚度、平台类/方法引用直方图、
native 方法清单、引擎指纹与存量 profile impl id 语料——校准 intrinsic 最小集。

## 依赖

- DVM-1（基线已 vendor，结构布局对照 `libdex/DexFile.h`）
- 设计：`docs/design/dexvm/06-migration.md` 阶段 0-1、`03-platform-intrinsics.md` §3

## 变更

- `tools/dex_survey_lib.py`：严格 DEX 035 只读解析（header/string/type/proto/
  field/method/class_def/class_data/code 元信息、MUTF-8、uleb128），越界即失败。
- `tools/dex_dependency_survey.py`：`--apk`（可多个）+ `--profiles` 产出确定性
  JSON 报告；厚度阈值与 `loader/dex_analysis.cpp` 同口径；`--self-test` 用内存
  合成的最小合法 dex 验证解析与分类。
- CTest：`tools.dex_dependency_survey_self_test`。
- `capabilities.toml` 新增 `tools.dex_dependency_survey`。

## 本地题库测量结论（报告在 `.local/dexvm/survey.json`，不提交）

| title | 应用类 | 厚度 | native | 平台类引用 | 平台方法引用 |
| --- | --- | --- | --- | --- | --- |
| pilot（Asphalt 5） | 22 | moderate | 28 | 46 | 148 |
| Dungeon Hunter | 111 | thick | 16 | 135 | 492 |
| Tales From Deep Space | 3637 | thick | 69 | 487 | 2401 |

pilot 的平台面有界且集中在 Activity/GLSurfaceView/SoundPool/MediaPlayer/
集合/String/IO；Sms/Wifi/Http/Sensor 引用存在但不在主界面路径。存量 impl id
语料 98 项。批次顺序按此校准。

## 验收（机器可判定）

- `ctest --preset dev -R tools.dex_dependency_survey` 通过。
- 对本地三个 exact APK 产出确定性 JSON 报告（本地验证，不进 CI）。
