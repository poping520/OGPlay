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
返回 JSON 表面及 API19 native exports；与 run-apk/run-dex 共用 AssembleAndroidContext，
在 catalog 注册/VM 建立前校验封存模式及 DEX/成员表/主题/视频 adapter/配方身份，选择不可变 owner；未知或
错配制品明确失败，不回落 Legacy。平台注入无 accessibility 服务事实，测试 bool 不参与
生产装配。无标识制品同样明确失败。不运行 guest、类初始化、图形或游戏会话。

DumpDexSurface 同时装配 bundled API19 骨架索引；只有该显式诊断枚举会物化完整骨架，字段状态区分常量与未实现访问。

RunDex（一致性探针，DIAG-7）复用 AndroidGuestProcess 的 rootless API19 system ELF 进程、
AndroidGuestCallSession、DexVmGuestBridge 与 NativeLibraryLoader，加载生产同源的 guest JNI、
ICU 数据与 tzdata；JNI 注册、NIO 和 bound core/Android catalog、BootDex、骨架与 APK 路径共用。
加载单个 dex/jar，经真实 native 入口 `NativeStart.main` 调用 `main(String[])`；无 APK，
不启动图形或音频呈现；可选 `--native-lib-dir` 将目录内合法 `lib*.so` 作为 armeabi-v7a
应用库，经同一 NativeLibraryLoader 和 application ClassLoader token 加载。目录不存在、
非法文件名、坏 ELF 或重复 DT_SONAME 明确失败；未指定时仍无应用库，
`System.load/loadLibrary` 缺库明确抛 UnsatisfiedLinkError。realtime 统一
Clock 驱动 Android 单调时钟；main 返回后 root 线程死亡、等待包含已创建待运行者的非 daemon 线程，未捕获异常经线程
handler 或 ThreadGroup 分派（处理器失败写 stderr）。结果只作行为证据，不是兼容结论。

RunDex 的 `--ledger-json` 保留未实现命中，并以独立 `warnings` 数组导出 guest process 的
warn 级命中（id/count/首末 LR），不将 Dalvik DefineClass 的 NULL 返回当作实现成功。

RunDex 默认将 Java out/err 分别输出到宿主 stdout/stderr；显式传入 DexConsole 时仍按原
公共契约合并两路字节，诊断/未捕获异常的宿主报告继续使用 stderr。
一致性工具可显式使用 `--merge-console`，按 AOSP run-test 的 `2>&1` 约定将 Java 两路字节
按提交顺序并入 stdout；宿主结构化诊断仍在 stderr，不能污染 Java expected.txt。

DescribeUnhandledException 只导出异常报告字符串：识别 VmJavaThrow 的类型/消息，
标准异常原样保留 what，未知异常保持未知失败；不向 EXE 暴露运行时类型。

RunGame 在同一共享库内将已知私有运行时异常转换为保留原类型/消息的标准失败，
再跨 app 边界；SDL 回调仍只保存 exception_ptr，不跨 C ABI 抛异常。
