# WU-HLE-02 · ID-based dispatch

目标：把正常 Android boundary 执行链改为 `PC → descriptor → route/function id → handler`，
移除热路径上的 module/symbol 字符串比较和 `FindGlesFunction(symbol)`。

验收：

- [x] Android、EGL、GLES1、GLES1 extension、GLES2 与 log 按 descriptor route 分派。
- [x] GLES1/2 handler 全链路接收生成目录的 function id。
- [x] 字符串只用于兼容入口、错误信息、查询与 trace，不参与正常 handler 选择。
- [x] descriptor 测试锁定 Android/GLES1/GLES2 的代表性 route/id。
- [ ] HLE 阶段统一构建与测试。
