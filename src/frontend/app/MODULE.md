# 子模块：frontend/app

ogplaycore 的 C++ 外观装配边界，公开头为 include/ogplay/app；只导出 ogplay::app，内核类型不导出。
公开类型只含 std/app 值或 Pimpl；EXE/DLL 同次构建、同工具链、MSVC /MD（Debug /MDd），无跨版本 ABI 承诺。
删除旧 C main。标准异常可跨同构建 C++ 边界，SDL 的 C 回调捕获异常，回到外观后重新抛出。

RunGame 校验值、持有库运行租约、配置日志并自行执行 SDL bootstrap；游戏运行宿主交给 game，库领域交给 library。
AgentConsole 封装账本/session/agent 生命周期，提供 Request/HandleJsonRpc；JsonRpcDispatcher 只复用 envelope 机制，协议回调归 EXE。
诊断触发、bundled payload、默认库根、Clock 单调计时/UTC、CSPRNG 控制令牌与日志/JSON/编码外观均不暴露内核头。
JSON 值为所属文档的借用视图，回调结束/文档销毁后失效；序列化保持严格解析、重复键/大小/深度边界。
不拥有 CLI usage/stdio 循环、桌面模式分派、WebView、GUI 视图或子进程管理；加载共享库时不创建应用资源。
SDL 动态库与桌面共享；RunGame 的状态行默认 stdout，CLI library run 可选 stderr 以保持 JSON 输出。
验证：ogplay_app_tests、frontend.app_entrypoints（含导出/导入审计）、frontend.run_apk_options 和真实 APK 有界复跑。

GameRunOptions 显式携带 surface_width/surface_height（默认 800×480，每边 1..4096），
解析、格式化和直接调用共用边界校验；DataPaths 不再暴露模板目录。

DumpDexSurface 显式读取 BootDex 归档，以生产相同的 bound core/Android catalog 装配独立 linker，
返回 JSON 表面及 API19 native exports；不运行 guest、类初始化、图形或游戏会话。

DumpDexSurface 同时装配 bundled API19 骨架索引；只有该显式诊断枚举会物化完整骨架，字段状态区分常量与未实现访问。
