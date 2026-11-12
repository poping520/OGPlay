# 前端、游戏库与会话宿主

前端把同一运行内核提供给桌面 GUI、CLI 和 Android 入口。
共享 app 外观隔离内核类型，library 管游戏库领域，game 管实际运行，
各可执行入口拥有自己的交互、协议与呈现，不实现游戏兼容逻辑。

## 构件与边界

```text
ogplay 桌面入口 ─→ GUI / 子进程管理 ─┐
ogplay-cli ─────→ 命令与 stdio ─────┼→ ogplay::app → library / game → session
Android SDL_main ──────────────────┘
```

ogplaycore 只导出同构建的 C++ 外观。
公开类型使用 std/app 值或 Pimpl，不泄漏 runtime 实现头。
标准异常可跨同工具链边界，SDL C callback 只保存异常再安全恢复。
不承诺跨版本 ABI，也不在加载 DLL 时创建应用资源。

GUI 拥有 WebView、RPC、导入向导和子进程。
CLI 拥有 usage、stdio 和结果格式。
RunGame 自行完成 SDL bootstrap、日志和运行租约，然后调用 game。
EXE 不另静态链接一份内核，避免产生两套全局运行状态。

## 游戏库对象

library 读取单体 APK、Manifest、名称、图标与原生库事实。
导入按安装实例组织，真实 package 不等同于 installation id。
同包多个实例可以分别保存设置和数据。
Profile 只作为精确 identity 覆盖，未命中仍可走 generic startup。

库条目保存导入事实、设置、可选自有数据包和最近运行报告。
沙盒目录独立保存 guest 持久数据。
移除库条目默认保留存档；明确删除存档时才处理对应 sandbox。
引用的外部数据与原始 APK 不因移除条目而删除。

ClearSaves 清空覆盖层内容，保留 meta、身份、APK、设置和日志。
同包重导入可沿用残留沙盒实例。
这使用户的数据生命周期不依赖启动器卡片是否还在列表中。

## 库锁与运行锁

库修改在排他库锁内执行，查询使用共享锁或有界尝试。
每实例运行锁表达真实跨进程运行状态。
获取运行租约与删除检查使用同一锁机制。
锁文件不能主动 unlink，否则可能制造两个互不相见的锁对象。

busy 是竞争状态，不能把条目标记成损坏。
导入原子发布，失败只清理本次新建内容。
活动导入快照与后台线程由服务 owner 回收，不能 detach。
运行租约持续到 RunGame 返回或异常。

## 设置解析与启动计划

设置 schema 在 library 中维护，GUI 与 CLI 使用同一份定义。
有效值按游戏覆盖、全局、默认解析。
读取配置可以跳过未知键，写请求仍拒绝未知字段。
保存检查 revision，避免旧窗口覆盖较新的配置。

ResolveLaunch 生成受检运行选项。
FormatGameRunArgs 是参数格式化的唯一实现，与解析保持往返。
预览和真实启动走同一路径，不允许前端自由拼 argv。
预留设置只保存，不暗中改变内核行为。

逻辑尺寸和 density 是 guest 显示事实。
继承尺寸可在建窗前按 Manifest 入口方向调整。
游戏显式尺寸保留来源，不被默认方向规则再次交换。
超采样与宿主帧率限制不能改变 Android display refreshRate 的声明。

外部数据来源与挂载位置属于用户设置。
默认挂到 sdcard，可指定其合法子目录。
Profile 不提供 required external 清单，也不决定是否解压 OBB。
缺失路径诊断可形成挂载建议，但应用成功探测不到可选文件不等于故障。

## 桌面 GUI

Windows 使用 WebView2，macOS 使用 WKWebView，平台对象由 HAL 隔离。
启动器主页面只加载 bundled webui。
CSP 与导航限制拒绝任意网络页面、弹窗和额外桥接。
RPC 使用 closed schema，前端传事实和实例身份，不传任意宿主路径执行请求。

导入向导先分析独立快照，再选择可选数据包和实例策略。
后台 job 提供进度与取消，发布之后仍报告真实导入结果。
分块上传检查顺序与总长度，不把半个 APK 发布为条目。
图标与名称失败可显示明确占位，Manifest 损坏则阻止发布。

Web UI 渲染状态码和模型事实。
界面文案在前端翻译，不由 C++ 拼接整句供页面解析。
宿主界面语言和 guest Locale 分开。
用户更改语言不改变游戏的 Android 区域事实。

## 游戏子进程

桌面启动当前 EXE 的 --run-game 模式。
启动器跟踪 PID、实例、端口与退出码，非阻塞回收。
进程退出后读取受限日志尾部，不能从自由文本推断兼容性。
启动器退出解除跟踪，不擅自杀死仍在运行的游戏。

last-run.json 保存结构化运行结局。
待排查分类来自条目损坏、数据缺失和运行失败等事实。
closed/exited 与 startup_failed/fault/crashed/no_frame 分开。
报告用于整理和挂载提示，不能替代正式兼容验收。

## CLI 与独立探针

ogplay-cli 的 library 命令调用共享库领域服务。
library run 在当前进程执行共享 RunGame，并持有相同运行锁。
JSON 输出与状态行分流，便于工具读取。
参数错误、guest 退出与运行故障保留各自退出码。

run-apk 装配通用 APK 进程，不要求 Profile 才能启动。
preflight 检查所选 ABI 的依赖与导入，不执行 guest。
dexvm dump-surface 导出实际 VM 表面，不执行类初始化和图形。
dexvm run-dex 提供独立 main 探针，复用生产 native/JNI 与启动类路径。

run-dex 的 Java stdout/stderr 与宿主诊断分开。
一致性工具可按 oracle 约定合并 Java 两路输出。
不能把宿主错误文本混入 expected.txt 比较结果。
探针通过只说明对应行为，不是游戏兼容结论。

## 会话循环与呈现

game 消费统一选项，查找随附 payload，创建 SDL3 窗口并编排 session。
宿主事件转为输入，Clock 驱动 step，完成帧交给 HAL。
实时等待期间仍处理宿主事件，手动模式则只在许可 step 时推进。
异步读回可在暂停时呈现最后完成帧，但不推进 guest 或 Clock。

帧源序号与实际 present 次数分开。
FPS 使用实际成功 present 数，不能用跳号的源帧序列替代。
关闭、帧预算和 MCP shutdown 属于宿主控制结局。
guest exit 和 fault 保留原始代码或异常。
首次呈现前退出还需报告明确启动失败。

## Dashboard 与按键编辑器

Dashboard 使用游戏进程 loopback 服务的只读 dash.* 路由。
启动器核对 schema 与 PID 后才标记入口 ready。
页面不拥有启动器 RPC，也不能提交任意目标 URL。
同实例复用监控窗口，游戏退出时移除入口。

按键编辑器使用单独 control.* 通道和随机令牌。
令牌通过子进程环境和页面会话传递，不放在 URL 中。
主循环处理 freeze/apply/resume，网络线程只排队。
具体映射模型见 [输入](input.md)，诊断数据模型见 [诊断](diagnostics-testing.md)。

## 契约、决定与操作

- [frontend](../../src/frontend/MODULE.md)、[app](../../src/frontend/app/MODULE.md)
- [library](../../src/frontend/library/MODULE.md)、[game](../../src/frontend/game/MODULE.md)
- [GUI](../../src/frontend/gui/MODULE.md)、[CLI](../../src/frontend/cli/MODULE.md)
- [desktop](../../src/frontend/desktop/MODULE.md)
- [交付与依赖决定](../adr/development.md)、[会话决定](../adr/session.md)
- [Dashboard 手册](../playbook/DASHBOARD.md)、[已知问题](../state/KNOWN-ISSUES.md)
