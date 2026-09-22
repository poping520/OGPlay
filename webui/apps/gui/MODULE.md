# 模块：Web 启动器

Preact 游戏库与导入向导，只显示宿主返回的事实，经 closed-schema JSON-RPC 操作宿主。
不执行 guest，不判断游戏兼容性。以 installation id 区分实例，不能按包名合并。

导入只接受单体 APK。原生选择器返回宿主路径；拖放读取 File 并以 192 KiB 顺序分块传输，
不构造虚假文件路径。分析与入库使用宿主 job，终态后刷新并选中新实例；重复包名确认新建。
原生选择器/后台操作未结束时不关闭向导、不允许重复请求。失败显式显示，可重新选择。
设置/Dashboard 未接入。主题位于 packages/ui-kit，产物由 Vite 生成，Node 仅构建期使用。

验证：npm run check，宿主 GUI CTest，以及 Windows 原生窗口交互。
