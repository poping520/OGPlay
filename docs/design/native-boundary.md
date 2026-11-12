# native 边界与 JNI 互通

native 边界把 guest ABI 调用转换为宿主能力。它既要正确解码 ARM 参数，
又要保持地址、Java 对象、线程和资源的身份。
Virtual SO 负责 ELF 导出组织，Fast Host Call 负责调用传输，JNI 负责 Java 与原生代码之间的语义。
三者共享必要底座，各自拥有不同状态。

Virtual SO 是由目录声明导出、由宿主 handler 实现的合成共享库；
Fast Host Call 是在 CPU 的宿主回调内完成调用并继续当前执行片段的传输路径；
JNI 是 Android 原生代码访问 Java 对象、方法和异常的接口。
把这三件事分开，才能优化高频调用而不改变 JNI 引用或库加载规则。

## 两条入口怎样汇合

```text
ARM import / dlsym → Virtual SO thunk → SVC #2 → boundary module
ARM JNIEnv table  → JNI slot thunk   → SVC #3 → jni_guest → jni
                              ↑
                     共用 CPU HostCallHook
```

第一条路径从 ELF 导出进入，第二条从 JNI 函数表进入。
thunk 是放在 guest 可执行内存中的短跳板，用固定陷阱把调用位置交给宿主。
CPU 钩子可以直接借用 live registers，也就是当前执行现场的寄存器视图，
避免为一次短调用退出 CPU 循环并复制整份状态。

两条入口的共同点止于传输。JNI 的函数表、引用、异常与 RegisterNatives 继续由 JNI 层管理，
并不登记成一个 Virtual SO。应用 Java 调用 native 则走相反方向，
由 bridge 解析已注册函数或标准 JNI 导出，再准备 guest 调用帧。

## 目录、运行实例与封存绑定

BoundaryCatalog 是 SONAME、导出名和 API 可见范围的唯一目录。
装配时完成 API 过滤、冲突检查和连续槽号分配，随后 seal，即封存为运行期间只读的布局。
sealed binding 指按这份布局固定下来的函数与实例绑定，
不是每次调用都临时搜索的注册表。

这里有冷、热两类数据。冷数据保存符号名、ABI 与诊断信息，供 Loader 和查询使用；
热表保存槽号对应的函数及实例。module-local id 是模块内元数据身份，
dense slot 则是连续的热表索引，两者用途不同。

以应用链接一个 GLES 导出为例，Loader 在冷路径按 SONAME 和符号名查询目录，
把重定位位置填成 guest thunk 地址。此时需要名字、API 范围和 ABI metadata，
也可以据此报告缺少哪个导出。应用反复绘制时已经持有地址，
没有必要再从名字重做一次解析。

```text
装配 / dlsym：符号名 → catalog → 稳定 thunk 地址
调用：          thunk PC → slot → 函数 + 实例 → 返回
故障：          slot → 冷 metadata → 符号化诊断
```

这样保留了冷路径的可读信息，也缩短了正常传输。
性能取舍不是删除检查：PC 范围、对齐、槽号、参数和资源仍各自校验，
只是把装配期已能确定的查找从调用期移走，运行期也就不再扩容或移动这份封存布局。

首次合成 ELF 就发布当前 API 下目录中完整的 active exports。
例如初始库只引用某个日志函数，稍后 dlopen 的插件仍可能引用同一 SO 的另一个导出。
若按首次 imports 裁剪 dynsym，后一个加载动作会因为时间顺序不同而失败。
完整发布避免补写既有 ELF 的复杂度，代价是目录需要在进程运行前保持自洽。

目录不创建窗口、ANGLE 或音频设备。链接预检因而可以只建立库来源、Loader 与封存导出，
回答“是否能解析依赖”，而不执行 guest。
查找与执行是不同证据：符号能被 dlsym 找到，说明命名空间有对应地址；
JNI 方法 ID 能查到，说明成员身份存在；只有实际调用完成，才说明当前参数下行为走通。
未实现导出因此仍需明确的执行失败边界。

冷/热分离、完整导出和封存布局的精确不变量见
[boundary 契约](../../src/runtime/boundary/MODULE.md)。

## 模块独立，图形事实共享

concrete module 是实现某个 SO ABI 的具体 C++ 对象，保存本模块 handler 与私有资源。
共享 service 则保存跨 SO 的事实。例如 EGL、GLES1、GLES2 与 GLES3
通过注入的 GraphicsBoundaryContext 共用当前 Context、对象命名空间及图形状态。
按 SO 拆文件并不改变它们属于同一个图形进程这一事实。

facade 只负责组合模块；稳态调用直接进入导出对应的函数和实例。
这种选择减少中央 route、字符串查找与再分派，也让新增模块的归属更清晰。
跨模块依赖则需要显式接口与装配，不能在调用时再去全局注册表寻找其他模块。
具体共享边界见[services 契约](../../src/runtime/boundary/services/MODULE.md)。

真实 libc 的热点拦截使用相同跳板技术，却仍属于真实 ELF。
libdl 的 namespace 与 handle 同样由进程管理，handler 只处理 ABI 和错误表达。
这一区分保留了符号归属与库生命周期，避免把部分拦截误写成整库虚拟化。

## Fast Host Call 的一次往返

```text
thunk PC
  → 检查 arena 范围、对齐与槽号
  → hot[slot] 取得函数和实例
  → handler 借用寄存器并按需读取 guest 栈
  → 写入返回寄存器
  → 继续当前 CPU Run
```

arena 是存放连续跳板的 guest 内存区，装配后封为 RX。
router 先确认 PC 属于该区域，再由偏移计算槽号。
正常调用只访问热表，符号名留在冷路径供错误报告使用。
遇到不属于本 router 的陷阱时返回 unhandled，由既有慢路径处理。

fast 与 slow 复用同一个业务 handler，所以传输选择不应改变状态和返回值。
启用 slice observer 时使用慢路径，以保留外层可观察边界；
启用需要归还并发名额的 CPU 预算时也交回 runner。
这牺牲部分短调用性能，换取观察和执行准入协议的确定性。

JNI 同样可以利用 CPU fast hook，入口身份却来自函数表槽号。
某个 CallObjectMethod 还需要查询方法、核对 receiver、准备参数与传播异常，
这些语义成本并不会因为避免一次 CPU 退出而消失。
因此 fast call 是传输优化，而不是跳过 JNI 校验的特权入口。

C++ 异常不穿过 JIT callback。fast callback 捕获原始异常并保存为 pending fault，
安全的 slow consumer 随后恢复它，并附带 SO、symbol、寄存器与线程信息。
这样可以保留真实故障类型，而不是把所有错误转成某个通用失败码。

Guest→Host→Guest 回调使用独立 nested CPU context。
同一个 JIT 实例在 callback 内递归 Run 会破坏正在使用的执行现场，
所以 nested 与 clone CPU 都继承进程钩子，却保留独立寄存器和栈。
停止时先终止调用，再撤销 hook 和释放 arena，使 callback 的引用保持有效。

## ARM 参数与受检内存

A32CallFrame 把寄存器与 guest 栈组织成统一参数视图。
前几个 word 直接借用寄存器，更多参数批量读取栈；64 位值按 ARM ABI 对齐与编组。
常规函数使用 typed binder 将参数转换为具体类型，
variadic、callback 等复杂形状使用明确 wrapper。

GuestPtr、GuestCString 等强类型包装保存 guest 地址含义。
输入在调用下层前验证并读取，输出先预检，再在成功后显式提交。
提交若遇到映射世代变化就重新验证，避免把结果写进已被复用的地址。
这多了一道输出协议，但比依靠暂存对象析构自动回写更容易保留失败边界。

暂存容量可以复用，内容每次重新读取。
调用中若需要解除锁再回调，应先复制必要参数，避免保留可能因容器扩容而悬空的引用。
资源令牌则核对类型、owner 与存活性；owner 是负责资源停止和释放的拥有者，
相同整数不能脱离所属会话继续表示原资源。
若把资源 token 直接当宿主指针，guest 的陈旧值就可能绕过会话、类型与存活性检查；
多一层受检转换后，故障停在明确边界，而不会变成任意宿主内存访问。

## JNI 为什么拆成语义层与 ABI 层

runtime/jni 维护引用、类型、调用、异常和 native 注册，不依赖 CPU 或 Android framework。
runtime/jni_guest 物化 guest JNIEnv/JavaVM 表，解码 A32 参数并把 handle 转成语义身份。
integration 注入 VM hooks，把下层操作连接到真实对象模型。
依赖理由见[运行时边界决定](../adr/runtime.md#adr-0013)。

JNIEnv 的槽位是固定 ABI。未绑定槽记账并失败；local、global 与 weak 引用分别保留作用域和 GC 语义。
Attach 登记当前宿主线程，detach 检查活动 Java 帧与 monitor，
回调返回对象则在调用线程发布 local reference。
这比用一个全局对象指针表替代引用更复杂，却能保留线程和回收边界。

pending exception 按 Dalvik 非 CheckJNI 行为处理并记录诊断，
坏 handle、线程归属错误和内存错误仍各有失败路径。
Throw、Clear 与异常查询保留对象身份，反射 Method 与 JNI method ID 使用同一成员元数据来源，
从而避免不同入口观察到不同的方法定义。

## Java 与 native 怎样看到同一对象

Java instance/static 槽保存权威字段，JNI 读写直接访问同一状态。
Class 镜像在两个方向规范化，对象数组元素保留真实运行时类型，
类、接口和数组协变由 VM 的 assignability 判断。
这对应[唯一对象图决定](../adr/dexvm.md#adr-0029)，避免在 JNI 侧复制一套 Java 状态。

primitive array 与 String 使用稳定 guest payload，即位于 guest 地址空间、
在借用期间地址保持稳定的字节存储。GetByteArrayElements/GetStringChars 借用同一地址，
pin 将相应对象作为 GC 强根；Modified UTF-8 仍需要单独编码副本。
这使 native 写入能直接被 Java/NIO 看到，代价是漏 Release 会留下真实的 pinned live set。

一个数组借用涉及三种身份。Java 数组对象可以有多个 JNI 引用，它们指向同一个对象身份；
GetByteArrayElements 返回的 payload 地址是第三种值，只用于读取元素，
不代替 jobject，也不作为 GC 对象头，Release 按规定解除 pin，而不是从该地址猜测 Java 类。
global reference、local reference 和直接数据借用因此各自表达寿命：
删除一个 local reference 不会自动结束另一个未配对的 pin，
地址稳定也不意味着调用方取得了对象布局或线程安全保证，native 与 Java 仍需要应用自身的同步协议。

[稳定存储决定](../adr/dexvm.md#adr-0152)没有把对象头或完整 Dalvik 堆 ABI 暴露给 native。
借用、回收和 direct buffer 的具体关系见[DexVM](dexvm.md)。

## native 库的生命周期

Java loadLibrary 经应用 ClassLoader 和唯一 NativeLibraryLoader 加载。
同一 canonical path 与 loader 的重复加载幂等，跨 loader 冲突明确失败。
映射与重定位完成后，依赖构造器先于本库执行；JNI_OnLoad 只在显式 root 自身查找，
避免误把依赖库的同名入口当作目标库初始化。

原生 dlopen 可以复用映射，却不登记 Java loader 或执行 JNI_OnLoad。
native 库构造器与 JNI_OnLoad 各有单独的完成事实：复用已映射 ELF 不重复构造器，
Java 首次取得该库仍可能需要 JNI 初始化。
这样 dlopen 与 loadLibrary 共享 ELF 状态而不抹平 API 差异，失败报告也能保留真正出错的阶段；
相应地，失败状态需要保存，不能为了重试重复执行已有副作用。

进程关闭先停止相关线程，再退役库关联资源、hook 与跳板。
尚未完成的兼容性验证和存量 ROM 替换范围见[已知问题](../state/KNOWN-ISSUES.md)。

## 契约、决定与操作

- [boundary](../../src/runtime/boundary/MODULE.md)、[服务](../../src/runtime/boundary/services/MODULE.md)
- [JNI](../../src/runtime/jni/MODULE.md)、[guest JNI](../../src/runtime/jni_guest/MODULE.md)
- [integration](../../src/runtime/integration/MODULE.md)
- [对象所有权](../adr/dexvm.md#adr-0029)、[稳定对象存储决定](../adr/dexvm.md#adr-0152)
- [排障手册](../playbook/TROUBLESHOOTING.md)、[已知问题](../state/KNOWN-ISSUES.md)
