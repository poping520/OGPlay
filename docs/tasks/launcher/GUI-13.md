# GUI-13 · Profile catalog 失效状态

## 目标

让 Profile catalog 加载失败时所有非损坏磁贴 fail closed，不再短暂呈现为 ready。

## 依赖

- GUI-4：磁贴互斥状态与严格优先级。
- GUI-5：Profile catalog 加载和 required-external 摘要。

## 结果

- `LibraryViewContext` 携带可选的 catalog 错误事实；错误存在时磁贴显示
  “Profile 不可用”，详情保留根因并引导用户打开设置或恢复内置数据。
- 状态优先级固定为：条目损坏、catalog 不可用、缺 Profile、缺数据包、运行中、ready。
- catalog 不可用时 `ExternalRequiredPackages` 虽无法给出数据包事实，但磁贴不会因此
  降级为 ready，点击只进入明确的不可启动提示。

## 验收

`gui_view_model_tests` 机器断言损坏状态仍优先，其他条目即使同时处于运行集合也显示
catalog 不可用且带下一步；Windows/MSVC `/W4 /WX` 构建和全量 CTest 通过。
