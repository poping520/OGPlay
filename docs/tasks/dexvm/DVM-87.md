# DVM-87 · Java util/regex/concurrent P1 核心

## 目标（一句话）

发布 API 19 常用 Arrays/Collections 算法、固定时区 Calendar、Pattern/Matcher，及复用
`VmThreadRuntime` 的 Future/Executor/atomic 核心能力。

## 依赖

- DVM-80：intrinsic family TU、core ownership 与 catalog/layout gate。
- DVM-85：`VmThreadRuntime`、统一 Clock 与调度生命周期。
- [`12-api19-capability-stack.md`](../../design/dexvm/12-api19-capability-stack.md) Phase F。

## 范围

- Arrays 覆盖 primitive sort/range sort/binarySearch/fill/equals，以及 Object array
  sort/binarySearch/fill/equals/asList；Collections 覆盖 sort/reverse/swap/fill/frequency/
  binarySearch，并通过 guest Comparable/Comparator、List/Collection 虚派发工作。
- TimeZone/SimpleTimeZone 支持 GMT/UTC 固定 offset；Calendar/GregorianCalendar 读取注入
  Clock，提供常用 field、millis、zone、set/clear/add 语义。
- Pattern/Matcher 提供 String 输入上的 compile/matches/find/reset/start/end/group/replace 与
  quote；语法由受检 ECMAScript regex backend 承载，非法 pattern/flag 明确失败。
- FutureTask、single-thread ExecutorService、Callable/RunnableFuture/Future interface 与
  AtomicInteger/Long/Boolean/Reference；worker 复用真实 guest Thread，连续提交串行化。
- 在独立 `java_text.cpp` family 按 API 19 层级发布 `java.text.Format`、`DateFormat`、
  `SimpleDateFormat` 最小表面；`SimpleDateFormat(String, Locale)` 按 API 19 校验 null、模式
  字母与引号并保存私有 pattern，不发布格式化/解析行为。
- 按用户授权，本 WU 不受通常 10 文件与 800 行限制。

## 不做

- 不提供 locale/DST 时区数据库、Calendar week/roll/完整 field 状态机。
- 不实现 `java.text` 日期格式化、解析、pattern、locale、symbols 或时区行为。
- 不承诺 Android libcore regex 的 Unicode/region/命名组完整等价。
- 不提供并行线程池、scheduled executor、timed Future.get、invokeAll/invokeAny 或并发集合。
- 不运行全量 CTest；全量回归留到本阶段最后一个 WU。

## 验收与结果

- primitive Arrays 排序/查找、Pattern find/group/replace、注入 Clock 与固定 offset 受检。
- FutureTask result、single-thread submit 和 atomic compare/update 受检。
- Windows `windows-msvc` Debug `ogplay_tests` 构建通过；DVM-87 4/4、Thread 相关 18/18、
  core catalog 1/1 与 architecture 6/6 定向测试通过；全量未运行。
- 后续补充 `SimpleDateFormat → DateFormat → Format` API 19 最小层级，并以声明 shape 测试
  锁定抽象标志、父类和 `Format` 的 Serializable/Cloneable 接口；指定 pattern/Locale
  构造器校验并保存 AOSP pattern，NumberFormat/Calendar/DateFormatSymbols 与 format/parse
  仍明确不在本次范围。

状态：已完成。
