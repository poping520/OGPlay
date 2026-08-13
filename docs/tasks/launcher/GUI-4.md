# GUI-4 · 游戏库网格与 CJK 字体

## 目标

把 GUI-2 的持久游戏库接入真实 ImGui 窗口，交付图标/占位磁贴、确定性名称排序、
互斥状态角标、空库引导和宿主 CJK 字体回退。

## 依赖

- GUI-1：SDL3 + ANGLE + ImGui shell。
- GUI-2：`LibraryStore`、严格 meta 和损坏条目事实。

## 结果

- `BuildLibraryTiles` 只消费库条目及注入的 running/required-external 事实，严格按
  损坏→缺 Profile→缺数据包→运行中→ready 派生状态并按名称、package 排序。
- shell 启动时读取游戏库，将缓存 PNG 上传 GLES texture；缺失/损坏图标使用内置
  占位磁贴。名称单行省略，悬停显示全名与状态原因。
- 空库显示引导；导入、设置和启动交互在对应后续 WU 接线前保持禁用，不伪造行为。
- Windows/macOS/Linux 固定字体候选不使用平台分支；加载 CJK 常用区失败时保留
  ImGui ASCII 字体并写结构化 warning。
- GUI-5 提供 Profile required-external 事实后只需填充 `LibraryViewContext`，无需改角标。

## 验收

Windows/MSVC `/W4 /WX` 构建；磁贴模型 3/3；真实空库与 CJK 非空库三帧冒烟；
完整 CTest 682/682。
