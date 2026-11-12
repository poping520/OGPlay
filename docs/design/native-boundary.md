# native 边界与 JNI 互通

native 边界把 guest ABI 调用转为宿主能力，同时保持地址、对象和线程身份。
Virtual SO 解决 ELF 导出和模块组织，Fast Host Call 解决调用传输，
JNI 则在独立语义层连接原生代码与 Java；三者共享底座而各自拥有状态。

## 两条入口

```text
ARM import / dlsym → Virtual SO thunk → SVC #2 → boundary module
ARM JNIEnv table  → JNI slot thunk   → SVC #3 → jni_guest → jni
```

两条路径都可以通过 CPU HostCallHook 直接借用 live registers。
JNI 不是一个 Virtual SO；它的函数表和引用语义不归 boundary catalog。
边界 module 也不能反向依赖 DexVM 或整个 session。
应用 Java 调用 native 则由 bridge 解析注册函数或标准 JNI 导出。

## Catalog 与 Virtual SO

BoundaryCatalog 是 SONAME、导出和 API 范围的唯一事实源。
装配期过滤 API、检查身份冲突并分配稳定 dense slot。
module-local id 用于元数据，不等同于数组下标。
seal 后的目录与 hot binding 只读。

合成 ELF 在首次建立时就发布当前 API 下已声明的完整 active exports。
不能只按初始 DT_NEEDED imports 裁剪 dynsym。
后续 dlopen 的新依赖因此能解析同一个既有模块。
未实现导出不能用假地址冒充可执行能力。
公开数据导出与函数 thunk 分开，private vtable 方法不必进入 dynsym。

catalog 元数据不要求创建 ANGLE、窗口或完整 runtime。
链接预检只需系统库来源、loader 与 sealed 导出。
这使静态缺符号检查不会意外执行 guest 或初始化图形设备。
应用提供与 Virtual SO 同名库也不能无声改变边界归属。

## 模块与共享服务

concrete module 拥有对应 SO 的 ABI handler 和私有状态。
共享服务保存跨 SO 的事实，例如同一 EGL Context 的 GL 状态。
EGL、GLES1、GLES2 和 GLES3 通过显式注入共用 GraphicsBoundaryContext。
不能因为拆成多个 SO 就复制 texture namespace 或 current 状态。

facade 负责组合，不成为第二个中央业务分派器。
热调用直接到导出对应函数和 concrete module 实例。
module 不在调用时查全局注册表寻找其他 module。
frame、音频、日志和输入能力都通过窄接口注入。

真实 libc 的热点拦截保存原 ELF 归属。
它可以复用 thunk transport，但不能把整个 libc 宣称为 Virtual SO。
libdl 的 namespace 和 handle 属于进程 owner，handler 只负责 ABI 与错误表达。

## Fast Host Call

```text
thunk PC
  → arena.DecodeSlot
  → hot[slot]
  → invoke(self, live register context)
  → 继续当前 CPU Run
```

thunk arena 按页映射并最终封为 RX。
地址解码先检查范围、对齐和 slot 是否绑定。
元数据用于冷查询和失败诊断，正常调用不按字符串重新查找。
SVC 不属于本 router 时返回 unhandled，让既有慢路径处理。

fast 与 slow 共用业务 handler。
是否使用 fast transport 不应改变参数、状态或异常。
启用 slice observer 时使用 slow 路径，以保持可观察边界。
启用需要外层释放名额的 CPU 执行预算时也遵守其运行协议。

C++ 异常不能穿过 JIT callback。
fast callback 将原异常保存为 pending fault，由安全的 slow consumer 恢复。
诊断附带 SO、symbol、寄存器、SP、LR 和线程身份。
内存错误、GL 错误和 Java 异常各有自己的表达，不能统一吞掉。

同一 JIT 实例不能在 callback 中递归 Run。
Guest→Host→Guest 使用独立 nested CPU context。
clone 与 nested 调用继承同一进程 hook 和资源事实。
退出须停止所有调用，再撤销 hook 和释放 arena。

## A32 参数与内存搬运

A32CallFrame 借用前几个参数寄存器，超出的 word 一次读取 guest 栈。
64 位参数和返回值按 ARM ABI 对齐与编组。
普通导出用 typed binder，复杂 variadic 和 callback 用明确 wrapper。
不能让每个 handler 自行猜测栈布局。

GuestPtr、GuestCString 和强类型地址保留 guest identity。
指针不能 reinterpret_cast 成宿主指针。
输入先完整验证并复制，输出先预检再调用下层，成功后显式提交。
映射世代变化时重新验证，失败不提交部分输出。

共享暂存可以复用容量，但每次仍重读 guest 内容。
回调解除锁前复制必要参数，不能持有可能扩容容器的元素引用。
资源 token 必须校验 owner、类型和存活性。
同一个整数在另一个会话中不能继续有效。

## JNI 语义与 guest ABI

runtime/jni 维护引用、类型、调用、异常和 native 注册语义。
它只依赖 core，不依赖 CPU 或 Android framework。
runtime/jni_guest 物化 guest 函数表、trap 和 A32 参数解码。
它把 handle 转成语义身份，再调用 jni 层。
integration 注入 VM hooks，避免下层反向依赖 DexVM。

JNIEnv 的 reserved 槽为空，未绑定槽记账并明确失败。
local、global 和 weak 引用保留各自作用域与 GC 含义。
Attach 保存线程身份，detach 检查 Java 帧和 monitor 生命周期。
回调返回的对象由调用线程发布 local reference。

pending exception 默认遵循 Dalvik 非 CheckJNI 行为，并记录诊断。
这不放宽坏 handle、线程归属或底层内存错误。
Throw、Clear 与查询保持异常身份；helper 不重复算成一次入槽调用。
反射 Method 与 JNI method ID 经唯一元数据工厂互转。

## 与 DexVM 共用对象

Java 字段由 VM instance/static 槽持有，JNI 读写访问同一槽。
Class 镜像在两个方向规范化为同一对象。
对象数组元素保存真实运行时类型，不用声明 component type 猜测。
VM 的 assignability 回调负责类、接口和数组协变。

primitive array 和 String 使用稳定 guest payload。
JNI Elements/Chars 可直接借用同一地址并通过 pin 保活。
Modified UTF-8 仍需要编码副本及独立 lease。
对象身份、引用 handle 与 payload 地址不能混用。
详细回收与 pin 规则见 [DexVM](dexvm.md)。

## native 库生命周期

Java loadLibrary 通过应用 ClassLoader 和唯一 NativeLibraryLoader。
同一 canonical path 与 loader 的加载幂等；跨 loader 冲突明确失败。
构造器在映射和重定位完成后执行，依赖先于本库。
JNI_OnLoad 只从显式 root 自身查找，不误调用依赖库的同名符号。

native dlopen 可复用同一映射，但不登记 Java loader 或执行 JNI_OnLoad。
后续 Java 加载复用已完成的构造器事实，再执行需要的 JNI 初始化。
失败保存原故障，不能为了重试重复执行已有 guest 副作用。
清理在所有相关线程停止后进行，hook 寿命短于被引用的 owner。

## 契约、决定与操作

- [boundary](../../src/runtime/boundary/MODULE.md)、[服务](../../src/runtime/boundary/services/MODULE.md)
- [JNI](../../src/runtime/jni/MODULE.md)、[guest JNI](../../src/runtime/jni_guest/MODULE.md)
- [integration](../../src/runtime/integration/MODULE.md)
- [对象所有权](../adr/dexvm.md#adr-0029)、[稳定对象存储决定](../adr/dexvm.md#adr-0152)
- [排障手册](../playbook/TROUBLESHOOTING.md)、[已知问题](../state/KNOWN-ISSUES.md)
