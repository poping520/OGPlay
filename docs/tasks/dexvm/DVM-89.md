# DVM-89 · native 失败保真与 JNI 字段互通

## 目标

分离 DexVM 出向 native 的解析与执行，让 JNI 与解释器共享 DEX instance/static 字段存储，
保留对象 identity、类初始化和原始异常语义。

## 依赖

DVM-14、DVM-15、APS-8。

## AOSP 依据

- `.local/aosp/dalvik/vm/Jni.cpp`：`GetFieldID/GetStaticFieldID` 先初始化类；object getter
  发布当前 JNI 线程 local reference，setter 解码后写回对象。
- `.local/aosp/dalvik/vm/oo/ObjectInlines.h`：JNI 与解释器通过同一 field offset/storage 访问值；
  OGPlay 对应 `VmFieldId`、model/linker slot 和 static storage。
- `ContextImpl.java`、`LoadedApk.java`、`ContextWrapper.java`：`getPackageResourcePath()`
  委托 `LoadedApk.getResDir()`，返回同进程 APK；wrapper 委托 base。
- `View.java`、`KeyEvent.java`：`OnFocusChangeListener` 为 public interface；
  `dispatchKeyEvent` 先处理 listener，再按 action 虚派 DOWN/UP/MULTIPLE。
- `ResultReceiver.java`：null Handler 同步虚派，非 null Handler 经 `post(MyRunnable)` 异步派发；
  Binder/Parcel 是另一条远端分支。

仅核对 API 19 声明形状，不引入完整 Android framework、Binder 或 system_server。

## 范围与边界

- RegisterNatives 先精确匹配 name+descriptor；未命中才查 `Java_` short/long export。已命中
  的 guest CPU、JNI slot 或 native fault 原样上报，不伪装成 mapping 缺失。
- DEX instance/static 字段注册到会话 `JniClassRegistry`，沿父类查找并严格区分 static/instance；
  `JniFieldAccessHooks` 只接管 DexVM 字段，平台 HLE 继续使用原 side table。
- JNI Z/B/C/S/I/J/F/D、object/array 直接访问 linker/model 槽；引用保持 `VmObjectRef` identity，
  getter 产生线程 local reference，setter 回到同一对象。DexVM identity 避开已导入 JNI identity。
- API 19 发布 `Bitmap.Config` 四个枚举常量、`name/ordinal/nativeInt/sConfigs/$VALUES`、
  `values/valueOf` 与 native index 映射。
- `Context.getPackageResourcePath()` 返回只读 guest APK 路径，wrapper 只委托 base，不泄露宿主路径。
- 发布 `View$OnFocusChangeListener` handle 与抽象方法；不伪造没有来源的焦点事件。
- `ResultReceiver` 仅闭合本地构造/send：Handler 决定同步或唯一 scheduler 异步派发，字段引用由
  GC tracing 保持；Binder/跨进程 Parcel 明确失败。
- `KeyEvent(action,keyCode)` 与 API 19 timed/meta 构造器保存 action、Android keyCode、
  repeatCount 与 metaState；发布 `getUnicodeChar()`、`getUnicodeChar(metaState)`、
  `getMetaState()` 与 `getScanCode()`。HAL 保留物理 scancode、当前布局 key symbol、左右
  modifier 和 repeat，session 统一映射 Android keyCode/meta/Unicode 后分派非空事件。
  OnKeyListener、DispatcherState tracking/long-press 仍 deferred。
- 同一 WU 还闭合 native 调用锁交接、API19 Bionic TID、相对 timed futex、`/proc/meminfo`、
  watchdog、guest 逻辑 RWX/`ARM_cacheflush`、软件 Surface/Canvas 和 AudioTrack 基础状态。
- 不补 title 专属 native、不改变 survey neutral stub、不把 native fault 转成伪成功。

## 验收

- [x] RegisterNatives 命中后的执行 fault 不回退 `Java_`；未注册 native 仍明确失败。
- [x] DEX 字段支持 GetFieldID/GetStaticFieldID、继承查找、primitive/wide/reference 双向往返，
  object identity 与 local-reference 作用域保持稳定。
- [x] `Bitmap.Config`、`getPackageResourcePath()`、OnFocusChangeListener、ResultReceiver
  的 API 19 声明/分派/边界语义闭合。
- [x] View 子类可解析 `dispatchKeyEvent(KeyEvent)`，DOWN/UP/MULTIPLE 携带非空事件并分派到 override；
  SDL 物理 A 映射 `KEYCODE_A`，Unicode/meta/repeat 可由 guest 查询，不可打印键返回 0。
- [x] native loader、JNI registry、field store/ABI、graphics、surface、watchdog/futex 与
  architecture 定向回归通过。

## 验证证据

- 字段阶段：Windows Debug 增量构建；JNI registry、field store、guest field ABI、identity/
  import/field bridge 定向 10/10，architecture 6/6。
- graphics、AudioTrack、native context、watchdog/futex、ARM TLS、memory protection、managed
  surface 定向 9/9，192 assertions。
- OnFocusChangeListener 定向 2/2；ResultReceiver 本地同步/异步、Bundle identity、Binder
  Parcel 拒绝与 catalog 定向 2/2。
- KeyEvent/View dispatch、SDL 规范化和 session Android 输入映射定向通过；覆盖当前布局
  Unicode、显式 meta 重载、repeat、方向键与未知键 fail-closed。catalog、capability 与
  architecture 门禁通过。以上均为 Windows Debug 增量/定向验证，未执行全量 CTest。
- PVZ Release 无 survey reached run 进入标题画面；真实键盘输入越过
  `LoaderKeyboard.onKeyEvent()` 的 `getUnicodeChar()` 调用并可完成自定义用户名输入，原
  method resolve fault 未再出现。
- Release PVZ survey 已越过 `initNative()`、`m_LoaderKeyboard`、`Bitmap.Config`、
  `getPackageResourcePath()`、managed surface callback，并发布软件帧；后续运行中
  `presented=1`。旧 survey 还曾在约 f=2483/3106 由 `Thread-2` 报 A32 低地址写 fault
  （PC `0x60462edc`，访问 `0x1f84`）；该 native/title 边界问题不作为 JNI 字段验收结论。

状态：完成（能力保持 partial）。
