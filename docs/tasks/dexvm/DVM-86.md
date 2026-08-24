# DVM-86 · API 19 高频值类与有界系统服务

## 目标（一句话）

发布高频 util/text/graphics 值类、进程内 Parcel/Parcelable transport，以及不依赖
Binder/system_server 的 PowerManager、Vibrator 和 Process façade。

## 依赖

- DVM-80：intrinsic family TU 与 catalog/layout gate。
- DVM-81：真实 Context/ContextWrapper 委托链。
- [`12-api19-capability-stack.md`](../../design/dexvm/12-api19-capability-stack.md) Phase E。

## 范围

- `Base64`、SparseArray family、TypedValue、TextUtils 使用真实数据语义。
- Color、RectF、Point/PointF、Path、PorterDuff.Mode、BitmapDrawable、ColorDrawable
  复用已有 Bitmap/graphics family，不建立第二套渲染状态。
- Parcel 支持 primitive、String、byte array、进程内 Parcelable；Bundle 实现 Parcelable，
  写入时快照 typed map，读出为新 Bundle identity。
- PowerManager/WakeLock 保存引用计数状态；Vibrator 保存有界请求事实；Process 只暴露当前
  guest process。power/vibrator 从 `Context.getSystemService()` 返回稳定 singleton。
- side-table reference 进入 GC trace，owner sweep 清理全部状态。
- 按用户授权，本 WU 不受通常 10 文件与 800 行限制。

## 不做

- 不实现 Binder transaction、service manager、文件描述符 transport 或跨进程 Parcel。
- 不模拟真实电源、屏幕唤醒、宿主振动器、进程调度或外部进程。
- 不运行全量 CTest；全量回归留到本阶段最后一个 WU。

## 验收与结果

- Base64 flag、Sparse 排序/覆盖、geometry/Path 状态与非法输入受检。
- Parcel cursor/type/buffer、Bundle snapshot、Parcelable reference 和 recycle 状态受检。
- Bundle side-table 中的 byte array/Parcelable identity 作为 owner 的 GC 强边受检，只有
  Bundle 被 root 时其引用对象仍保持存活。
- WakeLock acquire/release/reference count、Vibrator cancel 与 service singleton 受检。
- Windows `windows-msvc` Debug `ogplay_tests` 构建通过；DVM-86、catalog、Bundle、Context、
  GC 与 architecture 定向测试通过；全量未运行。

状态：已完成。
