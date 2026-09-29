# BND-45：进程内 NDK Looper 身份与事件轮询

状态：完成（所列 ident/pipe polling 有界范围）。决策：[ADR-0083](../../adr/runtime.md#adr-0083)。

## 范围

- 补齐 API19 ALooper 查询、prepare、引用、wake、removeFd、pollOnce 导出，与既有 addFd/pollAll
  统一到按 guest thread 管理的状态，消除固定全进程句柄及任意 write 触发命令事件的行为。
- Java 主线程及 HandlerThread 经 integration 关联 native Looper；退出清理，shutdown 唤醒。
- VFS 提供真实 pipe readiness，保留数据消费、EOF、失效和另一端关闭的事实。
- 保留 dlsym 的失败语义；未支持 callback/fd 类型明确失败，禁止补零返回桩。

## 验收

- 线程隔离/prepare 幂等/未 prepare 查询/引用与陈旧句柄/退出及 shutdown。
- pipe 输入/输出/EOF/挂断/无效 fd、remove/替换及输入 attach/detach。
- Java scheduler 关联及真实 ARM dlsym→ALooper 调用。
- windows-msvc 受影响目标构建、定向回归、无 survey 原 APK/OBB 路径复跑一次。

guest callback、通用 epoll/socket/宿主 fd 和 Java 消息在 NDK poll 内派发不是本阶段支持范围。
不引入 Binder 或完整 Android MessageQueue。原路径越过首错只记 reached-fault。

## 实际验证

- windows-msvc Release 的 ogplay_tests、ogplay 构建通过；沿用本机 /wd4996 /wd4834。
- 定向 14 用例/391 断言通过：线程隔离、引用/退役、wake/timeout/shutdown、注册拒绝及
  替换、真实 pipe readiness、输入 attach/detach、输出预检、Java main/HandlerThread
  准备关联、真实 ARM libdl 导出调用及 TID 复用、缺失符号 dlerror 和低 PC 调用点诊断。
- 用户原 APK/OBB 命令复跑退出 1：ALooper_forThread 缺失和 PC=0 消失，已进入 onDrawFrame；
  下一首错为 javax.crypto.SecretKeyFactory 类缺失，请求 PBEWITHSHAAND256BITAES-CBC-BC。
  后续仅调整 poll 输出和超时 Clock 并通过定向回归，没有重复游戏启动。
- architecture.boundary_hot_path 未通过：android_module.cpp 的既有 AssetManager read
  使用 std::function，HEAD 16106394 已含同一代码。本轮没有扩大范围修改该既有问题。
- git diff --check、UTF-8/TOML 与新增文档链接检查通过。未做完整游戏或 GC 全周期验收。
- 证据：.local/looper-fix-build-verified.log、looper-fix-tests-verified.log、
  looper-fix-startup.log、looper-fix-startup-check.log、looper-fix-static-check.log。
