# DVM-15 · JNI 入向第三路由（native→解释器）+ resources.arsc 读取

## 目标（一句话）

把全部解释类/方法注册进 session 的 `JniClassRegistry`（impl id
`dexvm.m<id>`），native 侧 FindClass/GetStaticMethodID/CallStatic* 经不变的
233 槽 ABI 解析到真实 DEX 事实并落入解释执行；并交付
`loader/arsc.cpp`——`Resources.getIdentifier`/`openRawResource`/
`SoundPool.load(resid)` 的诚实资源表后端。

## 依赖

- DVM-14（同一桥）；设计 04 §1 第三路由
- pilot 实证：`SoundPool.load(context, 0x7f040009 + n, 1)` 使用编译期资源
  id——必须解析 resources.arsc 才能诚实映射到 `res/raw/*.ogg`

## 变更

- `DexVmGuestBridge::RegisterDexClasses`：拓扑序注册解释类声明（含 super
  链内的 dex 类）；每个解释方法一个 handler，实参 JniValue→VmValue、返回
  按 shorty 反转；解释器未捕获异常按 JNI 语义置为调用方 pending exception。
- `loader/arsc.{h,cpp}`：严格 ResTable 解析（table/string pool UTF-8+UTF-16/
  package/type spec/type/entry/Res_value TYPE_STRING），产出
  resid ↔ (type, name, 文件路径)；默认配置优先，复杂值与多 locale 明确
  不在范围；截断/越界即失败。
- `tests/dexvm/arsc_tests.cpp`：合成最小表（id/name/path 三向查询 +
  截断拒绝）+ 本地 exact APK 交叉验证（0x7f040009 → raw/raw_000 →
  `res/raw/raw_000.ogg`，恰与 v1 profile 的人工 pattern 一致——机器事实
  取代人工声明的直接证据）。

## 验收（机器可判定，已过）

- `arsc` 3 用例/18 断言全绿（含本地 exact 交叉验证）。
