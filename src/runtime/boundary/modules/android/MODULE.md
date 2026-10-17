# Android NDK boundary

拥有 libandroid.so 的导出、进程内 opaque 对象及输入状态。只依赖 boundary services、
memory、core/hal；VFS 和线程存活事实通过 AndroidLooperHooks 注入，不反向依赖 integration。

## APK Asset

- manager 身份与 Activity 分离：fromJava 经显式 hooks 转换真实 JNI 引用，NativeActivity
  使用同一 manager。句柄单调分配且不复用，最多 1024 个 live manager/4096 个 Asset。
- source 接口只发布 length/ReadAt/直接 FD，不依赖 JNI、loader 或 VFS。open 在 registry
  锁外查询；每个 Asset 有独立游标和操作锁，read 使用至多 64 KiB 的临时窗口，不物化
  整个条目。读输出预检失败不推进游标。seek 限于资源范围，EOF 为真实零字节读取。
- openFileDescriptor 先预检两个 32 位输出，再获取完整 APK 的新 guest FD 与 start/length；
  失败回滚新 FD，压缩资源返回负值且不改输出；API19 的 32 位范围截断保持。
  Asset.close 不关闭交付的 FD；manager 退役不销毁已打开 Asset 的来源。
- NativeActivity 退役只处理窗口/输入；Java manager GC 独立撤销注册。owner 停止 guest
  线程后解除 hooks，同时退役所有 Asset/manager source。无永久 Java 引用、宿主指针或 FD。
  64 位导出未实现。
- openDir 发布有界、排序的直接条目快照（≤4096 项、名称合计≤1 MiB），不存在目录仍返回
  非空迭代器；next 只返回普通文件 basename，guest 只读名称在下一次 next/close 失效。
  目录句柄单调、不复用、≤4096 live，manager 退役/解除 hooks 一并回收。
- getBuffer 是唯一允许物化整个条目的路径：调用时才读取完整 STORED/Deflate 内容，单资源
  上限 32 MiB；guest 缓冲只读、地址在 Asset 生存期内稳定、不改读取游标，close/解除 hooks
  释放映射。超限记账并明确失败，普通 read 仍使用有界窗口。

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
  由 integration 关联。进程 shutdown 唤醒所有等待，后续 poll 返回 POLL_ERROR。
- fd 登记按 Looper 隔离；支持注入的真实 VFS pipe 就绪查询，轮询不消费 pipe 数据。
  add/removeFd 可替换/删除登记；错误、挂断、失效事件始终报告。未支持的 fd 或 callback
  登记返回 -1，不伪装成功。当前 pollOnce/pollAll 在无 callback 支持范围内行为相同。
- wake 返回 POLL_WAKE(-1)，超时返回 POLL_TIMEOUT(-3)，无关联返回 POLL_ERROR(-4)。
  outFd/outEvents/outData 允许为空；正超时判断通过统一 Clock，轮询可观察线程退出。
- NativeActivity InputQueue 绑定具体 Looper，仅所属线程可 poll 到对应输入；callback
  模式明确失败。命令通知只唤醒查询，不把任意 write 转换为输入/命令事件。

验证：BND45 定向边界、真实 libdl/ARM 调用与原 APK 路径；见 BND-45。

## 空传感器目录

- Java/NDK 读取 integration 注入的同一不可变 AndroidSensorDirectory；当前 Count=0，
  非空 provider 明确记账失败，不探测宿主、运行 SensorService 或产生事件。
- manager 是进程唯一非空 opaque token；getSensorList 返回零并可写非空空数组 guest 地址，
  写失败传播；任意类型 getDefaultSensor 返回 null。没有合法 ASensor，getter/enable/disable/
  setEventRate 对任何 sensor 都记账、结构化错误并失败；float getter 不返回伪造位模式。
- queue 单调分配且不复用，≤1024 live；持有真实 VFS pipe 两端且永不写入。使用 Looper
  共用 addFd 路径，按 API19 忽略结果；callback 登记拒绝时 warn/记账，无 callback 派发。
  null/stale Looper 与 queue 明确失败；destroy 移除登记、关闭 pipe、退役 token。
- getEvents 无事件返回零；hasEvents 保留 API19 revents!=POLLIN 返回 -1 的语义，不能改成零。
  creator thread 退役（含借用其他线程 Looper）及 shutdown 回收队列；进程结束释放空数组。

## Java Surface 窗口

- fromSurface 经显式 hooks 在 VM 锁内解析真实 JNI 引用、校验 holder 有效（active 且 host
  surface 打开，或仍在 surfaceDestroyed 回调中，同 Surface.isValid），
  将既有 holder 身份交给同一 windows_ registry；不在 boundary 解析 JNI 或反向依赖 integration。
- 每个 holder active generation 共用一个 native window，holder 自有一次引用，fromSurface 每次
  acquire；release 不得释放 owner 引用。changed 同步 managed 几何；destroyed 按 AOSP 先交付
  surfaceDestroyed（期间窗口仍可用于 EGL），回调结束后撤销 active/owner 引用；留存引用
  允许读取/释放，但 EGL 拒绝 retired window。
- 无效/已销毁 Surface 返回 null。新代使用新 token，不复用；解除 hooks 只在 guest 停止后
  回收 Java 窗口。窗口仍使用现有 ANGLE/SurfaceHolder 生命周期，不提供独立 buffer 模型。
