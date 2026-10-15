# 子模块：frontend/cli

CLI 薄入口，只转发 UTF-8 参数到 ogplaycore 的 C ABI；Windows 以显式宿主回调接收
hal/windows 启动适配转换后的参数。命令实现归 frontend/app，运行实现归 frontend/game。
本目录不链接或初始化内核、SDL、GUI，不保留旧 ogplay CLI/GUI 子命令。
