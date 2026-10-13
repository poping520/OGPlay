# 模块契约索引

依赖方向从上到下：`frontend/agent/session → runtime/gles/audio/video/input/loader → cpu/memory → hal → core`。
下层不得引用上层；跨层通知使用显式接口。各模块的职责、接口、子模块依赖与支持边界见其
MODULE.md；能力状态见 [能力账本](../../capabilities.toml)。

新增或删除 MODULE.md 时同步更新下表。

| 契约 | 标题 |
| --- | --- |
| [`src/agent`](../../src/agent/MODULE.md) | agent |
| [`src/audio`](../../src/audio/MODULE.md) | audio |
| [`src/core`](../../src/core/MODULE.md) | core |
| [`src/cpu`](../../src/cpu/MODULE.md) | cpu |
| [`src/frontend`](../../src/frontend/MODULE.md) | frontend |
| [`src/frontend/gui`](../../src/frontend/gui/MODULE.md) | frontend/gui |
| [`src/gles`](../../src/gles/MODULE.md) | gles |
| [`src/guest`](../../src/guest/MODULE.md) | guest 生产代码 |
| [`src/guest/crypto`](../../src/guest/crypto/MODULE.md) | BootDex guest crypto JNI 源码 |
| [`src/guest/crypto/java`](../../src/guest/crypto/java/MODULE.md) | KeyStore/JSSE guest Java |
| [`src/guest/framework`](../../src/guest/framework/MODULE.md) | 有界 Android framework guest Java |
| [`src/guest/framework/java`](../../src/guest/framework/java/MODULE.md) | framework Java 源码 |
| [`src/guest/framework/java/android/app`](../../src/guest/framework/java/android/app/MODULE.md) | 有界 android.app Java 客户端 |
| [`src/guest/framework/java/android/graphics`](../../src/guest/framework/java/android/graphics/MODULE.md) | 有界 BitmapFactory Java 客户端 |
| [`src/guest/framework/java/android/hardware`](../../src/guest/framework/java/android/hardware/MODULE.md) | 有界传感器客户端 |
| [`src/guest/framework/java/android/net`](../../src/guest/framework/java/android/net/MODULE.md) | 有界连接状态客户端 |
| [`src/guest/framework/java/android/nfc`](../../src/guest/framework/java/android/nfc/MODULE.md) | 无 NFC 设备的 API19 Java 客户端 |
| [`src/guest/framework/java/android/view`](../../src/guest/framework/java/android/view/MODULE.md) | 有界 android.view Java 客户端 |
| [`src/guest/framework/java/com/google/android/gles_jni`](../../src/guest/framework/java/com/google/android/gles_jni/MODULE.md) | API19 GLImpl 客户端 |
| [`src/guest/icu`](../../src/guest/icu/MODULE.md) | BootDex guest ICU JNI |
| [`src/guest/io`](../../src/guest/io/MODULE.md) | BootDex IO 平台端点 |
| [`src/guest/io/java/org/ogplay/io`](../../src/guest/io/java/org/ogplay/io/MODULE.md) | System 诊断输出流 |
| [`src/guest/math`](../../src/guest/math/MODULE.md) | guest 有界整数算术 |
| [`src/guest/zip`](../../src/guest/zip/MODULE.md) | guest ZIP native 边界 |
| [`src/hal`](../../src/hal/MODULE.md) | hal |
| [`src/hal/linux`](../../src/hal/linux/MODULE.md) | hal/linux |
| [`src/hal/macos`](../../src/hal/macos/MODULE.md) | hal/macos |
| [`src/hal/windows`](../../src/hal/windows/MODULE.md) | hal/windows |
| [`src/input`](../../src/input/MODULE.md) | input |
| [`src/loader`](../../src/loader/MODULE.md) | loader |
| [`src/memory`](../../src/memory/MODULE.md) | memory |
| [`src/runtime`](../../src/runtime/MODULE.md) | runtime |
| [`src/runtime/bionic`](../../src/runtime/bionic/MODULE.md) | runtime/bionic |
| [`src/runtime/boundary`](../../src/runtime/boundary/MODULE.md) | runtime/boundary |
| [`src/runtime/boundary/facade`](../../src/runtime/boundary/facade/MODULE.md) | boundary facade |
| [`src/runtime/boundary/modules`](../../src/runtime/boundary/modules/MODULE.md) | boundary concrete modules |
| [`src/runtime/boundary/modules/android`](../../src/runtime/boundary/modules/android/MODULE.md) | Android NDK boundary |
| [`src/runtime/boundary/modules/bitmap`](../../src/runtime/boundary/modules/bitmap/MODULE.md) | API19 libjnigraphics |
| [`src/runtime/boundary/modules/egl`](../../src/runtime/boundary/modules/egl/MODULE.md) | EGL |
| [`src/runtime/boundary/modules/gles1`](../../src/runtime/boundary/modules/gles1/MODULE.md) | GLES1 |
| [`src/runtime/boundary/modules/gles2`](../../src/runtime/boundary/modules/gles2/MODULE.md) | GLES2 共用入口 |
| [`src/runtime/boundary/modules/gles3`](../../src/runtime/boundary/modules/gles3/MODULE.md) | runtime/boundary/modules/gles3 |
| [`src/runtime/boundary/modules/opensles`](../../src/runtime/boundary/modules/opensles/MODULE.md) | runtime/boundary/modules/opensles |
| [`src/runtime/boundary/services`](../../src/runtime/boundary/services/MODULE.md) | boundary services |
| [`src/runtime/common`](../../src/runtime/common/MODULE.md) | runtime/common |
| [`src/runtime/database`](../../src/runtime/database/MODULE.md) | runtime/database |
| [`src/runtime/debug`](../../src/runtime/debug/MODULE.md) | runtime/debug |
| [`src/runtime/dexvm`](../../src/runtime/dexvm/MODULE.md) | runtime/dexvm |
| [`src/runtime/dexvm/intrinsics`](../../src/runtime/dexvm/intrinsics/MODULE.md) | runtime/dexvm/intrinsics |
| [`src/runtime/execution`](../../src/runtime/execution/MODULE.md) | runtime/execution |
| [`src/runtime/framework`](../../src/runtime/framework/MODULE.md) | runtime/framework |
| [`src/runtime/integration`](../../src/runtime/integration/MODULE.md) | runtime/integration |
| [`src/runtime/integration/dexvm_android`](../../src/runtime/integration/dexvm_android/MODULE.md) | runtime/integration/dexvm_android |
| [`src/runtime/jni`](../../src/runtime/jni/MODULE.md) | runtime/jni |
| [`src/runtime/jni_guest`](../../src/runtime/jni_guest/MODULE.md) | runtime/jni_guest |
| [`src/runtime/syscall`](../../src/runtime/syscall/MODULE.md) | runtime/syscall |
| [`src/runtime/ui`](../../src/runtime/ui/MODULE.md) | runtime/ui |
| [`src/runtime/vfs`](../../src/runtime/vfs/MODULE.md) | runtime/vfs |
| [`src/session`](../../src/session/MODULE.md) | session |
| [`src/video`](../../src/video/MODULE.md) | video |
