# 模块：runtime

## 职责

装载真实 Bionic，并实现 syscall、完整 JNI/JavaVM、可选 DEX 解释器和 Android 框架 HLE。

## 公共 API

M2/M3 定义。子域按 `bionic/syscall/jni/dex/framework` 分文件，禁止巨型 dispatcher。

## 不变量

- 未实现调用记入能力账本并明确失败；syscall 返回 ENOSYS。
- JNIEnv 全表完成前，缺槽位必须 trap，不得静默返回零。
- 框架类绑定声明式；对象模型允许宿主对象与未来 VM 对象共存。

## 禁止

- 不实现 Binder/system_server/Zygote/完整 ART。
- 不包含包名、厂商名、绝对补丁地址或游戏专属 Java 回调。

## 测试

`tests/runtime/` 契约基准来自 AOSP/真机行为。

