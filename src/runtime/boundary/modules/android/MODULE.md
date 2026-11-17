# Android NDK boundary

拥有 libandroid.so 的导出、进程内 opaque 对象及输入状态。只依赖 boundary services、
memory、core/hal；VFS 和线程存活事实通过 AndroidLooperHooks 注入，不反向依赖 integration。

## APK Asset

- manager 身份与 Activity 分离：fromJava 经显式 hooks 转换真实 JNI 引用，NativeActivity
  使用同一 manager。AConfiguration_fromAssetManager 复制该 manager 的配置快照。句柄单调分配且不复用，最多 1024 个 live manager/4096 个 Asset。
- source 接口只发布 length/ReadAt/直接 FD，不依赖 JNI、loader 或 VFS。open 在 registry
  锁外查询；每个 Asset 有独立游标和操作锁，read 使用至多 64 KiB 的临时窗口，不物化
  整个条目。读输出预检失败不推进游标。seek 限于资源范围，EOF 为真实零字节读取。
- 两种 openFileDescriptor 先预检对应宽度的输出，再获取完整 APK 的新 guest FD 与 start/length。
  写回失败回滚新 FD，压缩资源返回负值且不改输出。seek 和长度查询共用游标与范围检查，
  32 位接口保留截断，64 位接口通过 ARM32 r0/r1 返回完整值。
  Asset.close 不关闭交付的 FD；manager 退役不销毁已打开 Asset 的来源。
- NativeActivity 退役与 AssetManager 无关；Java manager GC 独立撤销注册。owner 停止 guest
  线程后解除 hooks，同时退役所有 Asset/manager source。无永久 Java 引用、宿主指针或 FD。
- openDir 发布有界、排序的直接条目快照（≤4096 项、名称合计≤1 MiB），不存在目录仍返回
  非空迭代器；next 只返回普通文件 basename，guest 只读名称在下一次 next/close 失效。
  rewind 只重置游标。
  目录句柄单调、不复用、≤4096 live，manager 退役/解除 hooks 一并回收。
- getBuffer 是唯一允许物化整个条目的路径：调用时才读取完整 STORED/Deflate 内容，单资源
  上限 32 MiB；guest 缓冲只读、地址在 Asset 生存期内稳定、不改读取游标，close/解除 hooks
  释放映射。isAllocated 只报告已物化的缓冲。超限记账并明确失败，普通 read 仍使用有界窗口。

## 输入事件

- API19 AInputEvent/AKeyEvent/AMotionEvent getter 读取不可变 AndroidBoundaryInput 快照。
  指针属性与 64 轴坐标、offset、precision、纳秒时间及历史样本随事件保存；缺省轴按
  AOSP PointerCoords 返回零，无历史时 size 为零，越界索引或错误事件类型明确失败。
- 每个 InputQueue 独立持有 pending/inflight；getEvent 输出失败不消费，finishEvent
  仅能完成该队列的事件。事件句柄单调分配、不复用，队列退役清除 pending/inflight。
  单队列最多 4096 个待处理/在途事件；预算耗尽明确失败，不静默丢弃 release/cancel。
- 输入时间用 ARM32 r0/r1 返回，float 保留 soft-float 位模式。legacy 单指生产者暂经
  NormalizeAndroidInput 归一；多指/历史快照检查身份、索引、有限坐标及时间顺序。

## Looper

- 每个 guest 线程至多关联一个 Looper；forThread 不创建，prepare 幂等且沿用首次 options。
  句柄单调分配、不复用；线程持有一个内部引用，acquire/release 管理额外引用。
- 线程退役移除关联和事件登记；持有额外引用的对象保留到 release。Java/native 线程身份
  由 integration 关联，Java MessageQueue 使用同一个 Looper 的 poll 和 wake 位。Clock 步进只通知
  poll 重新判断 Java deadline。进程 shutdown 唤醒所有等待，后续 poll 返回 POLL_ERROR。
- fd 登记按 Looper 隔离；支持注入的真实 VFS pipe 就绪查询，轮询不消费 pipe 数据。
  add/removeFd 可替换/删除登记；错误、挂断、失效事件始终报告。pollOnce 在原 guest
  线程派发 callback，回调返回 0 时只移除当次登记。pollAll 继续处理 callback，直到取得 ident、
  wake、超时或错误。未支持的 fd 或缺少回调执行 hook 的登记返回 -1。
- wake 返回 POLL_WAKE(-1)，超时返回 POLL_TIMEOUT(-3)，无关联返回 POLL_ERROR(-4)。
  outFd/outEvents/outData 允许为空；正超时判断通过统一 Clock，轮询可观察线程退出。
- NativeActivity InputQueue 绑定具体 Looper，仅所属线程可 poll 到对应输入；callback
  模式明确失败。NativeActivity 的窗口、IME 与 finish 命令只通过活动注册上的显式 hook
  入队并唤醒所属 Java MessageQueue。登记退役后拒绝旧地址，派发由上层在队列线程完成。

验证：BND45 定向边界、真实 libdl/ARM 调用与原 APK 路径。

## 传感器目录和事件

- Java/NDK 共享会话注入的不可变 AndroidSensorDirectory 和 AndroidSensorSource。目录发布 API19
  传感器描述，数据源为每个客户端创建按 handle 启停和设定采样周期的订阅。桌面数据源使用统一 Clock，
  以固定朝北平放姿态产出时间有序的运动样本。
- ASensorManager 是进程 opaque token。列表、ASensor token 与字符串在 guest 只读内存中稳定，
  缺席类型返回 null，非法句柄和无效输出地址明确失败。
- Java 和 NDK 队列使用可读 VFS pipe 唤醒 Looper。NDK 可用 ident 或 guest callback，Java fd
  回调将就绪队列交还所属 MessageQueue 派发原版事件；创建线程、所属 Looper 或进程退役时关闭 pipe。
  API19 登记拒绝保留队列并记账，hasEvents 无事件返回 -1，getEvents 无事件返回零。

## Java Surface 窗口

进程内窗口服务可向指定的已注册 AInputQueue 投递事件，并登记每个事件的完成回调。
该入口不改变现行单前台队列的 PushInput 行为；队列身份、事件预算、Looper 与 NDK getter
仍由本模块持有。AInputQueue_finishEvent 先在锁内退役事件，再在锁外报告 handled，
回调不得执行 Java；上层负责唤醒原 Java MessageQueue 并在队列所属线程完成 ROM InputQueue。
失效队列明确失败，不能回落到其他 Activity 的队列。
带 API19 PREDISPATCH 标志的按键仍交给 NDK 队列；AInputQueue_preDispatchEvent 对其完成为
未处理并返回 1，使 ROM 输入阶段继续。普通事件返回 0，等待应用正常 finish；完成均在队列锁外通知。

- fromSurface 经显式 hooks 在 VM 锁内解析真实 JNI 引用、校验 holder 有效（active 且 host
  surface 打开，或仍在 surfaceDestroyed 回调中，同 Surface.isValid），
  将既有 holder 身份交给同一 windows_ registry；不在 boundary 解析 JNI 或反向依赖 integration。
- 每个 holder active generation 共用一个 native window，holder 自有一次引用，fromSurface 每次
  acquire；release 不得释放 owner 引用。changed 同步 managed 几何；destroyed 按 AOSP 先交付
  surfaceDestroyed（期间窗口仍可用于 EGL），回调结束后撤销 active/owner 引用；留存引用
  允许读取/释放，但 EGL 拒绝 retired window。
- 无效/已销毁 Surface 返回 null。新代使用新 token，不复用；解除 hooks 只在 guest 停止后
  回收 Java 窗口。registry width/height 是 holder buffer 尺寸，changed 同步后 NDK getter 立即可见。
- Java EGL 的 holder token 使用同源几何别名；退役别名不回落到 standalone 窗口。
  纯像素 publisher、成功 swap 的 buffer-queued 身份通知与退役串行，经显式 hooks
  投递到 integration。两种通知都复核读回前的窗口代次，且与 fromSurface 的 JNI hook
  分锁，通知期间无需获取 VM 锁。
- ANativeWindow_setBuffersGeometry 的尺寸仍只接受重置或当前尺寸。CPU lock 支持
  RGBA8888、RGBX8888 和 RGB565，guest 缓冲按转成 RGBA8 后不超过 32 MiB 限制，
  post 经纯像素 hook 发布。
  每次 lock 要求整窗重绘，同尺寸同格式时复用地址，退役或解除 hooks 时释放锁与缓冲。
  CPU 从首次成功 lock 持有生产者连接至退役。EGL 与 CPU 按 holder 代际互斥，
  EGL 连接在对应 surface 生命周期结束时释放。

## AConfiguration

配置保留资源表的完整字段。new 从会话 DeviceLocale 预填语言和地区，随后允许独立修改。
locale 访问各传输恰好两个 guest 字节。copy 复制完整配置，diff/match/isBetterThan 复用资源核心。
provider 在 guest 启动前安装、线程停止后解除。

## OBB

扫描器通过 VFS hook 有界读尾部签名，包名存于 AObbInfo 持有的 guest 只读映射。
guest 停止后撤销读取 hook 并销毁剩余句柄。
OBB 挂载未支持。NDK 与 Java IMountService 共用没有已挂载 OBB 的设备状态。
NDK mount/unmount 记账和限频警告，无异步回调。
