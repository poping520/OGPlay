# DASH-03 · Dashboard HTTP 与前端骨架

状态：Windows 完成，定向与真实会话验证通过。
设计：[Dashboard](../../design/gui/dashboard.md)。

## 已完成

- 复用 MCP loopback 端口提供 `/dash/` 静态页面、`/dash/rpc` 只读 JSON-RPC。
  固定文件白名单预载、每文件 2 MiB 上限；拒绝路径变体、非 loopback Host/Origin、
  重复 Host/Origin 与错误方法；无 CORS 放宽，附 CSP/nosniff。
- run-apk 连接既有会话、诊断（含 Java 栈/执行/monitor/futex）、日志与账本。
  transport 先于来源销毁并 join；仅启用 MCP 不会启动诊断写盘协调器。
- `webui/apps/dashboard`：Preact/Vite/uPlot 顶栏、来源状态拓扑、实际 frame 采样曲线、
  线程表、事件筛选与 Java 栈焦点；不使用模拟数据。
- 125 ms 目标轮询周期，不重叠请求；2.5 s 超时、1 s 重试；保留最多 600 个快照/
  4096 个事件。断线清空当前事实，历史明确标注；stream_id 变化清除历史及游标。
- selection 使用 guest_tid/context_token/frame/capability；无 frame 的事件不强行关联。
  64 位大整数保留文本；选中线程变化后丢弃旧异步响应。
- webui 目标同时构建 GUI/Dashboard，附 manifest、SHA-256 和第三方许可；Windows
  构建校验并暂存到可执行文件旁，安装包包含两套静态产物。

## 验证

- Node 24：`npm run build` 通过，26 项 Vitest（含既有 GUI）、TypeScript 检查通过。
- `cmake --build --preset windows-msvc --config Release --target ogplay_tests` 通过。
- `ctest --test-dir build/windows-msvc -C Release -R "^frontend.dashboard_" --output-on-failure`
  两项通过：HTTP/Origin/Host/路径/只读边界及 manifest。
- agent Dashboard、MCP HTTP、MCP protocol 定向回归：32 用例、1013 断言通过。
- 真实 Angry Birds 2.3.0，临时沙盒 + manual-step：读取 Dashboard 不推进 frame 0；
  经既有 MCP step 3 后 Dashboard 显示 frame/presented 3、ticks 3000，无 guest fault。
  浏览器验证 6 条线程、GLThread Java 栈联动、图表、无横向溢出；退出后指标清空并标记
  断线，重启后页面连接正常。测试会话经 MCP 正常 shutdown，未遗留进程。

## 边界

- GPU/VFS/AudioTrack 尚无本阶段安全的非阻塞接线，明确 unavailable，不调用会等待的
  普通快照。DASH-02 新计数及 DASH-04 完整模块面板/关联不在本阶段。
- 当前图表是 frame 的时间采样，不代表逐帧耗时/FPS；现有事件没有 frame 时显示未关联。
  顶栏 package/profile/instance 等额外元数据、控制按钮仍待后续。
- 本次验收是 Dashboard 数据与交互链路，不是游戏兼容验收；Linux 继续暂缓。

依赖：uPlot 1.6.32（MIT），由 ADR-0072 和 Dashboard 设计选定；精确版本和 integrity
见 package-lock，许可随静态制品交付。
