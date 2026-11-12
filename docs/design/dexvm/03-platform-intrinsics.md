# 03 · ROM 平台边界与宿主接缝

## 1. 类与方法的归属

启动类路径按固定顺序加载 ogplay.jar 和原样 API19 ROM jar。ogplay.jar 提供 OGPlay 自有的
Java 与进程内服务对象，ROM 类保留完整字段、方法和初始化；应用类按 parent-first 链接。
来源和哈希由启动类路径 manifest 校验，不再逐类选择 ROM 类或生成成员来源配对制品。
实际装配规则见 [DexVM 契约](../../../src/runtime/dexvm/MODULE.md)。

新代码不得整类替换 ROM，也不得用 intrinsic 覆盖 ROM 非 native 方法。现存替换和覆盖仍需
按阶段移除；不能以 catalog 中存在 handler 作为 ROM 行为已实现的证据。
普通 Java 状态只存在于统一对象图，宿主仅持有 native 资源和进程内服务需要的状态；
持 guest 引用的宿主状态须按 owner 注册 trace/sweep，不能另建影子字段或永久强根。

## 2. native 与进程内服务

`runtime/dexvm/intrinsics` 负责 VM/libcore 接缝，`runtime/integration/dexvm_android`
负责 Android native 和服务绑定。catalog 显式聚合，平台事实由 context/services 注入，
下层不反向依赖 integration，时间统一来自 Clock，一个 guest 线程对应一个宿主线程。

ROM ActivityThread、LoadedApk 和 Instrumentation 创建 Application/Activity，主 Looper
运行 ROM MessageQueue/Handler。宿主 MessageQueue native 负责 poll、wake 与 Clock 等待，
服务通过 IApplicationThread 把生命周期与配置消息排入 ROM 队列，不重放 Java 回调序列。
ROM ViewRootImpl 完成遍历和输入分发，Canvas native 经 Skia 写入软件 Surface；
进程内 WMS 管理层级、frame、焦点和输入通道，WindowSystem 合成软件/EGL/视频帧。
ROM ServiceManager 返回本进程 Binder/Stub 对象，不经 Parcel 传输，不实现 Binder 驱动、
跨进程 IPC、system_server 或 Zygote。具体范围见 [Android 契约](../../../src/runtime/integration/dexvm_android/MODULE.md)。

ROM Resources/AssetManager/Theme/TypedArray 通过 native 使用唯一
[资源核心](../../../src/runtime/resources/MODULE.md)，读取 framework-res.apk 与应用 APK；
Java 和 NDK 共用 manager、配置和资源身份，不维护主题或系统布局的第二套投影。
图形 native 使用 Skia，GLES 使用 ANGLE，窗口与输入使用 SDL3。

## 3. 存量替换与覆盖的查询

使用现有二进制只读导出实际链接表面：

```sh
build/release/ogplay-cli dexvm dump-surface > .local/surface.json
```

将导出与本地启动类路径 ROM jar 的实际 DEX `class_def`、own 方法签名及 flags 比较，
可复用 `tools/dex_survey_lib.py` 的 `parse_dex` / `method_signature`，不以包名前缀猜来源：

- ROM 中存在且 surface `source=intrinsic` 的类是整类替换；自有 OGPlay 类不计入。
- ROM 非 native 方法在 `source=bootdex` 类中仍以 `origin=handwritten`、`binding=intrinsic`
  且 `status=implemented` 绑定，是存量覆盖；原版 abstract 声明与有字节码的方法应分开说明。
- 原版 native 的宿主实现不属于非 native 覆盖；`guest-jni-unverified` 只证明可解析，
  不证明已导出或可运行。无 handler 的声明、缺类及链接错误单独判断。

再沿 core/android 的 `Declare_*`、`MergesBootClass` 和链接器 `RegisterDex` 核对能力 owner、
资源接缝及保留原因。清单留在本地盘点产物，文档不重复维护类名和数量。

## 4. 反射与失败边界

Class、反射 wrapper、成员访问及调用共用 linker、Reflection Runtime、对象模型与 GC；
应用和 ROM 共享 JNI 引用身份。普通反射调用执行实际目标，不以 intrinsic 绕过访问检查。
P4 的成员检查对齐 Dalvik，范围及验证见 [P4-myboy](../../tasks/dexvm/P4-myboy.md)。

合法查找与执行分开：缺类/缺成员发布对应 Java 错误；未绑定 native 调用记账并抛
UnsatisfiedLinkError；文件、内存、GL、线程、音频和游戏逻辑返回值继续明确失败。
无返回值的纯 UI 展示请求仅在安全边界调用 `RecordUiDegraded` 忽略并记录警告，
密码掩码等有实际危害的属性不降级，不用 null 冒充应返回的对象。
诊断 survey 的中性返回只用于发现真实命中，不能作为兼容验收。
