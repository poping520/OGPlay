# BND-47：GLES2 索引 Buffer 与 client 属性混合绘制

状态：实现、定向验证及原游戏点击路径复现通过；不代表完整游戏兼容验收。

## 范围

- 修复 glDrawElements 在 element Buffer 非零且存在 client 属性时主动终止的问题。
- 经 AngleFrame 有界回读当前 Buffer 的指定索引区间，计算最大索引后复用 guest 属性搬运。
  ES2 使用宿主 EXT_map_buffer_range，ES3 使用 core API；不增加 guest 扩展声明。
- BufferData/SubData、映射写入及共享 Context 的实际内容直接生效，不新增 CPU 镜像。
- 检查范围、搬运预算和已有映射；内部映射在绘制前解除，临时绑定在异常路径恢复。
- 不处理随机源、GLES1 同类路径或移动移植；回读可能同步 GPU，性能优化须另有测量依据。

## 验证

windows-msvc 的 ogplay/ogplay_tests 构建通过，定向 6 用例/461 断言通过。
覆盖 ES2/ES3 回读、分段上传、非零 offset、byte/short 索引、最大索引大于 count、
越界与映射冲突、失败恢复及共享 Context 更新。
证据：`.local/gles2-mixed-{build,tests}.log`。

原 APK/OBB、无 Profile、临时沙盒，120 帧点击 (699,114)，继续到 180 帧：
guestFault=null，弹框关闭并显示任务地图，原 opaque element buffer 错误消失。
随机源 CryptographicException 仍存在，未扩入本任务。
证据：`.local/gles2-mixed-{click,after-state}.json`、before/after PNG、diag.stdout/stderr.log。

首轮运行停在启动第 6 帧且未处理 shutdown；诊断未启用，保存状态后结束该测试进程。
启用诊断重跑完成上述验证。首次启动停滞原因未确定，不把这次成功复现推广为稳定性验收。
重跑的 shutdown 已进入 stopping 生命周期但未退出；保存
`.local/gles2-mixed-diag/diag-38748-1.{json,txt}` 后结束测试进程，退出停滞留作独立问题。
