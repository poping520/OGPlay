# GUI-17 · 主视图选择与运行条件模型

## 目标

为双栏主界面提供不依赖 ImGui 的内存选择、详情和启动条件模型，并让系统库未配置时
不再把条目呈现为可启动。

## 依赖

- GUI-13：Profile catalog 失效状态与 fail-closed 优先级。
- GUI-15：严格 LibraryStore/GuiConfig 持久模型。

## 结果

- `LibrarySelection` 首次选择排序首项，刷新保留 package，删除后按原索引选择相邻项，
  空库清空；选择不进入磁盘 schema。
- `LibraryDetail` 只组合 LibraryEntry、Profile/external 注入事实和系统目录状态，发布
  版本、三项运行条件与 launch/delete 可用性；不推断 API、ABI 或兼容性等级。
- 磁贴状态优先级扩展为损坏、catalog 不可用、缺 Profile、缺数据包、运行中、需要设置、
  ready；系统库未设置/失效明确引导设置。
- 外部数据明确区分不需要、已就绪、缺失和无法判断；损坏条目不访问缺失 metadata，
  仍允许删除。
- 缺 Profile 保留橙色提示，但在 system 目录有效且条目未运行时允许通用启动；system
  未配置仍独立阻止启动，external 保持无法判断。

## 验收

Windows/MSVC Release 构建 `ogplay_tests`；选择、详情、系统目录、外部数据四态及既有
状态优先级定向测试通过。
