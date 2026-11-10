# 模块：有界 android.view Java 客户端

LayoutInflater 与 ContextThemeWrapper 使用 framework.jar 原版类，本目录不再提供替身。

Surface 是 holder/进程内纹理 producer 的有界 Java 引用客户端，公开构造、monitor、释放和
代际沿 API19；private native 显式携 owner 供 GC 清理，不含 compositor 指针。SurfaceTexture
仍为固定 framework.jar 原版类族，由 integration 接 native。Surface.release 只释放 producer
引用；consumer abandon 属于 SurfaceTexture。受管理的 holder 通过同一 Surface 执行软件
Canvas lock/post，private native 验证 owner 和代际。Parcel/SurfaceControl 尚未准入。
