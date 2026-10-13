# INP-2 · 操控配置数据与分级合并

状态：完成。依赖：INP-1。设计：[Input 设计](../../design/input/README.md) 第 5、8 节。

范围：Profile `[input]`（template、zone、binding）schema 与 `tools/validate_title_profiles.py`；
`data/input/templates/` 类型模板及校验；session 装载并按“内置默认 → 模板 → Profile →
用户覆盖”合并为 `ControlConfig`；用户覆盖为安装实例目录中的独立文件，不并入 GUI
`settings.toml`。替代未使用的 `input.profile` 字段。

验收：合并、显式解除绑定、同模式源冲突与缺区域的明确失败均有测试；现有 Profile 与
Scenario 校验通过。

实现：`session/control_config` 统一解码三类来源并合并；Profile `[input]` 改为该结构，
旧 `input.profile` 与 `ApplyProfileInput` 移除；首个类型模板 `stick_action`；模板目录随
运行时数据暂存与安装。

验证（2026-10-13，macOS dev 构建）：`tests/session/control_config_tests.cpp` 4 个用例通过
（全部内置模板可构建、合并/解绑/用户覆盖、错误路径）；`validate_title_profiles.py`
自测与现有 Profile 目录校验通过。同批运行 session/input 用例时
`profile_entry_scope_tests.cpp` 有 6 个用例失败，断言 DexVM 平台目录的方法数与字段值，
与本任务改动的文件无关，未在改动前基线复验。
