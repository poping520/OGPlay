# PKCS12 SecretKeyFactory oracle

`SecretKeyFactoryOracle.java` 仅为独立参考程序，不进入 BootDex 或生产依赖。
使用 Maven `org.bouncycastle:bcprov-jdk15on:1.50`，JAR SHA-256：
`115b14a02b91fb03cb1866d6b311d33cd5518a9d8524dd63a14f19571e420404`。

六个输出固定在 `tests/runtime/native_library_loader_tests.cpp` 的 DVM-199 区段，
经标准 SecretKeyFactory 公开调用在双解释器/guest libcrypto 路径比较。
输入为 ASCII、空密码、中文/NUL/代理对密码；盐 `0102030405060708`，迭代 1/1024，
请求长度 128（命名算法依然生成 256 位）。

API19 语义另从本地固定 `bouncycastle.jar` 核对：
`AES$PBEWithSHAAnd256BitAESBC` 使用 scheme=PKCS12、digest=SHA1、keySize=256、ivSize=128；
`PBESecretKeyFactory` 与 `BCPBEKey` 定义无盐密码编码及已派生密钥的 RAW 编码；
`BaseSecretKeyFactory` 定义原始字节规格转换和按规范算法名检查的 translateKey。
该 JAR SHA-256：`a849c4c9c99886ff63007b43ca9f470d83e686bc3a53d472d9c3a3df872d3a14`。
本次未连接 API19 设备；BC 1.50 向量不是新的 Android 真机验收。
