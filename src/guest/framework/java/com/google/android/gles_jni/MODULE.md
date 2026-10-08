# 子模块：API19 GLImpl 客户端

保留 Android 4.4.4_r2.0.1 GLImpl 的接口实现、构造器、Buffer 强引用与 Java 转发；
编入 BootDex。源文件来自 AOSP frameworks/base/opengl/java/com/google/android/gles_jni。
GL/GL10/GL11 及扩展接口从固定 framework2.jar 配方导入，不重复定义。

仅替换 allowIndirectBuffers 的系统包管理查询为显式 native，由当前 APK targetSdk 决定
API19 的兼容条件；不调用 AppGlobals/IPackageManager/Binder。真正的堆 Buffer 持久
映射尚未实现：现代应用报 IAE，旧应用兼容路径记账报 UOE，禁止保存临时编组地址。

每个 javax EGLContext 的普通字段拥有独立 GLImpl；对象不绑定调用目标 Context，
GL 操作仍由调用线程的 EGL current 决定。client Buffer 强边经 Java 字段由 GC 追踪。
仅已发布 native GLES 目录提供实际操作，超出目录的调用明确失败，不引入系统 GL 驱动。
