# DVM-45 · GC-B 宿主状态析构

## 目标（一句话）

让不可达 host-backed 对象在清扫时通过类声明绑定的 handler 恰好释放一次宿主资源。

## 依赖

- DVM-44
- `docs/design/dexvm/09-gc.md` §6

## 交付

- `IntrinsicClassBuilder::HostStateDestructor` 把析构 handler 与 intrinsic class 声明同址绑定。
- linker 将 handler 固化进 `LinkedClass`；只对 `VmObjectKind::host_backed` 调用。
- 析构发生在 store/identity/record 释放之前，不执行 guest 字节码，不等同于 Java finalizer。
- 未声明 destructor 的类保持 no-op；本次盘点没有生产 intrinsic 使用 `NewHostBacked`，
  因而没有需要迁移的既有资源类，测试类锁定声明通道与调用时序。

## 机器验收

- 死亡 host-backed object 调用一次；存活 object 不调用；幂等 GC 不重复调用。
- 随后撤销唯一根，原存活 object 再调用一次。

## 不做

不实现 `finalize()`、FinalizerReference、ReferenceQueue 或 guest callback。
