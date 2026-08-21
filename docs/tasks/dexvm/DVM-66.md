# DVM-66 · Reflection invoke foundation

## 目标（一句话）

建立 Method/Constructor/Field/Array 共用的 API-19 reflection conversion/access 底座，
并闭合 `Method.invoke` 的真实 caller、分派、初始化、boxing 与 target throwable
identity 语义。

## 依赖

- DVM-62..65（linker metadata、ClassLoader facade、wrapper factory 与 Class query）
- [11 · Class、ClassLoader facade 与有界反射基础栈](../../design/dexvm/11-class-reflection-loader.md)
- 本地 API-19 Dalvik `reflect/Reflect.cpp::dvmConvertArgument`、
  `interp/Stack.cpp::dvmInvokeMethod`、`oo/AccessCheck.cpp` 与 libcore
  `Method.java` / `InvocationTargetException.java`

## 交付

- 新增唯一 `ReflectionCodec`：reference 使用 linker assignability，null 只可传
  reference，八种 primitive wrapper 按 API-19 widening matrix 转换，禁止 narrowing
  与 boolean/numeric 互转；返回值按真实 primitive 类型 boxing，void -> null。
- Interpreter 发布 intrinsic 外层的真实 interpreted caller class；reflection 不得把
  `Method` 或 host 固定为 caller。
- access check 覆盖 declaring class 可见性、public/private/package/protected、
  `(defining_loader, package)` runtime package identity 与 protected receiver restriction；
  per-wrapper `setAccessible(true)` 仅跳过该语言访问检查。
- `Method.invoke` 覆盖 null/wrong receiver、null/0/1/N args、direct/static/virtual/
  interface target selection、static/interface initialization、reference identity、wide 参数、
  primitive boxing 与 void -> null；switch/threaded 共用同一 runtime 路径。
- 新增 API-19 shape `InvocationTargetException`；target 抛出 E 时包装新异常，
  `getTargetException/getCause` 都指向原 E 的同一 `VmObjectRef`。class-init
  failure 在 target call 之前直接传播，不伪装成 target exception。
- Constructor/Class 实例化留给 DVM-67，Field/Array 操作留给 DVM-68。

## 验证与裁决

- `tests/dexvm/reflection_tests.cpp` 覆盖完整 primitive widening 正反矩阵、
  reference/null、virtual/interface/direct/static、0/1/N 参数、wide/boxing/void、
  switch/threaded 一致性、caller package/protected/private/accessibility 和 target identity。
- 只运行 DVM-66 新增用例及 reflection 邻接回归，不跑全量测试。
- 新能力记为 `dexvm.reflection_invoke = complete`。

状态：完成。
