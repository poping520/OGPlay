# BND-49：EGL 配置事实与 RGB 表面

状态：实现与 macOS 定向验证完成；Windows/Linux 与 title 验收未完成。
依赖：既有 EGL registry、ANGLE 客户端表面、HAL 宿主存储。
设计：[ADR-0090](../../adr/media.md#adr-0090)。

范围：

- 删除 Java EGL10/14 根据 choose 最低颜色位数改写 config 的行为。
- 保留默认 RGBA 配置；有真实 backing 时发布独立 RGB888 配置。
  优先用 ANGLE 原生 RGB config；Metal 通过 HAL IOSurface + ANGLE GL_RGB 导入，
  由 ANGLE 处理无 alpha 的存储、混合和读回语义。配置属性与 context/surface 使用同一身份。
- 无 backing 时不发布 RGB；RGB565、完整驱动 config 枚举和手机窗口系统不在范围内。
- 关联 BND-48：clone 非正常 CPU stop 沿现有进程首错/notifier 发布；取消、预算和退出保留原语义。

验收：最低 RGB=4 不能改写实际 RGB=8；两配置属性稳定、容量计数、Java/native 查询一致；
RGB 表面实际 alpha bits=0、清屏读回 alpha=255、可交换出帧；clone 空读和非法指令保留
guest TID/PC/原始错误并触发进程退出。原 APK reached-fault 及跨平台实跑结果另行记录，
不将边界测试视为 title 验收。

验证：macOS Release ogplay/ogplay_tests 构建通过；定向 35 用例/1139 断言通过，包含
fast/slow 配置选择、RGB 真实 GL 查询/像素/交换、格式失配、Java EGL10/14 双解释器与
clone CPU fault。3 个额外 Metal read-surface/image 用例在恢复原 EGL module 的对照中
同样失败，未计入通过数。对照只替换 a00bc76e 的 EGL module，不宣称整树基线验收。
原 APK 无 Profile/survey、隔离沙盒越过原空 Context/strstr 路径，6.7 秒报告新的纹理
初始化空读并退出，无超时；尚未获得实际呈现帧。证据 `.local/tales-black-screen-fix/`。
静态文档检查通过；全树平台边界检查被未改动的 frontend/gui/process_manager.cpp
既有平台宏阻断，本次新增/改动文件另作平台边界静态核对。Windows/Linux 未实跑。
