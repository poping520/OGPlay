# BND-50：ATC 压缩纹理回退

状态：实现与 macOS 定向验证完成；Windows/Linux 与 title gate 未验收。依赖：现有 ANGLE 上传、受检 guest 搬运与 buffer 回读。
设计：[ADR-0091](../../adr/media.md#adr-0091)。

范围：三种 ATC 格式的可移植 CPU 解码及 RGBA8 上传；GLES1/2 共用，ES3 PBO 复用
同一解码器。扩展与压缩格式计数/数组查询同步；4×4 块、部分块、mipmap/cube face、
数据长度、大小溢出与解码预算受检。按 ATC 扩展拒绝非法子图更新。
不修改 APK、不增加游戏分支，不以 ATC 修复承诺全游戏兼容或完整 ES3 扩展。

验收：上游固定版本参考向量覆盖颜色模式与 alpha 模式；关闭回退即失败的真实采样、
查询计数/输出长度/非法参数与 guest/PBO 路径回归；原 APK 同路径记录首错消失和下一阻塞。
Windows/Linux/macOS 实跑分开记账，未实跑不得宣称平台验收完成。

验证：Release ogplay/ogplay_tests 构建通过。定向 14 用例/1706 断言通过：ATC 原版参考
向量、ES2/3 实际采样、cube/mip、guest/PBO、格式查询边界与非法更新，复用 ETC1/PVRTC
及 buffer/query 回归。原 APK 无 Profile/survey、隔离沙盒 7.4 秒退出 1，无超时，
原纹理空读消失；下一首错为 WifiLock.setReferenceCounted 方法解析缺口。此项只作为
reached-fault，不宣称黑屏或游戏验收完成。证据 `.local/atc-fix/`。

架构 hot-path 检查被未改动的 android_module.cpp 中既有 std::function 阻断；该文件
与 HEAD 字节相同。Windows/Linux 未实跑，未修改该独立问题或用户的 third_party/imgui。
