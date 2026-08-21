# DVM-49 · 统一 Java 对象与 intrinsic 状态底座

## 目标（一句话）

让 DexVM 解释路径与 guest JNI 共享会话级 Java 对象数组身份/存储，并用统一注册契约管理 intrinsic 宿主状态的追踪、清扫与克隆。

## 依赖

- DVM-8、DVM-42..46
- `.local/aosp/dalvik/vm/Jni.cpp`
- `.local/aosp/dalvik/vm/oo/Array.cpp`
- `.local/aosp/dalvik/vm/alloc/Visit.cpp`

## 交付

- `JniGuestObjectRegistry` 成为会话级普通对象 class identity 与 `JniObjectArrayStore` 的共同所有者；JNI array binder 不再私有持有第二份 object-array store。
- `JavaObjectModel` 通过显式 interop 契约发布/解析 class identity，解释器创建、读取、写入、克隆及 GC 清扫的 `Object[]` 全部落到同一 JNI store。
- JNI 创建的 `Object[]` 可按同一 identity 导入 DexVM；标记前同步导入 JNI 写入的元素，使 native 与解释路径看到同一对象图。
- DexVM→JNI 发布普通对象时登记精确 class identity；GC 清扫同步清除统一 registry 映射。
- intrinsic 宿主状态改为具名注册表，统一声明可选 trace、必需 sweep 与可选 clone hook；现有 throwable/builder/list/map 四张表全部迁入该注册机制。

## 有界差异

- 不引入完整 Dalvik `Object`/`ArrayObject` 布局、moving GC、reflection、finalizer 或跨 VM identity。
- string 与 primitive array 继续使用既有专用 session store；普通 instance payload 仍由 DexVM 持有。
- object-array store 的 class assignability 仍由既有 `JniClassRegistry`/DexVM linker 适配，不复制一套类型系统。
- 本 WU 只提供 intrinsic 状态生命周期底座，不新增特定 Java 类或游戏专属 handler。

## 验证

- `tests/dexvm/gc_tests.cpp`
- `tests/runtime/jni_guest_bindings_tests.cpp`
- `tests/runtime/jni_object_array_tests.cpp`

状态：完成。Windows/x64 `windows-msvc` focused suites 与完整 CTest 通过；证据同步于 `docs/state/CURRENT.md`。
