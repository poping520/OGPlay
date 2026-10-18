# APS-13 · 有界 Surface 队列与 ANGLE 图像交付

## 目标与依赖

实现 ADR-0140 第二阶段的底层资源；依赖 APS-12，供 APS-14 Java/native 使用。
因本阶段跨队列/图形与 Java/VM，按实际接口拆为两个相依 WU，在同一会话验证。

## AOSP 锚点

- `framework/native/libs/gui/BufferQueue.cpp::queueBuffer`：异步替换 droppable pending 不重发事件。
- `GLConsumer.cpp::updateTexImage/checkAndUpdateEglStateLocked`：合法 current 校验，无新帧也 bind。
- `GLConsumer.cpp::releaseTexImage`：single-buffer 归还消费权；原版 debug bitmap 不代表新流帧。

## 实现与边界

- 无 JNI/GL 的 `SurfaceStream`，排他 producer 租约带单调 stream identity/epoch；
  非阻塞 async latest 与 single-buffer busy；pending/acquired/current snapshot 为拥有型不可变帧。
- 每进程共享 64 MiB CPU backing 预算，按 vector 实际 capacity 计入所有在途 lease；
  单图 1..4096 RGBA8，布局/矩阵受检，源 PTS 不读取宿主时钟。
- acquire 与 commit 分离，GL 上传期间 worker 新 enqueue 不丢通知；失败 abort 保留较新 pending，
  否则归还原帧。producer disconnect 撤销 pending/通知，保留已锁存图像；abandon 永久拒绝操作。
- 新 boundary 冷接口仅选择实际 current ANGLE，不隐式绑定/借用其他线程 Context；
  `AngleFrame` 上传内部 2D texture，经 EGLImage 绑定真实 external texture 后释放临时句柄。
  保留 2D binding、unpack PBO/行状态，最终 external binding 同步 shared shadow。
- external target 的绑定、删除与查询复用既有 GLES 状态；扩展仅按 ANGLE 原生支持发布。
  Metal 更新明确不支持；无软件/默认 renderer 切换，不新增 shader 转译。

## 验收

- `Surface stream*`：租约身份/重连、跨队列拒绝、异步替换/通知、snapshot 预算、single-buffer busy、
  worker 提交、上传期间 enqueue/rollback 通知与 abandon。
- `SurfaceTexture*`：保留 APS-12 后端探针，APS-14 用原 Java 走真实像素闭环，含 unpack PBO 状态。
- 仅受影响 CLI/tests 构建与上述定向回归；文档布局/capabilities/UTF-8/diff 检查。

## 结果（已达成）

- 队列资源与 ANGLE 冷入口已供 APS-14 使用。producer identity/epoch、异步 latest/通知、
  single-buffer busy、跨队列拒绝、snapshot 持有预算和 worker 提交定向检查通过。
- APS-14 双解释器通过真实队列→原 Java/native→external sampler 的像素/PTS/矩阵/事件检查，
  同时验证 unpack PBO/row 状态保存、无 current/错误 Context 与原生 cold/shared binding。
- 受影响 CLI/tests 构建退出 0，合并定向验证 **27 cases / 4077 assertions**，未运行全量测试。
  统一证据见 APS-14；MediaPlayer、NDK/EGL window producer、前端后端设置没有因此完成。
