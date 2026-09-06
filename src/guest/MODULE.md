# 模块：guest 生产代码

本目录存放交叉编译后在 guest 进程中执行的 OGPlay 自有生产代码。
不加入宿主 runtime 的编译目标，不依赖宿主 C++ API；通过 guest ABI 调用 bionic/JNI。
构建编排留在 tools，发行产物留在 data/android/<api>/lib。

- [crypto](crypto/MODULE.md)：API 19 ARM 的 AES 与证书签名校验 JNI 桥。
