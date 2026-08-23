# DVM-78 · Java Util 集合核心、视图与迭代器

## 目标（一句话）

在一个 WU 内以统一、可 GC/clone 的集合运行时发布 API 19 常用 List、Map、Set、
Deque 类层级、活视图与 fail-fast 迭代器，并将 Java 类声明聚合进单一 family TU。

## 依赖

- DVM-49：具名 intrinsic side-table trace/sweep/clone hooks。
- DVM-57：switch/threaded 的 virtual/interface invoke 闭环。
- DVM-61：guest identity、virtual `equals/hashCode` 与 existing throwable 传播。
- pinned libcore：`.local/aosp/libcore/luni/src/main/java/java/util/`。

## 范围

### 类形状

- 接口：`Collection/List/Set/Map/Map.Entry/Iterator/ListIterator/Queue/Deque/`
  `Enumeration/Comparator/RandomAccess`。
- 抽象类：`AbstractCollection/AbstractList/AbstractSequentialList/AbstractSet/`
  `AbstractMap/AbstractQueue`。
- 具体类：`ArrayList/LinkedList/ArrayDeque/HashMap/LinkedHashMap/HashSet/`
  `LinkedHashSet`。
- 既有兼容类 `Vector/Stack/Hashtable` 迁移到同一状态底座；新增
  `ConcurrentModificationException`。

### 行为

- sequence 的构造、单项/批量增删改查、数组转换、sub-list、正反向 iterator。
- map 的 null-aware `put/get/remove`、copy、三类 live view、Entry 与 insertion/access
  order；set 复用 map-key 语义。
- `equals/hashCode` 通过 guest virtual dispatch，nested throwable 保留原 identity。
- structural `modCount`、iterator 状态机与 fail-fast。
- Queue/Deque 的 head/tail、offer/peek/poll、push/pop 常用面。
- side state 统一 trace/sweep/clone；容器 clone 浅拷贝 guest references。

## 文件组织

- Java 声明与薄 handlers 聚合到 `java_util_collections.cpp`，只暴露
  `AppendJavaUtilCollections()`；不按 Java 类新增 `.cpp`。
- 容器算法和 side state 放入 `CollectionRuntime`，handler 不保存宿主容器指针。
- 删除原 ArrayList/HashMap/List/Iterator/Vector/Stack/Hashtable 等分散集合 TU。

## 不做

- Tree/Sorted/Navigable、PriorityQueue、Weak/Identity/Enum map/set。
- concurrent collections、executor/lock、完整 `Collections`/`Arrays` 算法与 wrappers。
- serialization wire format、Android system service 或 title-specific 分支。
- 宿主容器容量、桶布局与 API 19 实现细节逐字节一致；guest 可观察语义必须一致。

## 验收（机器可判定）

- API 19 superclass/interface/method shape 与 application-defined abstract-base subclass 链接。
- switch/threaded 均覆盖 list/map/set/deque 主路径、null/index/capacity/load-factor 异常。
- guest override `equals/hashCode`、三类 map live view、Entry `setValue`、sub-list。
- iterator/listIterator 正反向修改、非法状态、耗尽与 concurrent modification。
- typed `toArray`、clone、GC trace/sweep；Vector/Stack/Hashtable 回归。
- Windows Debug 全目标、focused tests 与 architecture gate 通过。

PVZ 或其他 title 实跑不作为本 WU 验收条件。

## 结果（机器可判定，已达成）

- 新增统一 `CollectionRuntime`，sequence/map/sub-list、live view、Entry 与 iterator
  共用一个 trace/sweep/clone side-table；GC 127..130 与 clone 回归通过。
- `java_util_collections.cpp` 聚合本 WU 全部接口、抽象类、具体容器及三类集合异常；原
  ArrayList/List/Iterator/HashMap/Vector/Stack/Hashtable 等 10 个分散 TU 已删除。
- dexasm 集合主路径在 switch/threaded 均通过：ListIterator/sub-list、LinkedHashMap
  entrySet/values/keySet、ArrayDeque、fail-fast，以及 null/index/iterator exhaustion。
- Windows Debug 全目标构建通过；集合 focused、GC 4/4、architecture 5/5 通过。
- 全量 CTest 的本 WU 影响项均通过；剩余失败仅为进入本 WU 前已记录的 String catalog
  43/44 与 liblog tag 断言漂移。按范围约定未运行 PVZ 或其他 title gate。

状态：完成。
