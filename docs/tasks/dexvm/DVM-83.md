# DVM-83 · Java GLES façade 与 NIO/Bitmap 编组

## 目标（一句话）

在唯一 `GuestGlContext` 上发布 API 19 Java GLES 可链接面，并让 Buffer、primitive array、
String 与 Bitmap 数据经受检 guest memory 调用既有 native GLES handler。

## 依赖

- DVM-80：`android_gl.cpp` family TU 与 Android ownership 已收敛。
- DVM-82：`NioRuntime`、typed view 与 JNI direct-buffer 已共享 backing/address 语义。
- [`12-api19-capability-stack.md`](../../design/dexvm/12-api19-capability-stack.md) Phase D / Java GLES。

## 范围

- 从固定 Android 4.4.4 AOSP 源码生成 `GLES10`、`GLES10Ext`、`GLES11`、
  `GLES11Ext`、`GLES20`、`GLUtils`、`GLU` 的 public static 方法与 int 常量。
- Java GLES 参数适配只调用 sealed native GLES catalog/binding；不复制 texture、program、
  buffer、EGL currency 或 ANGLE context 状态。
- direct Buffer 直接传当前 position 对应的 guest address；heap/view Buffer 与 primitive
  array 在调用期间使用 session direct arena，输出方法 copy-back 且不移动 cursor。
- `GLUtils` 校验 Bitmap identity/recycled 状态，把唯一 ARGB backing 转为 RGBA bytes 后调用
  同一 `glTexImage2D/glTexSubImage2D` handler。
- 本 WU 延续阶段授权，不受通常单 WU 10 文件与 family TU 800 行限制。

## 不做

- 不建立 Java-only GLES 实现、第二套 GL object namespace 或宿主指针旁路。
- Java wrapper 与 native catalog 无确定适配关系的入口保持逐方法记账并抛
  `UnsupportedOperationException`，不伪造成功。
- 不进入 AudioTrack、SystemClock/调度；不运行全量 CTest，全量回归留到阶段最后一个 WU。

## 验收

- 七个 API 19 类、关键 GLES20 Buffer/String 签名、常量与继承 shape 可机器链接。
- Java managed 调用与 native GLES1/GLES2 调用交错时观察同一 texture state。
- heap/direct Buffer 编组地址、copy-back、cursor 不变与临时内存回收均有机器测试。
- `GLUtils` 复用 Bitmap backing，非法/回收 Bitmap 与不支持的 format/type 明确失败。
- Windows Debug 受影响目标构建、DVM-83/NIO/EGL/GLES targeted regression、surface
  generator stale check 与 architecture label 通过。

## 结果

- API 19 Java GLES surface 已由固定 AOSP 源生成并进入 Android intrinsic catalog。
- `InvokeManagedGles` 使用 sealed boundary binding 与唯一 `GuestGlContext`；Java/native
  交错 texture identity/state 测试通过。
- `NioRuntime` 新增有生命周期保证的 guest-memory operation，heap/view copy-back 不改变
  cursor，direct Buffer 不复制。
- `GLUtils` 上传路径消费 `DexVmAndroidContext` 中既有 Bitmap 像素，不维护第二份 Bitmap。
- 聚焦验证通过；全量 CTest 按计划未运行。

状态：已完成。
