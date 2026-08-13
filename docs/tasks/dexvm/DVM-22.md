# DVM-22 · v2-only 启动作用域 schema

## 目标（一句话）

固化 ADR-0022，并让 C++/Python/JSON 三重门禁只接受带受检 entry/preset 的 Profile v2。

## 验收

- schema 1、`native_call`、`[[java.class]]` 均在加载期拒绝。
- `launch_activity`、preset 的类/字段/type/value/reason 为闭合 schema。
- 引用 preset、类型不匹配、空 reason 和未知字段均有机器负例。

## 结果（已完成）

- `title-profile-v2.schema.json`、Python validator 与 C++ loader 只接受 schema 2。
- `[runtime.entry]` 与 `[[runtime.presets]]` 的名称、类型、值、reason 和唯一性均受检；
  声明启动作用域必须同时有 required data manifest。
- schema 1、旧 replay 字段、引用类型、范围错误、缺 reason 和未知字段均有负例。
