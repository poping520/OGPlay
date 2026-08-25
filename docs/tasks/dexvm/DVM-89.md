# DVM-89 · native 失败保真与 JNI 字段互通

## 目标（一句话）

分离 DexVM 出向 native 的目标解析与执行，并让 native JNI 与解释器对 DEX 字段读写
同一份对象/静态存储，保留引用身份与类初始化语义。

## 依赖

- DVM-14 JNI 出向编组
- DVM-15 JNI 入向第三路由
- APS-8 process-lifetime loader 与 JNI_OnLoad

## AOSP 语义依据

- `.local/aosp/dalvik/vm/Jni.cpp`：`GetFieldID`/`GetStaticFieldID` 先执行
  `dvmInitClass`；object getter 创建当前 JNI 线程 local reference，setter 解码引用后写入。
- `.local/aosp/dalvik/vm/oo/ObjectInlines.h`：JNI 与解释执行通过同一对象 field offset/static
  field storage 访问值；OGPlay 对应为同一 `VmFieldId` 的 model/linker slot。
- `.local/aosp/framework/base/core/java/android/app/ContextImpl.java` 与 `LoadedApk.java`：
  `getPackageResourcePath()` 返回 `LoadedApk.getResDir()`；同 UID 应用的 resDir 来自
  `ApplicationInfo.sourceDir`，即已安装 APK 文件。
- `.local/aosp/framework/base/core/java/android/content/ContextWrapper.java`：该方法直接委托
  `mBase`，不在 wrapper 复制 package 状态。
- `framework/base` 与 `libcore` 仅用于核对 API 19 声明形状，不把 Android framework 状态
  搬入 DexVM，也不引入 Binder/system_server。

## 范围

- 会话提供只读的 RegisterNatives 精确映射查询，并保持名称与 descriptor 联合键语义；
- DexVM 仅在动态映射不存在时回退到 `Java_` short/long export；
- 动态映射一旦命中，其 guest CPU、JNI slot 或 native 内部异常原样向上报告；
- 把 DEX 自有 instance/static 字段发布到会话 `JniClassRegistry`，已有平台类可追加
  不冲突字段，字段 ID 继续遵循父类查找与 static/instance 严格分离；
- DexVM 字段通过可撤销的窄 hooks 接管 `JniFieldStore` 访问；平台 HLE 字段仍由原 side
  table 拥有，不产生第二份 DEX 字段状态；
- `GetFieldID`/`GetStaticFieldID` 对 DexVM 类先执行初始化；失败时留下当前 JNI 线程的
  pending exception 并返回 null ID；
- JNI 的 Z/B/C/S/I/J/F/D 与 object/array getter/setter 直接读写 linker/model 槽，引用
  getter 发布调用线程 local reference，setter 解析回同一 `VmObjectRef`；
- DexVM 自分配 identity 必须避开同域已导入 JNI identity，重复身份不得静默注册；
- 按 API 19 `Bitmap.java` 完整发布 `Bitmap.Config` enum：四个常量、Enum name/ordinal、
  `nativeInt`、`sConfigs`、生成的 `$VALUES/values/valueOf` 与 native index 映射；
- 按 API 19 `ContextImpl`/`LoadedApk` 语义发布 `getPackageResourcePath()`：返回同进程
  APK 的 guest 路径，`ContextWrapper` 虚派委托 base，原始 APK 在该路径只读可访问；
- 出向 native 释放 DexVM 单写者锁，JNI 回调重新获取；process native TID 使用 API 19
  Bionic mutex 可表示的 16-bit 范围，并按 native context slot 稳定分配；
- 支持相对超时 futex、只读 `/proc/meminfo`、长驻 native 在已处理 syscall/JNI 边界刷新
  watchdog，以及 guest 逻辑 RWX 与 `ARM_cacheflush`；宿主 backing 仍保持不可执行；
- 软件 `SurfaceHolder.lockCanvas/unlockCanvasAndPost` 与 `Canvas.drawBitmap/drawColor` 使用
  Bitmap ARGB backing 发布 boundary RGBA frame；补齐 AudioTrack native output rate、音量范围
  和 position-listener/notification-period 状态；
- 不补 title 专属 native，不改变 survey neutral stub，也不把 native fault 转成伪成功。

## 验收

- [x] 已注册但执行 fault 的 native 不进入 `Java_` export 回退；
- [x] 回归断言保留 `registered JNI native invocation failed`，且不含
  `native method has no registered mapping or export`；
- [x] 未注册 native 仍按既有顺序查找 `Java_` export 并明确失败；
- [x] DEX 字段可由 `GetFieldID/GetStaticFieldID` 查得，继承查找返回同一声明 ID；
- [x] JNI→解释器与解释器→JNI 共用 instance/static 槽，primitive/wide/reference 往返；
- [x] object 字段往返保持对象身份和 JNI local-reference 作用域；
- [x] `Bitmap.Config.RGB_565` 可经 DEX/JNI static field 读取，四个枚举对象及 native
  映射与 API 19 AOSP 声明一致；
- [x] `Context.getPackageResourcePath()` 与 wrapper 委托返回稳定 guest APK 路径，且该
  路径可读取原始 APK、不可写，不泄露宿主路径；
- [x] Marmalade 动态回调桩的 `mprotect(RWX)`、写入和 `ARM_cacheflush` 可完成；
- [x] 软件 SurfaceHolder 可把 Bitmap 像素提交为 presented frame；
- [x] native loader、JNI registry、field store/ABI 与 architecture 定向回归通过。

## 验证证据

- 第一阶段历史证据：Windows Debug 定向 CTest 19/19、当时全量 994/994。
- 字段互通阶段按要求不执行完整测试；Windows Debug 增量构建通过，JNI registry、field
  store、guest field ABI、DexVM identity/import/field bridge 10/10 与 architecture 6/6
  通过。
- Release 构建复跑原 PVZ survey 命令后，`initNative()V` 及
  `m_LoaderKeyboard:Lcom/ideaworks3d/marmalade/LoaderKeyboard;` 均已越过，并继续交付
  managed surface callback。补齐 `Bitmap.Config` 后再次复跑，`RGB_565` 已越过；补齐
  `getPackageResourcePath()` 后 PVZ 继续完成 `surfaceChanged`。本次黑屏追踪又依次越过
  native 锁死、Bionic recursive mutex owner 不匹配、`/proc/meminfo`、长驻 native tick
  上限、timed futex、空 Canvas 以及动态回调桩 RWX 拒绝，最终由 `doDraw()` 发布软件帧；
  frame counter 在后续循环中保持 `presented=1`，不再是零提交黑屏。
- Windows Debug 目标构建通过；graphics/AudioTrack/process native context/watchdog/futex/
  ARM private syscall/memory protection/managed surface 9 个定向用例、192 assertions 通过。
  按要求未执行完整测试。
- 继续执行约 1.2 万 frontend frame 后，S3E title module 在 `0x60462edc` 对低地址
  `0x1f84` 发生独立的 unmapped write；OGPlay 保留 PC/LR/register 与 code window，未把它
  伪装成平台 mapping 缺失。本 WU 不以该后续 title 内部 fault 声称完整兼容。
- survey 运行仅用于证明错误推进，不构成 title 兼容性验收。

状态：完成。
