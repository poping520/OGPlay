# API19 Harmony SHA1PRNG oracle

`sha1prng-api19.txt` 由固定 AOSP 的原版四个 Java 文件在宿主 OpenJDK 17 执行生成，
不使用宿主默认 SHA1PRNG。算法源文件没有修改，参考 JVM 独立于 OGPlay/DexVM。
平台 BlockGuard/EmptyArray 仅给编译适配；Streams.readFully 在参考程序中直接抛
AssertionError，保证五组显式 seed 向量未使用额外熵。SPI clinit 打开 /dev/urandom
但不读取。熵与错误语义另由 VFS 回归验证。

输入与输出：

- seed8_128：seed 0001020304050607，输出 128 bytes。
- seed64_64：seed 00..3f，输出 64 bytes，覆盖 seed 哈希分块。
- append_before_64：seed8 后再次播种 ff00，输出 64 bytes。
- reseed_after_43：seed8 输出并丢弃 21 bytes，再播种 ff00，输出 43 bytes。
- empty_seed_40：显式空 seed，输出 40 bytes。

生成程序为 `Sha1PrngApi19Oracle.java`。将以下三个编译适配器与四个固定算法源复制到
临时目录，使用 `javac -d classes <sources>`、`java -cp classes Sha1PrngApi19Oracle`
即可重现向量；适配器不进入产品或 BootDex：

```java
// dalvik/system/BlockGuard.java
package dalvik.system;
public final class BlockGuard {
    public interface Policy {}
    public static final Policy LAX_POLICY = new Policy() {};
    public static Policy getThreadPolicy() { return LAX_POLICY; }
    public static void setThreadPolicy(Policy p) {}
}
// libcore/io/Streams.java (separate file)
package libcore.io;
import java.io.*;
public final class Streams {
    public static void readFully(InputStream i, byte[] b, int o, int c) throws IOException {
        throw new AssertionError("Explicit-seed oracle must not request entropy");
    }
}
// libcore/util/EmptyArray.java (separate file)
package libcore.util;
public final class EmptyArray { public static final byte[] BYTE = new byte[0]; }
```

固定源码身份：

```json
{
  "source_tag": "android-4.4.4_r2.0.1",
  "algorithm_sources": [
    {
      "path": ".local/aosp/libcore/luni/src/main/java/org/apache/harmony/security/provider/crypto/CryptoProvider.java",
      "sha256": "ab7296ab8b2542aa21bc089796d9cdadf7a5ca61cf6363a3073d75946e6fea28"
    },
    {
      "path": ".local/aosp/libcore/luni/src/main/java/org/apache/harmony/security/provider/crypto/SHA1PRNG_SecureRandomImpl.java",
      "sha256": "5876a54dc8d34d1a66e35150af1ba1fe28134c2329b62657da82296486269913"
    },
    {
      "path": ".local/aosp/libcore/luni/src/main/java/org/apache/harmony/security/provider/crypto/SHA1Impl.java",
      "sha256": "43ac1a677ed31a33483e2278054e42903bcf187c19e46a1080517ae38e36129d"
    },
    {
      "path": ".local/aosp/libcore/luni/src/main/java/org/apache/harmony/security/provider/crypto/SHA1Constants.java",
      "sha256": "4202a234c49242e6e254f00ae98dfaf5e6eeaf2d5f13ab582640adb26ce1b38c"
    }
  ],
  "scope": "Unmodified API19 source on host JVM; explicit seed only. Platform BlockGuard/EmptyArray adapters; entropy adapter throws on any read."
}
```
