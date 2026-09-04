# DVM-97 · 有界的 Intent/IntentFilter 匹配

## 目标

在一个 WU 内闭合 API 19 `LocalBroadcastManager` 实际调用的
`Intent` 读取链与 `IntentFilter.match`，并保持 ContentProvider、系统广播和隐式组件解析
在 OGPlay 边界之外。

## 依赖

- pinned AOSP 4.4.4 `Intent.java`、`IntentFilter.java`、`Uri.java`；
- support-v4 `LocalBroadcastManager.sendBroadcast()` 的 action/type/data/scheme/categories/flags
  调用链；
- DVM-73 的 scheme/authority 元数据与 DVM-95 owner-state GC sweep。

## 交付与边界

- `Intent` 保存 action、data、显式 MIME、categories 和 flags；构造器及
  `setAction/setData/setType/setDataAndType/addCategory/removeCategory/setFlags/addFlags`
  与 getter 形成同一状态，不再返回中性占位。
- `resolveTypeIfNeeded` 遵循 API 19：显式 component 只返回显式 type；否则优先显式 type，
  无 type 的 `content://` 才调用 `ContentResolver.getType`。OGPlay 没有 ContentProvider/Binder，
  该动态解析明确抛 `UnsupportedOperationException`，不猜 MIME。
- `Uri.parse` 的有界层只保存原字符串并解析常规 hierarchical URI 的 scheme/host/port/path；
  不实现 percent normalization、opaque/relative URI 完整语义或 builder。
- `IntentFilter` 支持保序去重的 action/category/type/scheme、DVM-73 authority，发布 API 19
  match 常量，并实现 action、MIME（exact、`major/*`、`*/*`）、scheme、host/wildcard/port、
  category 及标准负错误码匹配。
- 不实现 scheme-specific-part/path pattern、manifest resolver、系统/跨进程广播派发、
  implicit activity/service resolution、ContentProvider 或完整 `Intent` Parcelable/clone 面。

## 验收

- [x] action-only Intent 走通 support-v4 `LocalBroadcastManager` 使用的完整读取/匹配链；
- [x] MIME、scheme、authority wildcard/port 与 category 成功及四类 mismatch 受检；
- [x] flags、Uri getter、显式 type 和 explicit-component shortcut 受检；
- [x] 动态 content MIME 与 malformed MIME 分别明确抛边界异常；
- [x] owner-attached filter 元数据加入死亡 owner sweep；
- [x] Windows Release 受影响目标构建、Android value 与 catalog 定向测试通过；
- [x] PvZ Profile 实跑越过 `resolveTypeIfNeeded`，新首错固定为网络/SMS action 边界。

验证：`android_value_tests.cpp` 9/9、374 断言；`profile_entry_scope_tests.cpp` 30/30、
3989 断言；DVM-97 与相邻 IntentFilter 定向集合 6/6、3450 断言；
`architecture.dexvm_intrinsic_layout` 1/1。

状态：已完成。
