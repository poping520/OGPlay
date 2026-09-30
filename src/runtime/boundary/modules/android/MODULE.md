# Android NDK boundary

拥有 libandroid.so 的导出、进程内 opaque 对象及输入状态。只依赖 boundary services、
memory、core/hal；VFS 和线程存活事实通过 AndroidLooperHooks 注入，不反向依赖 integration。

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
