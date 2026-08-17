# DVM-41 · java.lang primitive wrapper family 聚合与 API 19 公共接口补齐

## 目标（一句话）

以 Android 4.4.4 Luni 为唯一 API 真相，将 9 个 primitive wrapper 基础类聚合为
单一 DexVM intrinsic family，并用真实、确定性的 handler 补齐前 8 类 public API
及 Character 的高频子集。

## 依赖

- DVM-35
- DVM-40

## Source of truth

- 本地源码：`.local/asop/libcore/luni/src/main/java/java/lang/`
- 固定 tag：`android-4.4.4_r2.0.1`
- 已逐个读取：`Number.java`、`Byte.java`、`Short.java`、`Integer.java`、
  `Long.java`、`Float.java`、`Double.java`、`Boolean.java`、`Character.java`

inventory 只统计源码中的 public constructor、public method 与 public field；
泛型擦除后的 descriptor 使用 JNI 形式。intrinsic 为保存 boxed payload 额外声明的
private `value` 字段不计入 public field 数。

## 固定 inventory 汇总

| Class | superclass | interfaces | public ctor | public methods | public fields | 原有方法声明 | 本次新增方法声明 |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| Number | Object | Serializable | 1 | 6 | 0 | 0 | 7 |
| Byte | Number | Comparable | 2 | 19 | 4 | 0 | 21 |
| Short | Number | Comparable | 2 | 19 | 4 | 0 | 21 |
| Integer | Number | Comparable | 2 | 35 | 4 | 6 | 31 |
| Long | Number | Comparable | 2 | 35 | 4 | 5 | 32 |
| Float | Number | Comparable | 3 | 24 | 10 | 3 | 24 |
| Double | Number | Comparable | 2 | 23 | 10 | 3 | 22 |
| Boolean | Object | Serializable, Comparable | 2 | 11 | 3 | 3 | 10 |
| Character | Object | Serializable, Comparable | 1 selected | 43 selected | 65 | 0 | 44 selected |

“本次新增方法声明”含 constructor；Character 的 44 是本 WU selected inventory。

## 前 8 类完整 public API

### Number

- ctor：`Number()V`
- methods：`byteValue()B`、`shortValue()S`、`intValue()I`、`longValue()J`、
  `floatValue()F`、`doubleValue()D`
- fields：无

builder 尚无 abstract access flag；6 个 conversion 以 overridable handler 表达，
直接落到 Number 时明确抛 `AbstractMethodError`，具体 wrapper 均覆盖真实 handler。

### Byte

- ctor：`Byte(B)V`、`Byte(Ljava/lang/String;)V`
- methods：`byteValue()B`、`shortValue()S`、`intValue()I`、`longValue()J`、
  `floatValue()F`、`doubleValue()D`、`compareTo(Ljava/lang/Byte;)I`、
  `compare(BB)I`、`decode(Ljava/lang/String;)Ljava/lang/Byte;`、
  `equals(Ljava/lang/Object;)Z`、`hashCode()I`、
  `parseByte(Ljava/lang/String;)B`、`parseByte(Ljava/lang/String;I)B`、
  `toString()Ljava/lang/String;`、`toString(B)Ljava/lang/String;`、
  `toHexString(BZ)Ljava/lang/String;`、`valueOf(B)Ljava/lang/Byte;`、
  `valueOf(Ljava/lang/String;)Ljava/lang/Byte;`、
  `valueOf(Ljava/lang/String;I)Ljava/lang/Byte;`
- fields：`MAX_VALUE:B`、`MIN_VALUE:B`、`SIZE:I`、`TYPE:Ljava/lang/Class;`

### Short

- ctor：`Short(S)V`、`Short(Ljava/lang/String;)V`
- methods：六种 Number conversion；`compareTo(Ljava/lang/Short;)I`、
  `compare(SS)I`、`decode(Ljava/lang/String;)Ljava/lang/Short;`、
  `equals(Ljava/lang/Object;)Z`、`hashCode()I`、
  `parseShort(Ljava/lang/String;)S`、`parseShort(Ljava/lang/String;I)S`、
  `toString()Ljava/lang/String;`、`toString(S)Ljava/lang/String;`、
  `reverseBytes(S)S`、`valueOf(S)Ljava/lang/Short;`、
  `valueOf(Ljava/lang/String;)Ljava/lang/Short;`、
  `valueOf(Ljava/lang/String;I)Ljava/lang/Short;`
- fields：`MAX_VALUE:S`、`MIN_VALUE:S`、`SIZE:I`、`TYPE:Ljava/lang/Class;`

### Integer

- ctor：`Integer(I)V`、`Integer(Ljava/lang/String;)V`
- methods：`byteValue()B`、`shortValue()S`、`intValue()I`、`longValue()J`、
  `floatValue()F`、`doubleValue()D`、`compareTo(Ljava/lang/Integer;)I`、
  `compare(II)I`、`decode(Ljava/lang/String;)Ljava/lang/Integer;`、
  `equals(Ljava/lang/Object;)Z`、`hashCode()I`、`parseInt(Ljava/lang/String;)I`、
  `parseInt(Ljava/lang/String;I)I`、`toString()Ljava/lang/String;`、
  `toString(I)Ljava/lang/String;`、`toString(II)Ljava/lang/String;`、
  `toBinaryString(I)Ljava/lang/String;`、`toHexString(I)Ljava/lang/String;`、
  `toOctalString(I)Ljava/lang/String;`、`valueOf(I)Ljava/lang/Integer;`、
  `valueOf(Ljava/lang/String;)Ljava/lang/Integer;`、
  `valueOf(Ljava/lang/String;I)Ljava/lang/Integer;`、
  `getInteger(Ljava/lang/String;)Ljava/lang/Integer;`、
  `getInteger(Ljava/lang/String;I)Ljava/lang/Integer;`、
  `getInteger(Ljava/lang/String;Ljava/lang/Integer;)Ljava/lang/Integer;`、
  `highestOneBit(I)I`、`lowestOneBit(I)I`、`numberOfLeadingZeros(I)I`、
  `numberOfTrailingZeros(I)I`、`bitCount(I)I`、`rotateLeft(II)I`、
  `rotateRight(II)I`、`reverseBytes(I)I`、`reverse(I)I`、`signum(I)I`
- fields：`MAX_VALUE:I`、`MIN_VALUE:I`、`SIZE:I`、`TYPE:Ljava/lang/Class;`

### Long

- ctor：`Long(J)V`、`Long(Ljava/lang/String;)V`
- methods：`byteValue()B`、`shortValue()S`、`intValue()I`、`longValue()J`、
  `floatValue()F`、`doubleValue()D`、`compareTo(Ljava/lang/Long;)I`、
  `compare(JJ)I`、`decode(Ljava/lang/String;)Ljava/lang/Long;`、
  `equals(Ljava/lang/Object;)Z`、`hashCode()I`、`parseLong(Ljava/lang/String;)J`、
  `parseLong(Ljava/lang/String;I)J`、`toString()Ljava/lang/String;`、
  `toString(J)Ljava/lang/String;`、`toString(JI)Ljava/lang/String;`、
  `toBinaryString(J)Ljava/lang/String;`、`toHexString(J)Ljava/lang/String;`、
  `toOctalString(J)Ljava/lang/String;`、`valueOf(J)Ljava/lang/Long;`、
  `valueOf(Ljava/lang/String;)Ljava/lang/Long;`、
  `valueOf(Ljava/lang/String;I)Ljava/lang/Long;`、
  `getLong(Ljava/lang/String;)Ljava/lang/Long;`、
  `getLong(Ljava/lang/String;J)Ljava/lang/Long;`、
  `getLong(Ljava/lang/String;Ljava/lang/Long;)Ljava/lang/Long;`、
  `highestOneBit(J)J`、`lowestOneBit(J)J`、`numberOfLeadingZeros(J)I`、
  `numberOfTrailingZeros(J)I`、`bitCount(J)I`、`rotateLeft(JI)J`、
  `rotateRight(JI)J`、`reverseBytes(J)J`、`reverse(J)J`、`signum(J)I`
- fields：`MAX_VALUE:J`、`MIN_VALUE:J`、`SIZE:I`、`TYPE:Ljava/lang/Class;`

### Float

- ctor：`Float(F)V`、`Float(D)V`、`Float(Ljava/lang/String;)V`
- methods：六种 Number conversion；`compareTo(Ljava/lang/Float;)I`、`compare(FF)I`、
  `equals(Ljava/lang/Object;)Z`、`hashCode()I`、`isInfinite()Z`、`isInfinite(F)Z`、
  `isNaN()Z`、`isNaN(F)Z`、`parseFloat(Ljava/lang/String;)F`、
  `toString()Ljava/lang/String;`、`toString(F)Ljava/lang/String;`、
  `toHexString(F)Ljava/lang/String;`、`valueOf(F)Ljava/lang/Float;`、
  `valueOf(Ljava/lang/String;)Ljava/lang/Float;`、`floatToIntBits(F)I`、
  `floatToRawIntBits(F)I`、`intBitsToFloat(I)F`
- fields：`MAX_VALUE`、`MIN_VALUE`、`NaN`、`POSITIVE_INFINITY`、
  `NEGATIVE_INFINITY`、`MIN_NORMAL`（均为 `F`），`MAX_EXPONENT:I`、
  `MIN_EXPONENT:I`、`TYPE:Ljava/lang/Class;`、`SIZE:I`

### Double

- ctor：`Double(D)V`、`Double(Ljava/lang/String;)V`
- methods：`byteValue()B`、`shortValue()S`、`intValue()I`、`longValue()J`、
  `floatValue()F`、`doubleValue()D`、`compareTo(Ljava/lang/Double;)I`、
  `compare(DD)I`、`equals(Ljava/lang/Object;)Z`、`hashCode()I`、
  `isInfinite()Z`、`isInfinite(D)Z`、`isNaN()Z`、`isNaN(D)Z`、
  `parseDouble(Ljava/lang/String;)D`、`toString()Ljava/lang/String;`、
  `toString(D)Ljava/lang/String;`、`toHexString(D)Ljava/lang/String;`、
  `valueOf(D)Ljava/lang/Double;`、
  `valueOf(Ljava/lang/String;)Ljava/lang/Double;`、
  `doubleToLongBits(D)J`、`doubleToRawLongBits(D)J`、`longBitsToDouble(J)D`
- fields：与 Float 同名，浮点字段 descriptor 为 `D`，指数范围为 1023/-1022，
  `SIZE` 为 64

### Boolean

- ctor：`Boolean(Z)V`、`Boolean(Ljava/lang/String;)V`
- methods：`booleanValue()Z`、`compare(ZZ)I`、
  `compareTo(Ljava/lang/Boolean;)I`、`equals(Ljava/lang/Object;)Z`、`hashCode()I`、
  `getBoolean(Ljava/lang/String;)Z`、`parseBoolean(Ljava/lang/String;)Z`、
  `toString()Ljava/lang/String;`、`toString(Z)Ljava/lang/String;`、
  `valueOf(Z)Ljava/lang/Boolean;`、
  `valueOf(Ljava/lang/String;)Ljava/lang/Boolean;`
- fields：`TYPE:Ljava/lang/Class;`、`TRUE:Ljava/lang/Boolean;`、
  `FALSE:Ljava/lang/Boolean;`

## Character selected 与 deferred inventory

selected 共 **44**（1 ctor + 43 methods）：wrapper 9、radix 3、分类判断 17、
case conversion 4、UTF-16/code-point 11。descriptor 由 API-shape 测试逐项锁定，
包括 `digit(CI/II)`、char/int 双重载判断和转换、surrogate pair、U+10000 到
U+10FFFF 的 code-point 算法。

Character.java 从 wrapper ctor 起共有 **86** 个 public declaration；其余 **42** 个
deferred：`isSurrogate`、全部 `codePointAt/codePointBefore/toChars/codePointCount/
offsetByCodePoints` 重载、`getName/getNumericValue/getType/getDirectionality/isMirrored`、
`isAlphabetic/isIdeographic/isDefined/isIdentifierIgnorable`、Java/Unicode identifier、
`isJavaLetter/isJavaLetterOrDigit`、title-case，以及 selected 分类/大小写的非 ASCII
Unicode property 路径。它们不声明成假成功 stub；能力保持 partial。

公开常量按 Luni 补齐：radix/value/code-point/size/surrogate 边界、Unicode category
0..30（含 17 空洞）与 directionality -1..18，另有
`TYPE:Ljava/lang/Class;`（指向 primitive `C` class）。

## 实现约束与语义

- `ParseSignedRadix` 采用负向累积，支持 radix 2..36、正负号、精确 int/long
  overflow 与 Byte/Short 范围检查；所有解析失败均抛 `NumberFormatException`。
- `DecodeIntegral` 支持 `0x/0X/#/0` 前缀并拒绝前缀后的符号。
- bit API 只在 `uint32_t/uint64_t` bit pattern 上运算，避免 signed shift UB。
- FP bits 使用 `std::bit_cast`；canonical/raw NaN、signed zero、Infinity 饱和整数转换、
  Java compare/equals/hashCode 分开实现。解析/格式化使用 locale-free `charconv`，
  Java-visible token 固定为 `NaN/Infinity/-Infinity` 与大写 `E`。
- `Boolean.TRUE/FALSE` 在每 VM `<clinit>` 内建立，`valueOf` 返回 canonical identity；
  wrapper property API 读取 Interpreter 的 guest System property 表，不读宿主环境。
- Character surrogate/code-point 算法完整；分类与大小写不读 host locale，ASCII 路径
  确定，Luni 明文列出的 whitespace/space code point 同步实现。

## 文件与 catalog 结果

- 新增 `java_lang_primitive_wrappers.cpp`，最终 **877 行**；family TU 按契约允许超过
  通常 800 行。
- 删除旧 cpp **6** 个；不存在 Byte/Short/Character 独立 cpp。
- 9 个 `Declare_*` 全部位于 anonymous namespace；`catalog.h` 删除 **6** 个旧 class
  symbol，只暴露 `AppendJavaLangPrimitiveWrappers()`。
- family 共 **232** 个 method/ctor handler + 8 个 clinit handler，
  `Unimplemented` **0**。相对旧 surface 新增 **212** 个 method/ctor handler +
  7 个 clinit handler，即新增 handler **219**。

## 测试与验收结果

- catalog/class inventory：9 类存在且唯一，superclass/interfaces 精确检查。
- API shape：前 8 类完整固定 inventory、公开字段及 boxed `value` shape；Character
  44 个 selected descriptor 与 66 个 intrinsic field（65 public + `value`）。
- 行为：integral radix/overflow/decode/bit API、Boolean identity/property、FP special
  values/bits/format/conversion、Character ASCII/radix/whitespace/surrogate/code point。
- guest DEX：`p1.dexasm::primitiveWrappers()` 真实调用 Integer、Long、Byte、Short、
  Boolean、Float、Double、Character，期望 checksum 107。
- 新增 3 个 doctest case，并扩展 1 个真实 DEX case。
- `cmake --preset dev` / `cmake --build --preset dev`：当前 Windows 主机的
  Ninja/dev 配置在 Dynarmic 报 `Unsupported architecture encountered`，未生成本次
  产物。旧 dev 目录的 769/769 CTest 可运行但属于陈旧二进制，不作为本 WU 证据。
- `cmake --preset windows-msvc`：通过。
- `cmake --build --preset windows-msvc`：通过，含 warnings-as-errors。
- `ctest --preset windows-msvc`：**775/775 通过**，最终复跑 71.19 秒。
- `architecture.documentation_layout`：随全量 CTest 通过；CURRENT 为 6144-byte
  上限内滚动快照。
- `git diff --check`：通过。

## 剩余 limitation

- Character 的 42 个非 selected public declaration deferred，尤其完整 Unicode
  property 需要未来有界数据方案；`dexvm.intrinsics_java_core` 因此保持 `partial`。
- builder 不表达 Java `abstract` access flag；Number conversion 使用 overridable +
  显式 `AbstractMethodError` 的最接近模型，不为此扩大 ClassLinker。
- Integer/Long/Character valueOf cache identity 不是本 WU blocker；未使用生命周期
  不安全的 process-global VM cache。

## 不做

不引入 ICU/UnicodeData、BigInteger/BigDecimal、Math/StrictMath 扩展、java.text、
android.* intrinsic、title-specific 分支或静态自注册。
