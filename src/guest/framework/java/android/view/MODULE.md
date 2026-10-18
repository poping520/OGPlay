# 模块：有界 android.view Java 客户端

LayoutInflater 保存 Context、同步和重载协议；from 查询 Context 服务，cloneInContext
创建绑定新 Context 的独立对象。ContextThemeWrapper 按 API19 克隆并缓存 inflater，
主题资源只保留身份，不提供完整主题解析。Activity 委托 Window 获取同一服务。
唯一 nativeInflate 边界复用 integration 的资源/AXML/注册控件构建与 UiTree。

支持 resource ID 的两种 inflate 重载；null root 生成独立子树，有 root 时生成父容器
对应的 LayoutParams，attach 仅追加并返回 root。merge 仅允许非空 root 且 attach。
解析器重载、自定义 Factory/反射构造、完整 AttributeSet/theme 不支持；不引入系统服务。
来源：固定 AOSP Android 4.4.4 LayoutInflater/ContextThemeWrapper 的有界客户端改编，
保留 Apache-2.0 头。平台事实与事务边界见 ADR-0126。

Surface 是 holder/进程内纹理 producer 的有界 Java 引用客户端，公开构造、monitor、释放和
代际沿 API19；private native 显式携 owner 供 GC 清理，不含 compositor 指针。SurfaceTexture
仍为固定 framework.jar 原版类族，由 integration 接 native。Surface.release 只释放 producer
引用；consumer abandon 属于 SurfaceTexture。Canvas/Parcel/SurfaceControl 尚未准入。
