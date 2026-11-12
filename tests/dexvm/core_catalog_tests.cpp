#include "android_value_fixture.h"
#include "boot_dex.h"
#include "container_traversal_fixture.h"
// DexVM stage-1 interpreter conformance over dexasm fixtures.
// Expected values are recorded with their semantic source: AOSP
// vm/mterp/c/OP_*.cpp at the pinned baseline (07 §2 mode B) or the Dalvik
// bytecode specification (docs/dalvik-bytecode at the same tag).

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/text.h"
#include "ogplay/hal/clock.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"

#include "interpreter_fixture.h"

using namespace ogplay::test::interpreter;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("dexvm floating String valueOf and wrapper rendering use libcore RealToString") {
    WithEachBackend([](InterpreterConfig config) {
        Vm vm(config);
        VmThreadRuntime threads(vm.interpreter);
        for (bool wide : {false, true}) {
            for (const auto& [value, expected] :
                 std::vector<std::pair<double, std::string>>{{64.0, "64.0"},
                                                             {-0.0, "-0.0"},
                                                             {1e8, "1.0E8"},
                                                             {std::numeric_limits<double>::infinity(), "Infinity"},
                                                             {std::numeric_limits<double>::quiet_NaN(), "NaN"}}) {
                const auto argument = wide ? VmValue::Double(value) : VmValue::Float(static_cast<float>(value));
                const auto signature = wide ? "(D)Ljava/lang/String;" : "(F)Ljava/lang/String;";
                const auto string = [&](const char* owner, const char* name) {
                    const auto result = vm.CallStatic(owner, name, signature, {argument});
                    REQUIRE_FALSE(result.exception.IsValid());
                    return vm.interpreter.StringUtf8(result.value.ref);
                };
                CHECK(string("Ljava/lang/String;", "valueOf") == expected);
                CHECK(string(wide ? "Ljava/lang/Double;" : "Ljava/lang/Float;", "toString") == expected);
            }
        }
    });
}

TEST_CASE("dexvm core intrinsic catalog is unique and structurally stable") {
    auto catalog = CoreIntrinsicCatalog();
    std::set<std::string> descriptors;
    std::set<std::string> intentionally_unimplemented = {
        // Explicit bounded native/platform boundaries added since the original inventory.
        "Ldalvik/system/BaseDexClassLoader;.findLibrary(Ljava/lang/String;)Ljava/lang/String;",
        "Ljava/net/ProxySelector;.setDefault(Ljava/net/ProxySelector;)V",
        "Ljava/net/ProxySelector;.select(Ljava/net/URI;)Ljava/util/List;",
        "Ljava/net/ProxySelector;.connectFailed(Ljava/net/URI;Ljava/net/SocketAddress;Ljava/io/IOException;)V",
        "Llibcore/io/Posix;.preadBytes(Ljava/io/FileDescriptor;Ljava/lang/Object;IIJ)I",
        "Llibcore/io/Posix;.pwriteBytes(Ljava/io/FileDescriptor;Ljava/lang/Object;IIJ)I",
        "Llibcore/io/Posix;.recvfromBytes(Ljava/io/FileDescriptor;Ljava/lang/Object;IIILjava/net/InetSocketAddress;)I",
        "Llibcore/io/Posix;.sendtoBytes(Ljava/io/FileDescriptor;Ljava/lang/Object;IIILjava/net/InetAddress;I)I",
        "Llibcore/io/Posix;.umaskImpl(I)I",
        "Llibcore/io/Posix;.accept(Ljava/io/FileDescriptor;Ljava/net/InetSocketAddress;)Ljava/io/FileDescriptor;",
        "Llibcore/io/Posix;.bind(Ljava/io/FileDescriptor;Ljava/net/InetAddress;I)V",
        "Llibcore/io/Posix;.chown(Ljava/lang/String;II)V",
        "Llibcore/io/Posix;.connect(Ljava/io/FileDescriptor;Ljava/net/InetAddress;I)V",
        "Llibcore/io/Posix;.dup2(Ljava/io/FileDescriptor;I)Ljava/io/FileDescriptor;",
        "Llibcore/io/Posix;.environ()[Ljava/lang/String;",
        "Llibcore/io/Posix;.execv(Ljava/lang/String;[Ljava/lang/String;)V",
        "Llibcore/io/Posix;.execve(Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)V",
        "Llibcore/io/Posix;.fchmod(Ljava/io/FileDescriptor;I)V",
        "Llibcore/io/Posix;.fchown(Ljava/io/FileDescriptor;II)V",
        "Llibcore/io/Posix;.fstatvfs(Ljava/io/FileDescriptor;)Llibcore/io/StructStatVfs;",
        "Llibcore/io/Posix;.getegid()I",
        "Llibcore/io/Posix;.getenv(Ljava/lang/String;)Ljava/lang/String;",
        "Llibcore/io/Posix;.geteuid()I",
        "Llibcore/io/Posix;.getgid()I",
        "Llibcore/io/Posix;.getpeername(Ljava/io/FileDescriptor;)Ljava/net/SocketAddress;",
        "Llibcore/io/Posix;.getpid()I",
        "Llibcore/io/Posix;.getppid()I",
        "Llibcore/io/Posix;.getpwnam(Ljava/lang/String;)Llibcore/io/StructPasswd;",
        "Llibcore/io/Posix;.getpwuid(I)Llibcore/io/StructPasswd;",
        "Llibcore/io/Posix;.getsockname(Ljava/io/FileDescriptor;)Ljava/net/SocketAddress;",
        "Llibcore/io/Posix;.getsockoptByte(Ljava/io/FileDescriptor;II)I",
        "Llibcore/io/Posix;.getsockoptInAddr(Ljava/io/FileDescriptor;II)Ljava/net/InetAddress;",
        "Llibcore/io/Posix;.getsockoptInt(Ljava/io/FileDescriptor;II)I",
        "Llibcore/io/Posix;.getsockoptLinger(Ljava/io/FileDescriptor;II)Llibcore/io/StructLinger;",
        "Llibcore/io/Posix;.getsockoptTimeval(Ljava/io/FileDescriptor;II)Llibcore/io/StructTimeval;",
        "Llibcore/io/Posix;.getsockoptUcred(Ljava/io/FileDescriptor;II)Llibcore/io/StructUcred;",
        "Llibcore/io/Posix;.gettid()I",
        "Llibcore/io/Posix;.getuid()I",
        "Llibcore/io/Posix;.if_indextoname(I)Ljava/lang/String;",
        "Llibcore/io/Posix;.ioctlInetAddress(Ljava/io/FileDescriptor;ILjava/lang/String;)Ljava/net/InetAddress;",
        "Llibcore/io/Posix;.kill(II)V",
        "Llibcore/io/Posix;.lchown(Ljava/lang/String;II)V",
        "Llibcore/io/Posix;.listen(Ljava/io/FileDescriptor;I)V",
        "Llibcore/io/Posix;.mincore(JJ[B)V",
        "Llibcore/io/Posix;.mlock(JJ)V",
        "Llibcore/io/Posix;.mmap(JJIILjava/io/FileDescriptor;J)J",
        "Llibcore/io/Posix;.msync(JJI)V",
        "Llibcore/io/Posix;.munlock(JJ)V",
        "Llibcore/io/Posix;.munmap(JJ)V",
        "Llibcore/io/Posix;.pipe()[Ljava/io/FileDescriptor;",
        "Llibcore/io/Posix;.poll([Llibcore/io/StructPollfd;I)I",
        "Llibcore/io/Posix;.readv(Ljava/io/FileDescriptor;[Ljava/lang/Object;[I[I)I",
        "Llibcore/io/Posix;.sendfile(Ljava/io/FileDescriptor;Ljava/io/FileDescriptor;Llibcore/util/MutableLong;J)J",
        "Llibcore/io/Posix;.setegid(I)V",
        "Llibcore/io/Posix;.setenv(Ljava/lang/String;Ljava/lang/String;Z)V",
        "Llibcore/io/Posix;.seteuid(I)V",
        "Llibcore/io/Posix;.setgid(I)V",
        "Llibcore/io/Posix;.setsid()I",
        "Llibcore/io/Posix;.setsockoptByte(Ljava/io/FileDescriptor;III)V",
        "Llibcore/io/Posix;.setsockoptGroupReq(Ljava/io/FileDescriptor;IILlibcore/io/StructGroupReq;)V",
        "Llibcore/io/Posix;.setsockoptIfreq(Ljava/io/FileDescriptor;IILjava/lang/String;)V",
        "Llibcore/io/Posix;.setsockoptInt(Ljava/io/FileDescriptor;III)V",
        "Llibcore/io/Posix;.setsockoptIpMreqn(Ljava/io/FileDescriptor;III)V",
        "Llibcore/io/Posix;.setsockoptLinger(Ljava/io/FileDescriptor;IILlibcore/io/StructLinger;)V",
        "Llibcore/io/Posix;.setsockoptTimeval(Ljava/io/FileDescriptor;IILlibcore/io/StructTimeval;)V",
        "Llibcore/io/Posix;.setuid(I)V",
        "Llibcore/io/Posix;.shutdown(Ljava/io/FileDescriptor;I)V",
        "Llibcore/io/Posix;.socket(III)Ljava/io/FileDescriptor;",
        "Llibcore/io/Posix;.socketpair(IIILjava/io/FileDescriptor;Ljava/io/FileDescriptor;)V",
        "Llibcore/io/Posix;.strsignal(I)Ljava/lang/String;",
        "Llibcore/io/Posix;.symlink(Ljava/lang/String;Ljava/lang/String;)V",
        "Llibcore/io/Posix;.tcdrain(Ljava/io/FileDescriptor;)V",
        "Llibcore/io/Posix;.tcsendbreak(Ljava/io/FileDescriptor;I)V",
        "Llibcore/io/Posix;.unsetenv(Ljava/lang/String;)V",
        "Llibcore/io/Posix;.waitpid(ILlibcore/util/MutableInt;I)I",
        "Llibcore/io/Posix;.writev(Ljava/io/FileDescriptor;[Ljava/lang/Object;[I[I)I",

        // DVM-102: precise native failures frozen in api19.json (date_family_audit).
        "Llibcore/icu/ICU;.addLikelySubtags(Ljava/lang/String;)Ljava/lang/String;",
        "Llibcore/icu/ICU;.getAvailableBreakIteratorLocalesNative()[Ljava/lang/String;",
        "Llibcore/icu/ICU;.getAvailableCalendarLocalesNative()[Ljava/lang/String;",
        "Llibcore/icu/ICU;.getAvailableCollatorLocalesNative()[Ljava/lang/String;",
        "Llibcore/icu/ICU;.getAvailableCurrencyCodes()[Ljava/lang/String;",
        "Llibcore/icu/ICU;.getAvailableDateFormatLocalesNative()[Ljava/lang/String;",
        "Llibcore/icu/ICU;.getAvailableNumberFormatLocalesNative()[Ljava/lang/String;",
        "Llibcore/icu/ICU;.getCldrVersion()Ljava/lang/String;",
        "Llibcore/icu/ICU;.getIcuVersion()Ljava/lang/String;",
        "Llibcore/icu/ICU;.getScript(Ljava/lang/String;)Ljava/lang/String;",
        "Llibcore/icu/ICU;.getUnicodeVersion()Ljava/lang/String;",
        "Llibcore/icu/NativeDecimalFormat;.formatDigitList(JLjava/lang/String;Llibcore/icu/NativeDecimalFormat$FieldPositionIterator;)[C",
        "Llibcore/icu/NativeDecimalFormat;.formatDouble(JDLlibcore/icu/NativeDecimalFormat$FieldPositionIterator;)[C",

        "Ljava/lang/System;.currentTimeMillis()J",
        "Ljava/lang/System;.nanoTime()J",
        "Ljava/lang/System;.load(Ljava/lang/String;)V",
        "Ldalvik/system/VMRuntime;.bootClassPath()Ljava/lang/String;",
        "Ldalvik/system/VMRuntime;.classPath()Ljava/lang/String;",
        "Ldalvik/system/VMRuntime;.preloadDexCaches()V",
        "Ldalvik/system/VMRuntime;.properties()[Ljava/lang/String;",
        "Ldalvik/system/VMRuntime;.setTargetSdkVersion(I)V",
        "Ldalvik/system/VMRuntime;.vmLibrary()Ljava/lang/String;",
        "Ldalvik/system/VMRuntime;.vmVersion()Ljava/lang/String;",
        "Ljava/lang/Runtime;.nativeLoad(Ljava/lang/String;Ljava/lang/ClassLoader;Ljava/lang/String;)Ljava/lang/String;",
        "Ljava/lang/AssertionError;.<init>"
        "(Ljava/lang/String;Ljava/lang/Throwable;)V",
        "Ljava/lang/AssertionError;.<init>(Ljava/lang/Object;)V",
        "Ljava/lang/AssertionError;.<init>(Z)V",
        "Ljava/lang/AssertionError;.<init>(C)V",
        "Ljava/lang/AssertionError;.<init>(I)V",
        "Ljava/lang/AssertionError;.<init>(J)V",
        "Ljava/lang/AssertionError;.<init>(F)V",
        "Ljava/lang/AssertionError;.<init>(D)V",
        "Ljava/lang/ReflectiveOperationException;.<init>"
        "(Ljava/lang/Throwable;)V",
        "Ljava/lang/ReflectiveOperationException;.<init>"
        "(Ljava/lang/String;Ljava/lang/Throwable;)V",
        "Ljava/lang/SecurityException;.<init>"
        "(Ljava/lang/String;Ljava/lang/Throwable;)V",
        "Ljava/lang/SecurityException;.<init>(Ljava/lang/Throwable;)V",
        "Ljava/lang/TypeNotPresentException;.<init>"
        "(Ljava/lang/String;Ljava/lang/Throwable;)V",
        "Ljava/io/Closeable;.close()V",
        "Ljava/lang/CharSequence;.charAt(I)C",
        "Ljava/lang/CharSequence;.subSequence(II)Ljava/lang/CharSequence;",
        "Ljava/lang/CharSequence;.toString()Ljava/lang/String;",
        "Ljava/lang/Comparable;.compareTo(Ljava/lang/Object;)I",
        "Ljava/lang/Iterable;.iterator()Ljava/util/Iterator;",
        "Ljava/lang/Readable;.read(Ljava/nio/CharBuffer;)I",
        "Ljava/lang/Runnable;.run()V",
        "Ljava/lang/Thread;.stop()V",
        "Ljava/lang/Thread;.stop(Ljava/lang/Throwable;)V",
        "Ljava/lang/Thread;.suspend()V",
        "Ljava/lang/Thread;.resume()V",
        "Ljava/lang/Thread;.destroy()V",
        "Ljava/lang/BootClassLoader;.<init>()V",
        "Ljava/math/NativeBN;.BN_add(JJJ)V",
        "Ljava/math/NativeBN;.BN_add_word(JI)V",
        "Ljava/math/NativeBN;.BN_bin2bn([BIZJ)V",
        "Ljava/math/NativeBN;.BN_bn2bin(J)[B",
        "Ljava/math/NativeBN;.BN_bn2dec(J)Ljava/lang/String;",
        "Ljava/math/NativeBN;.BN_bn2hex(J)Ljava/lang/String;",
        "Ljava/math/NativeBN;.BN_div(JJJJ)V",
        "Ljava/math/NativeBN;.BN_exp(JJJ)V",
        "Ljava/math/NativeBN;.BN_gcd(JJJ)V",
        "Ljava/math/NativeBN;.BN_generate_prime_ex(JIZJJJ)V",
        "Ljava/math/NativeBN;.BN_hex2bn(JLjava/lang/String;)I",
        "Ljava/math/NativeBN;.BN_is_bit_set(JI)Z",
        "Ljava/math/NativeBN;.BN_is_prime_ex(JIJ)Z",
        "Ljava/math/NativeBN;.BN_mod_exp(JJJJ)V",
        "Ljava/math/NativeBN;.BN_mod_inverse(JJJ)V",
        "Ljava/math/NativeBN;.BN_mod_word(JI)I",
        "Ljava/math/NativeBN;.BN_mul(JJJ)V",
        "Ljava/math/NativeBN;.BN_mul_word(JI)V",
        "Ljava/math/NativeBN;.BN_nnmod(JJJ)V",
        "Ljava/math/NativeBN;.BN_shift(JJI)V",
        "Ljava/math/NativeBN;.BN_sub(JJJ)V",
        "Ljava/math/NativeBN;.litEndInts2bn([IIZJ)V",
        "Ljava/math/NativeBN;.twosComp2bn([BIJ)V",
    };
    // Explicit boundaries added by DVM-105/106/109 after the original catalog snapshot.
    for (
        const auto* signature :
        {"Ljava/lang/reflect/Proxy;.constructorPrototype(Ljava/lang/reflect/InvocationHandler;)V",
         "Ldalvik/system/VMStack;.fillStackTraceElements(Ljava/lang/Thread;[Ljava/lang/StackTraceElement;)I",
         "Ldalvik/system/VMStack;.getThreadStackTrace(Ljava/lang/Thread;)[Ljava/lang/StackTraceElement;",
         "Ldalvik/system/VMStack;.getCallingClassLoader()Ljava/lang/ClassLoader;",
         "Ldalvik/system/VMStack;.getStackClass2()Ljava/lang/Class;",
         "Ljava/io/ObjectOutputStream;.getFieldL(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/Object;",
         "Lorg/ogplay/security/OsRandom;.engineSetSeed([B)V"})
        intentionally_unimplemented.insert(signature);
    for (const auto* digest : {"SHA1", "SHA224", "SHA256", "SHA384", "SHA512"})
        for (const auto* key : {"RSA", "ECDSA"})
            for (const auto* method : {"engineInitSign(Ljava/security/PrivateKey;)V", "engineSign()[B",
                                       "engineSetParameter(Ljava/lang/String;Ljava/lang/Object;)V",
                                       "engineGetParameter(Ljava/lang/String;)Ljava/lang/Object;"})
                intentionally_unimplemented.insert(std::string("Lorg/ogplay/security/Verify") + digest + "with" + key +
                                                   ";." + method);
    for (const auto& declaration : catalog) {
        CHECK(descriptors.insert(declaration.descriptor).second);
        for (const auto& method : declaration.methods) {
            if (!method.implementation && !method.guest_native) {
                CAPTURE(declaration.descriptor);
                CAPTURE(method.name);
                CAPTURE(method.descriptor);
                const auto is_abstract = (method.access_flags & 0x0400U) != 0U;
                CHECK((is_abstract || intentionally_unimplemented.contains(declaration.descriptor + "." + method.name +
                                                                           method.descriptor)));
            }
        }
    }

    CHECK(descriptors.contains("Ljava/net/URL;"));
    CHECK(descriptors.contains("Ljava/nio/charset/Charset;"));
    CHECK_FALSE(descriptors.contains("Ljava/util/Locale;"));
    CHECK_FALSE(descriptors.contains("Ljavax/net/ssl/SSLSocketFactory;"));
    CHECK(descriptors.contains("Ljavax/xml/parsers/SAXParserFactory;"));
    CHECK_FALSE(descriptors.contains("Lorg/xml/sax/XMLReader;"));

    DexClassLinker boot_linker;
    boot_linker.RegisterIntrinsics(catalog);
    ogplay::test::RegisterBootDex(boot_linker);
    boot_linker.Link();
    CHECK(boot_linker.Class(boot_linker.ResolveDescriptor("Ljavax/net/ssl/SSLSocketFactory;")).is_boot_dex);
    for (const auto* name :
         {"Ljava/lang/CharSequence;", "Ljava/lang/Cloneable;", "Ljava/lang/Comparable;", "Ljava/lang/Readable;",
          "Ljava/lang/Runnable;", "Ljava/lang/AutoCloseable;", "Ljava/lang/Appendable;", "Ljava/lang/Iterable;",
          "Ljava/io/DataInput;", "Ljava/io/DataOutput;", "Ljava/io/ObjectInput;", "Ljava/io/ObjectOutput;",
          "Ljava/io/ObjectStreamConstants;"}) {
        CHECK_FALSE(descriptors.contains(name));
        const auto type = boot_linker.ResolveDescriptor(name);
        const auto& linked = boot_linker.Class(type);
        CHECK(linked.is_boot_dex);
        IntrinsicClassDecl shape;
        shape.descriptor = name;
        shape.is_interface = linked.is_interface;
        for (const auto parent : linked.direct_interfaces)
            shape.interfaces.push_back(boot_linker.Class(parent).descriptor);
        for (const auto id : linked.own_virtual_methods) {
            const auto& method = boot_linker.Method(id);
            IntrinsicMethodDecl member;
            member.name = method.name;
            member.descriptor = method.descriptor;
            shape.methods.push_back(std::move(member));
        }
        catalog.push_back(std::move(shape));
    }

    const auto signatures = [&catalog](const std::string& descriptor) {
        std::set<std::pair<std::string, std::string>> result;
        const auto declaration = std::find_if(
            catalog.begin(), catalog.end(), [&](const auto& candidate) { return candidate.descriptor == descriptor; });
        REQUIRE(declaration != catalog.end());
        for (const auto& method : declaration->methods) {
            CHECK(result.emplace(method.name, method.descriptor).second);
        }
        return result;
    };

    CHECK(signatures("Ljava/lang/Object;") == std::set<std::pair<std::string, std::string>>{
                                                  {"<init>", "()V"},
                                                  {"clone", "()Ljava/lang/Object;"},
                                                  {"equals", "(Ljava/lang/Object;)Z"},
                                                  {"finalize", "()V"},
                                                  {"getClass", "()Ljava/lang/Class;"},
                                                  {"hashCode", "()I"},
                                                  {"notify", "()V"},
                                                  {"notifyAll", "()V"},
                                                  {"toString", "()Ljava/lang/String;"},
                                                  {"wait", "()V"},
                                                  {"wait", "(J)V"},
                                                  {"wait", "(JI)V"},
                                              });
    CHECK(signatures("Ljava/lang/Cloneable;").empty());
    CHECK(signatures("Ljava/lang/Appendable;") == std::set<std::pair<std::string, std::string>>{
                                                      {"append", "(C)Ljava/lang/Appendable;"},
                                                      {"append", "(Ljava/lang/CharSequence;)Ljava/lang/Appendable;"},
                                                      {"append", "(Ljava/lang/CharSequence;II)Ljava/lang/Appendable;"},
                                                  });
    CHECK(signatures("Ljava/lang/AutoCloseable;") == std::set<std::pair<std::string, std::string>>{{"close", "()V"}});
    CHECK(signatures("Ljava/lang/Iterable;") == std::set<std::pair<std::string, std::string>>{
                                                    {"iterator", "()Ljava/util/Iterator;"},
                                                });
    CHECK(signatures("Ljava/lang/Readable;") == std::set<std::pair<std::string, std::string>>{
                                                    {"read", "(Ljava/nio/CharBuffer;)I"},
                                                });
    CHECK(signatures("Ljava/lang/CharSequence;") == std::set<std::pair<std::string, std::string>>{
                                                        {"length", "()I"},
                                                        {"charAt", "(I)C"},
                                                        {"subSequence", "(II)Ljava/lang/CharSequence;"},
                                                        {"toString", "()Ljava/lang/String;"},
                                                    });
    CHECK(signatures("Ljava/lang/Comparable;") == std::set<std::pair<std::string, std::string>>{
                                                      {"compareTo", "(Ljava/lang/Object;)I"},
                                                  });
    CHECK(signatures("Ljava/lang/Runnable;") == std::set<std::pair<std::string, std::string>>{{"run", "()V"}});
    for (const auto* owner : {"Ljava/lang/AbstractStringBuilder;", "Ljava/lang/IntegralToString;",
                              "Ljava/lang/StringBuilder;", "Ljava/lang/StringBuffer;"}) {
        CHECK_FALSE(descriptors.contains(owner));
        const auto& type = boot_linker.Class(boot_linker.ResolveDescriptor(owner));
        CHECK(type.is_boot_dex);
        for (auto method : type.own_direct_methods)
            CHECK(boot_linker.Method(method).kind == MethodKind::interpreted);
        for (auto method : type.own_virtual_methods)
            CHECK(boot_linker.Method(method).kind == MethodKind::interpreted);
    }
    const auto abstract_builder = boot_linker.ResolveDescriptor("Ljava/lang/AbstractStringBuilder;");
    const auto string_builder = boot_linker.ResolveDescriptor("Ljava/lang/StringBuilder;");
    const auto& linked_builder = boot_linker.Class(string_builder);
    REQUIRE(linked_builder.super.has_value());
    CHECK(*linked_builder.super == abstract_builder);
    CHECK(linked_builder.direct_interfaces ==
          std::vector<DexClassId>{boot_linker.ResolveDescriptor("Ljava/lang/Appendable;"),
                                  boot_linker.ResolveDescriptor("Ljava/lang/CharSequence;"),
                                  boot_linker.ResolveDescriptor("Ljava/io/Serializable;")});
    const auto string_signatures = signatures("Ljava/lang/String;");
    // Boundaries and overloads matter; a class gaining a supported method
    // must not invalidate an unrelated frozen method count.
    CHECK(string_signatures.contains({"regionMatches", "(ILjava/lang/String;II)Z"}));
    CHECK(string_signatures.contains({"regionMatches", "(ZILjava/lang/String;II)Z"}));
    CHECK(string_signatures.contains({"intern", "()Ljava/lang/String;"}));
    CHECK(string_signatures.contains({"toLowerCase", "(Ljava/util/Locale;)Ljava/lang/String;"}));

    CHECK(signatures("Ljava/io/InputStreamReader;") ==
          std::set<std::pair<std::string, std::string>>{
              {"<init>", "(Ljava/io/InputStream;)V"},
              {"<init>", "(Ljava/io/InputStream;Ljava/nio/charset/Charset;)V"},
              {"<init>", "(Ljava/io/InputStream;Ljava/lang/String;)V"},
              {"read", "()I"},
              {"read", "([CII)I"},
              {"ready", "()Z"},
              {"close", "()V"},
              {"getEncoding", "()Ljava/lang/String;"},
          });
    // Migrated IO shapes come from the linked BootDex class, not the native overlay catalog.
    for (const auto* descriptor :
         {"Ljava/io/ObjectInputStream;", "Ljava/io/ObjectOutputStream;", "Ljava/io/DataInput;", "Ljava/io/DataOutput;",
          "Ljava/io/ObjectInput;", "Ljava/io/ObjectOutput;", "Ljava/io/ObjectStreamConstants;"}) {
        const auto type = boot_linker.ResolveDescriptor(descriptor);
        REQUIRE(boot_linker.Class(type).is_boot_dex);
        boot_linker.EnsureClassLinked(type);
        for (const auto id : boot_linker.Class(type).own_virtual_methods)
            CHECK(boot_linker.Method(id).kind != MethodKind::intrinsic);
    }
}

TEST_CASE("dormant classes with missing hierarchy link only when reached") {
    Vm vm;

    // An unrelated usable class proves Link() no longer rejects the entire
    // DEX because an optional packaged class names an absent framework base.
    CHECK(vm.linker.ResolveDescriptor("LCounter;").IsValid());

    try {
        static_cast<void>(vm.linker.ResolveDescriptor("LDormantOptional;"));
        FAIL("reached class with missing hierarchy did not fail");
    } catch (const JavaLinkageError& error) {
        const std::string message = error.what();
        CHECK(message.find("class hierarchy is not available: ") != std::string::npos);
        CHECK(message.find("Landroid/optional/MissingActivity;") != std::string::npos);
        CHECK(message.find("required by LDormantOptional;") != std::string::npos);
    }
}

TEST_CASE("survey records a missing hierarchy only when its class is reached") {
    Vm vm;
    vm.linker.EnableGapSurvey();
    CHECK(vm.linker.GapSurveyHits().empty());

    const auto optional = vm.linker.ResolveDescriptor("LDormantOptional;");
    CHECK(optional.IsValid());
    const auto hits = vm.linker.GapSurveyHits();
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].owner_descriptor == "Landroid/optional/MissingActivity;");
    CHECK(hits[0].member.empty());
}

TEST_CASE("APK Android Support classes remain application-owned") {
    Vm vm;

    constexpr auto support_descriptor = "Landroid/support/v4/content/FixtureSupport;";
    const auto support = vm.linker.ResolveDescriptor(support_descriptor);
    CHECK(vm.linker.Class(support).defining_loader == kApplicationLoader);
    CHECK_FALSE(vm.linker.Class(support).is_intrinsic);
    CHECK(vm.CallStatic(support_descriptor, "answer", "()I").value.AsInt() == 42);

    // A real BootDex definition shadows the APK copy, independent of package prefix.
    const auto activity = vm.linker.FindClass("Landroid/app/Activity;");
    REQUIRE(activity);
    CHECK(vm.linker.Class(*activity).defining_loader == kBootstrapLoader);
    CHECK(vm.linker.Class(*activity).is_boot_dex);
}

TEST_CASE("dexvm core ROM classes own Java methods and fields") {
    Vm vm;
    const auto catalog = CoreIntrinsicCatalog();
    for (const auto* owner : {"Ljava/lang/Number;",
                              "Ljava/lang/Boolean;",
                              "Ljava/lang/Byte;",
                              "Ljava/lang/Short;",
                              "Ljava/lang/Integer;",
                              "Ljava/lang/Long;",
                              "Ljava/lang/Character;",
                              "Ljava/lang/Thread$State;",
                              "Ljava/lang/Thread$UncaughtExceptionHandler;",
                              "Ljava/lang/reflect/Type;",
                              "Ljava/io/EOFException;",
                              "Ljava/io/UnsupportedEncodingException;",
                              "Ljava/net/MalformedURLException;",
                              "Ljava/nio/BufferOverflowException;",
                              "Ljava/nio/BufferUnderflowException;",
                              "Ljava/nio/InvalidMarkException;",
                              "Ljava/nio/ReadOnlyBufferException;",
                              "Ljava/lang/reflect/Method;",
                              "Ljava/lang/reflect/Field;",
                              "Ljava/lang/reflect/Constructor;",
                              "Ljava/lang/reflect/AccessibleObject;",
                              "Ljava/lang/reflect/Array;"}) {
        CAPTURE(owner);
        const auto type = vm.linker.ResolveDescriptor(owner);
        const auto& linked = vm.linker.Class(type);
        CHECK(linked.is_boot_dex);
        std::size_t natives = 0;
        const auto check_method = [&](VmMethodId id) {
            const auto& method = vm.linker.Method(id);
            if ((method.access_flags & kAccNative) != 0) {
                ++natives;
                CHECK((std::string_view(owner) == "Ljava/lang/Character;" ||
                       std::string_view(owner).starts_with("Ljava/lang/reflect/")));
            } else {
                CHECK(method.kind != MethodKind::intrinsic);
            }
        };
        for (auto id : linked.own_direct_methods)
            check_method(id);
        for (auto id : linked.own_virtual_methods)
            check_method(id);
        const std::string_view name(owner);
        const auto expected =
            name == "Ljava/lang/Character;"                                                                 ? 25U
            : name == "Ljava/lang/reflect/Method;"                                                          ? 8U
            : name == "Ljava/lang/reflect/Field;"                                                           ? 23U
            : name == "Ljava/lang/reflect/Array;"                                                           ? 2U
            : (name == "Ljava/lang/reflect/Constructor;" || name == "Ljava/lang/reflect/AccessibleObject;") ? 1U
                                                                                                            : 0U;
        CHECK(natives == expected);
        for (const auto& declaration : catalog) {
            if (declaration.descriptor != owner) continue;
            CHECK(declaration.fields.empty());
            for (const auto& method : declaration.methods)
                CHECK((method.access_flags & kAccNative) != 0);
        }
    }
    // ROM owns constructor signatures (InvalidMarkException also has a String ctor).
    // VmJavaThrow initializes Throwable directly rather than inventing a subclass ctor.
    for (const auto* owner : {"Ljava/nio/BufferOverflowException;", "Ljava/nio/BufferUnderflowException;",
                              "Ljava/nio/InvalidMarkException;", "Ljava/nio/ReadOnlyBufferException;"}) {
        const auto type = vm.linker.ResolveDescriptor(owner);
        const auto ctor = vm.linker.FindDirectMethod(type, "<init>", "(Ljava/lang/String;)V");
        CHECK((ctor && vm.linker.Method(*ctor).owner == type) ==
              (std::string_view(owner) == "Ljava/nio/InvalidMarkException;"));
        CHECK(vm.interpreter.MakeThrowable(owner, "VM boundary").IsValid());
    }
}

TEST_CASE("dexvm primitive wrapper parsing bits and Character boundaries") {
    Vm vm;
    VmThreadRuntime threads(vm.interpreter);
    const auto string = [&](std::string_view value) { return VmValue::Ref(vm.interpreter.NewStringUtf8(value)); };
    const auto call_on = [&](const char* owner, VmObjectRef receiver, const char* name, const char* descriptor,
                             std::vector<VmValue> arguments = {}) {
        const auto java_class = vm.linker.FindClass(owner);
        REQUIRE(java_class.has_value());
        std::optional<VmMethodId> method;
        for (const auto candidate : vm.linker.Class(*java_class).vtable) {
            if (!candidate.IsValid()) continue;
            const auto& linked = vm.linker.Method(candidate);
            if (linked.name == name && linked.descriptor == descriptor) {
                method = candidate;
                break;
            }
        }
        CAPTURE(std::string(owner));
        CAPTURE(std::string(name));
        CAPTURE(std::string(descriptor));
        REQUIRE(method.has_value());
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return vm.interpreter.Call(*method, arguments);
    };
    const auto as_string = [&](const VmCallOutcome& outcome) {
        REQUIRE_FALSE(outcome.exception.IsValid());
        return vm.interpreter.StringUtf8(outcome.value.ref);
    };
    const auto expect_long = [](const VmCallOutcome& outcome, std::int64_t expected) {
        REQUIRE_FALSE(outcome.exception.IsValid());
        REQUIRE(outcome.value.kind == VmValue::Kind::wide);
        CHECK(outcome.value.AsLong() == expected);
    };
    const auto static_bits = [&](const char* owner, const char* name, const char* descriptor) {
        const auto java_class = vm.linker.FindClass(owner);
        REQUIRE(java_class.has_value());
        const auto field = vm.linker.FindFieldRecursive(*java_class, name, descriptor);
        REQUIRE(field.has_value());
        const auto& linked = vm.linker.Field(*field);
        const auto& storage = vm.linker.Class(linked.owner).static_storage;
        std::uint64_t bits = storage[linked.slot];
        if (linked.is_wide) bits |= static_cast<std::uint64_t>(storage[linked.slot + 1]) << 32U;
        return bits;
    };
    for (const auto* owner : {"Ljava/lang/Integer;", "Ljava/lang/Long;"}) {
        const bool wide = std::string_view(owner) == "Ljava/lang/Long;";
        const auto signature = wide ? "(J)Ljava/lang/Long;" : "(I)Ljava/lang/Integer;";
        for (const int value : {-129, -128, 127, 128}) {
            const auto argument = wide ? VmValue::Long(value) : VmValue::Int(value);
            const auto first = vm.CallStatic(owner, "valueOf", signature, {argument});
            REQUIRE_FALSE(first.exception.IsValid());
            const auto root = vm.interpreter.ProtectReferences(std::array{first.value.ref});
            const auto second = vm.CallStatic(owner, "valueOf", signature, {argument});
            REQUIRE_FALSE(second.exception.IsValid());
            CHECK((first.value.ref == second.value.ref) == (value >= -128 && value <= 127));
        }
    }
    const auto parse_radix = vm.Static("Ljava/lang/Integer;", "parseInt", "(Ljava/lang/String;I)I");
    CHECK(vm.linker.Method(parse_radix).kind == MethodKind::interpreted);
    ExpectInt(vm.interpreter.Call(parse_radix, std::vector<VmValue>{string("7fffffff"), VmValue::Int(16)}),
              std::numeric_limits<std::int32_t>::max());
    ExpectException(vm,
                    vm.CallStatic("Ljava/lang/Integer;", "parseInt", "(Ljava/lang/String;)I", {string("2147483648")}),
                    "Ljava/lang/NumberFormatException;");
    const auto parsed_min =
        vm.CallStatic("Ljava/lang/Long;", "parseLong", "(Ljava/lang/String;)J", {string("-9223372036854775808")});
    REQUIRE_FALSE(parsed_min.exception.IsValid());
    CHECK(parsed_min.value.AsLong() == std::numeric_limits<std::int64_t>::min());
    ExpectException(vm, vm.CallStatic("Ljava/lang/Byte;", "parseByte", "(Ljava/lang/String;)B", {string("128")}),
                    "Ljava/lang/NumberFormatException;");
    ExpectInt(vm.CallStatic("Ljava/lang/Short;", "reverseBytes", "(S)S", {VmValue::Int(0x0100)}), 1);
    ExpectException(
        vm, vm.CallStatic("Ljava/lang/Integer;", "parseInt", "(Ljava/lang/String;)I", {VmValue::Ref(VmObjectRef{0})}),
        "Ljava/lang/NumberFormatException;");
    ExpectException(
        vm, vm.CallStatic("Ljava/lang/Float;", "parseFloat", "(Ljava/lang/String;)F", {VmValue::Ref(VmObjectRef{})}),
        "Ljava/lang/NullPointerException;");
    ExpectException(
        vm, vm.CallStatic("Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D", {VmValue::Ref(VmObjectRef{})}),
        "Ljava/lang/NullPointerException;");
    ExpectException(vm,
                    vm.CallStatic("Ljava/lang/Float;", "valueOf", "(Ljava/lang/String;)Ljava/lang/Float;",
                                  {VmValue::Ref(VmObjectRef{})}),
                    "Ljava/lang/NullPointerException;");
    ExpectException(vm,
                    vm.CallStatic("Ljava/lang/Double;", "valueOf", "(Ljava/lang/String;)Ljava/lang/Double;",
                                  {VmValue::Ref(VmObjectRef{})}),
                    "Ljava/lang/NullPointerException;");

    const std::string tiny_float_text = "0." + std::string(80, '0') + "1";
    const auto tiny_float =
        vm.CallStatic("Ljava/lang/Float;", "parseFloat", "(Ljava/lang/String;)F", {string(tiny_float_text)});
    REQUIRE_FALSE(tiny_float.exception.IsValid());
    CHECK(tiny_float.value.AsFloat() == 0.0F);
    CHECK_FALSE(std::signbit(tiny_float.value.AsFloat()));

    const std::string tiny_double_text = "-0." + std::string(400, '0') + "1";
    const auto tiny_double =
        vm.CallStatic("Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D", {string(tiny_double_text)});
    REQUIRE_FALSE(tiny_double.exception.IsValid());
    CHECK(tiny_double.value.AsDouble() == 0.0);
    CHECK(std::signbit(tiny_double.value.AsDouble()));

    {
        // ROM HexStringParser initializes a Pattern; exercise these existing
        // assertions with the real JNI/ICU fixture rather than a host-only VM.
        ogplay::test::container_traversal::ContainerVm hex_vm(InterpreterBackend::switch_dispatch);
        const VmExecutionLockScope lock(hex_vm.vm.ExecutionLock());
        const auto hex_string = [&](std::string_view value) { return VmValue::Ref(hex_vm.vm.NewStringUtf8(value)); };
        const auto parse_hex = [&](const char* owner, const char* name, const char* descriptor,
                                   std::vector<VmValue> arguments) {
            const auto method =
                hex_vm.linker.FindDirectMethod(hex_vm.linker.ResolveDescriptor(owner), name, descriptor);
            REQUIRE(method.has_value());
            return hex_vm.vm.Call(*method, arguments);
        };
        ExpectException(hex_vm,
                        parse_hex("Ljava/lang/Float;", "parseFloat", "(Ljava/lang/String;)F", {hex_string("0x1.0")}),
                        "Ljava/lang/NumberFormatException;");
        ExpectException(hex_vm,
                        parse_hex("Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D", {hex_string("-0X1.0")}),
                        "Ljava/lang/NumberFormatException;");

        const auto hex_float =
            parse_hex("Ljava/lang/Float;", "parseFloat", "(Ljava/lang/String;)F", {hex_string("0x1.0p0")});
        REQUIRE_FALSE(hex_float.exception.IsValid());
        CHECK(hex_float.value.AsFloat() == 1.0F);
        const auto hex_double =
            parse_hex("Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D", {hex_string("-0x1.8p1")});
        REQUIRE_FALSE(hex_double.exception.IsValid());
        CHECK(hex_double.value.AsDouble() == -3.0);
    }

    const std::string huge_negative_exponent = std::string(400, '9') + "e-1";
    const auto still_overflow =
        vm.CallStatic("Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D", {string(huge_negative_exponent)});
    REQUIRE_FALSE(still_overflow.exception.IsValid());
    CHECK(std::isinf(still_overflow.value.AsDouble()));
    CHECK(still_overflow.value.AsDouble() > 0.0);

    const auto decoded = vm.CallStatic("Ljava/lang/Integer;", "decode", "(Ljava/lang/String;)Ljava/lang/Integer;",
                                       {string("-0x80000000")});
    REQUIRE_FALSE(decoded.exception.IsValid());
    ExpectInt(call_on("Ljava/lang/Integer;", decoded.value.ref, "intValue", "()I"),
              std::numeric_limits<std::int32_t>::min());
    CHECK(as_string(vm.CallStatic("Ljava/lang/Integer;", "toString", "(II)Ljava/lang/String;",
                                  {VmValue::Int(35), VmValue::Int(36)})) == "z");
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "rotateLeft", "(II)I", {VmValue::Int(1), VmValue::Int(-1)}),
              std::numeric_limits<std::int32_t>::min());
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "numberOfLeadingZeros", "(I)I", {VmValue::Int(0)}), 32);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "highestOneBit", "(I)I", {VmValue::Int(0)}), 0);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "highestOneBit", "(I)I", {VmValue::Int(-1)}),
              std::numeric_limits<std::int32_t>::min());
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "lowestOneBit", "(I)I",
                            {VmValue::Int(std::numeric_limits<std::int32_t>::max())}),
              1);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "numberOfLeadingZeros", "(I)I", {VmValue::Int(1)}), 31);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "numberOfLeadingZeros", "(I)I", {VmValue::Int(-1)}), 0);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "numberOfTrailingZeros", "(I)I",
                            {VmValue::Int(std::numeric_limits<std::int32_t>::min())}),
              31);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "rotateLeft", "(II)I", {VmValue::Int(1), VmValue::Int(33)}), 2);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "reverse", "(I)I", {VmValue::Int(1)}),
              std::numeric_limits<std::int32_t>::min());
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "reverseBytes", "(I)I", {VmValue::Int(0x01020304)}), 0x04030201);
    expect_long(vm.CallStatic("Ljava/lang/Long;", "highestOneBit", "(J)J", {VmValue::Long(0)}), 0);
    expect_long(vm.CallStatic("Ljava/lang/Long;", "highestOneBit", "(J)J", {VmValue::Long(-1)}),
                std::numeric_limits<std::int64_t>::min());
    ExpectInt(vm.CallStatic("Ljava/lang/Long;", "numberOfLeadingZeros", "(J)I", {VmValue::Long(1)}), 63);
    ExpectInt(vm.CallStatic("Ljava/lang/Long;", "numberOfTrailingZeros", "(J)I",
                            {VmValue::Long(std::numeric_limits<std::int64_t>::min())}),
              63);
    expect_long(vm.CallStatic("Ljava/lang/Long;", "rotateLeft", "(JI)J", {VmValue::Long(1), VmValue::Int(65)}), 2);
    expect_long(vm.CallStatic("Ljava/lang/Long;", "rotateRight", "(JI)J", {VmValue::Long(1), VmValue::Int(-1)}), 2);
    expect_long(vm.CallStatic("Ljava/lang/Long;", "reverse", "(J)J", {VmValue::Long(1)}),
                std::numeric_limits<std::int64_t>::min());
    expect_long(vm.CallStatic("Ljava/lang/Long;", "reverseBytes", "(J)J", {VmValue::Long(0x0102030405060708LL)}),
                0x0807060504030201LL);
    ExpectInt(vm.CallStatic("Ljava/lang/Float;", "compare", "(FF)I", {VmValue::Float(-0.0F), VmValue::Float(0.0F)}),
              -1);
    ExpectInt(vm.CallStatic("Ljava/lang/Float;", "floatToIntBits", "(F)I",
                            {VmValue::Float(std::numeric_limits<float>::quiet_NaN())}),
              0x7fc00000);
    const auto raw_nan = std::bit_cast<float>(0x7fc01234U);
    ExpectInt(vm.CallStatic("Ljava/lang/Float;", "floatToRawIntBits", "(F)I", {VmValue::Float(raw_nan)}), 0x7fc01234);
    CHECK(as_string(vm.CallStatic("Ljava/lang/Float;", "toString", "(F)Ljava/lang/String;", {VmValue::Float(-0.0F)})) ==
          "-0.0");
    for (const auto& [value, text] :
         std::vector<std::pair<float, std::string>>{{0.0F, "0.0"},
                                                    {1.0F, "1.0"},
                                                    {-1.0F, "-1.0"},
                                                    {0.5F, "0.5"},
                                                    {1.5F, "1.5"},
                                                    {1.0e8F, "1.0E8"},
                                                    {1.0e-4F, "1.0E-4"},
                                                    {std::numeric_limits<float>::denorm_min(), "1.4E-45"},
                                                    {std::bit_cast<float>(0x00000003U), "4.2E-45"}}) {
        CAPTURE(text);
        CHECK(as_string(vm.CallStatic("Ljava/lang/Float;", "toString", "(F)Ljava/lang/String;",
                                      {VmValue::Float(value)})) == text);
    }
    CHECK(as_string(vm.CallStatic("Ljava/lang/Float;", "toString", "(F)Ljava/lang/String;",
                                  {VmValue::Float(std::numeric_limits<float>::quiet_NaN())})) == "NaN");
    CHECK(as_string(vm.CallStatic("Ljava/lang/Float;", "toString", "(F)Ljava/lang/String;",
                                  {VmValue::Float(-std::numeric_limits<float>::infinity())})) == "-Infinity");
    CHECK(as_string(vm.CallStatic("Ljava/lang/Float;", "toHexString", "(F)Ljava/lang/String;",
                                  {VmValue::Float(std::numeric_limits<float>::denorm_min())})) == "0x0.000002p-126");
    CHECK(as_string(vm.CallStatic("Ljava/lang/Float;", "toHexString", "(F)Ljava/lang/String;",
                                  {VmValue::Float(std::bit_cast<float>(0x007fffffU))})) == "0x0.fffffep-126");
    CHECK(as_string(vm.CallStatic("Ljava/lang/Double;", "toString", "(D)Ljava/lang/String;",
                                  {VmValue::Double(std::numeric_limits<double>::infinity())})) == "Infinity");
    CHECK(as_string(vm.CallStatic("Ljava/lang/Double;", "toString", "(D)Ljava/lang/String;",
                                  {VmValue::Double(std::numeric_limits<double>::denorm_min())})) == "4.9E-324");
    CHECK(as_string(vm.CallStatic("Ljava/lang/Double;", "toHexString", "(D)Ljava/lang/String;",
                                  {VmValue::Double(std::numeric_limits<double>::denorm_min())})) ==
          "0x0.0000000000001p-1022");
    CHECK(as_string(vm.CallStatic("Ljava/lang/Double;", "toHexString", "(D)Ljava/lang/String;",
                                  {VmValue::Double(std::bit_cast<double>(0x000fffffffffffffULL))})) ==
          "0x0.fffffffffffffp-1022");
    const auto parsed_half =
        vm.CallStatic("Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D", {string("  +0.5  ")});
    REQUIRE_FALSE(parsed_half.exception.IsValid());
    CHECK(parsed_half.value.AsDouble() == 0.5);
    ExpectInt(vm.CallStatic("Ljava/lang/Double;", "compare", "(DD)I",
                            {VmValue::Double(std::numeric_limits<double>::quiet_NaN()),
                             VmValue::Double(std::numeric_limits<double>::infinity())}),
              1);
    const auto raw_double_nan = std::bit_cast<double>(0x7ff8000000001234ULL);
    expect_long(vm.CallStatic("Ljava/lang/Double;", "doubleToRawLongBits", "(D)J", {VmValue::Double(raw_double_nan)}),
                static_cast<std::int64_t>(0x7ff8000000001234ULL));
    const auto infinity = vm.CallStatic("Ljava/lang/Float;", "valueOf", "(F)Ljava/lang/Float;",
                                        {VmValue::Float(std::numeric_limits<float>::infinity())});
    REQUIRE_FALSE(infinity.exception.IsValid());
    ExpectInt(call_on("Ljava/lang/Float;", infinity.value.ref, "intValue", "()I"),
              std::numeric_limits<std::int32_t>::max());
    const auto true_one = vm.CallStatic("Ljava/lang/Boolean;", "valueOf", "(Z)Ljava/lang/Boolean;", {VmValue::Int(1)});
    const auto true_two =
        vm.CallStatic("Ljava/lang/Boolean;", "valueOf", "(Ljava/lang/String;)Ljava/lang/Boolean;", {string("TRUE")});
    REQUIRE_FALSE(true_one.exception.IsValid());
    REQUIRE_FALSE(true_two.exception.IsValid());
    CHECK(true_one.value.ref == true_two.value.ref);
    const auto false_one = vm.CallStatic("Ljava/lang/Boolean;", "valueOf", "(Z)Ljava/lang/Boolean;", {VmValue::Int(0)});
    REQUIRE_FALSE(false_one.exception.IsValid());
    CHECK(false_one.value.ref != true_one.value.ref);
    ExpectInt(call_on("Ljava/lang/Boolean;", true_one.value.ref, "hashCode", "()I"), 1231);
    ExpectInt(call_on("Ljava/lang/Boolean;", false_one.value.ref, "compareTo", "(Ljava/lang/Boolean;)I",
                      {VmValue::Ref(true_one.value.ref)}),
              -1);
    ExpectInt(
        vm.CallStatic("Ljava/lang/Character;", "toCodePoint", "(CC)I", {VmValue::Int(0xd800), VmValue::Int(0xdc00)}),
        0x10000);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isValidCodePoint", "(I)Z", {VmValue::Int(0x110000)}), 0);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isSupplementaryCodePoint", "(I)Z", {VmValue::Int(0x10ffff)}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "digit", "(II)I", {VmValue::Int('Z'), VmValue::Int(36)}), 35);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "digit", "(II)I", {VmValue::Int('1'), VmValue::Int(2)}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "digit", "(II)I", {VmValue::Int('a'), VmValue::Int(10)}), -1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "forDigit", "(II)C", {VmValue::Int(15), VmValue::Int(16)}), 'f');
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isLetterOrDigit", "(I)Z", {VmValue::Int('Q')}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "toLowerCase", "(I)I", {VmValue::Int('Q')}), 'q');
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isWhitespace", "(I)Z", {VmValue::Int('\n')}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isHighSurrogate", "(C)Z", {VmValue::Int(0xdbff)}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isLowSurrogate", "(C)Z", {VmValue::Int(0xdfff)}), 1);
    ExpectInt(
        vm.CallStatic("Ljava/lang/Character;", "toCodePoint", "(CC)I", {VmValue::Int(0xdbff), VmValue::Int(0xdfff)}),
        0x10ffff);
    static_cast<void>(vm.interpreter.SetSystemProperty("feature.enabled", "TrUe"));
    ExpectInt(vm.CallStatic("Ljava/lang/Boolean;", "getBoolean", "(Ljava/lang/String;)Z", {string("feature.enabled")}),
              1);
    CHECK(static_bits("Ljava/lang/Byte;", "MAX_VALUE", "B") == 127U);
    CHECK(static_bits("Ljava/lang/Short;", "MIN_VALUE", "S") == 0xffff8000U);
    CHECK(static_bits("Ljava/lang/Integer;", "MIN_VALUE", "I") == 0x80000000U);
    CHECK(static_bits("Ljava/lang/Long;", "MAX_VALUE", "J") == 0x7fffffffffffffffULL);
    CHECK(static_bits("Ljava/lang/Float;", "NaN", "F") == 0x7fc00000U);
    CHECK(static_bits("Ljava/lang/Float;", "MIN_NORMAL", "F") == 0x00800000U);
    CHECK(static_bits("Ljava/lang/Double;", "MIN_NORMAL", "D") == 0x0010000000000000ULL);
    CHECK(static_bits("Ljava/lang/Character;", "MAX_CODE_POINT", "I") == 0x10ffffU);
    CHECK(VmObjectRef{static_cast<std::uint32_t>(static_bits("Ljava/lang/Boolean;", "TRUE", "Ljava/lang/Boolean;"))} ==
          true_one.value.ref);
    for (const auto& [owner, primitive] :
         std::vector<std::pair<std::string, std::string>>{{"Ljava/lang/Byte;", "B"},
                                                          {"Ljava/lang/Short;", "S"},
                                                          {"Ljava/lang/Integer;", "I"},
                                                          {"Ljava/lang/Long;", "J"},
                                                          {"Ljava/lang/Float;", "F"},
                                                          {"Ljava/lang/Double;", "D"},
                                                          {"Ljava/lang/Boolean;", "Z"},
                                                          {"Ljava/lang/Character;", "C"}}) {
        CAPTURE(owner);
        CHECK(VmObjectRef{static_cast<std::uint32_t>(static_bits(owner.c_str(), "TYPE", "Ljava/lang/Class;"))} ==
              vm.model.ClassObject(vm.linker.ResolveDescriptor(primitive)));
    }
}

TEST_CASE("dexvm java.lang primitive wrappers are in lang family TU") {
    const std::vector<std::string> classes = {
        "Number", "Byte", "Short", "Integer", "Long", "Float", "Double", "Boolean", "Character",
    };
    const auto directory = std::filesystem::path(OGPLAY_SOURCE_DIR) / "src" / "runtime" / "dexvm" / "intrinsics";
    CHECK(std::filesystem::is_regular_file(directory / "java_lang.cpp"));
    for (const auto& name : classes) {
        CAPTURE(name);
        CHECK_FALSE(std::filesystem::exists(directory / ("java_lang_" + name + ".cpp")));
    }
    std::ifstream header(directory / "catalog.h", std::ios::binary);
    REQUIRE(header.good());
    const std::string source(std::istreambuf_iterator<char>(header), {});
    CHECK(source.find("AppendJavaLangPrimitiveWrappers") != std::string::npos);
    for (const auto& name : classes) {
        CAPTURE(name);
        CHECK(source.find("Declare_java_lang_" + name) == std::string::npos);
    }
}

TEST_CASE("dexvm API 19 java.lang interface inventory is complete") {
    const std::vector<std::string> inventory = {
        "Appendable", "AutoCloseable", "CharSequence", "Cloneable", "Comparable", "Iterable", "Readable", "Runnable",
    };
    REQUIRE(inventory.size() == 8U);

    const auto catalog = CoreIntrinsicCatalog();
    std::map<std::string, std::size_t> descriptor_counts;
    for (const auto& declaration : catalog) {
        ++descriptor_counts[declaration.descriptor];
    }
    DexClassLinker linker;
    linker.RegisterIntrinsics(catalog);
    ogplay::test::RegisterBootDex(linker);
    linker.Link();
    for (const auto& name : inventory) {
        const auto descriptor = "Ljava/lang/" + name + ";";
        CAPTURE(descriptor);
        CHECK(descriptor_counts[descriptor] == 0U);
        const auto type = linker.ResolveDescriptor(descriptor);
        CHECK(linker.Class(type).is_boot_dex);
        CHECK(linker.Class(type).is_interface);
    }
}

TEST_CASE("dexvm java.lang interfaces are in lang family TU") {
    const std::vector<std::string> classes = {
        "Appendable", "AutoCloseable", "CharSequence", "Cloneable", "Comparable", "Iterable", "Readable", "Runnable",
    };
    const auto directory = std::filesystem::path(OGPLAY_SOURCE_DIR) / "src" / "runtime" / "dexvm" / "intrinsics";
    CHECK(std::filesystem::is_regular_file(directory / "java_lang.cpp"));
    for (const auto& name : classes) {
        CAPTURE(name);
        CHECK_FALSE(std::filesystem::exists(directory / ("java_lang_" + name + ".cpp")));
    }
    std::ifstream header(directory / "catalog.h", std::ios::binary);
    REQUIRE(header.good());
    const std::string source(std::istreambuf_iterator<char>(header), {});
    CHECK(source.find("AppendJavaLangInterfaces") != std::string::npos);
    for (const auto& name : classes) {
        CAPTURE(name);
        CHECK(source.find("Declare_java_lang_" + name) == std::string::npos);
    }
}

TEST_CASE("dexvm System properties are deterministic and mutable") {
    Vm vm;
    const auto get = vm.Static("Ljava/lang/System;", "getProperty", "(Ljava/lang/String;)Ljava/lang/String;");
    const auto set =
        vm.Static("Ljava/lang/System;", "setProperty", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    const auto line_separator = vm.Static("Ljava/lang/System;", "lineSeparator", "()Ljava/lang/String;");
    const auto string = [&vm](const std::string& value) { return VmValue::Ref(vm.interpreter.NewStringUtf8(value)); };
    const auto get_property = [&vm, get, &string](const std::string& key) {
        return vm.interpreter.Call(get, std::vector<VmValue>{string(key)});
    };

    for (const auto& [key, expected] : std::vector<std::pair<std::string, std::string>>{{"file.separator", "/"},
                                                                                        {"java.home", "/system"},
                                                                                        {"java.io.tmpdir", "/tmp"},
                                                                                        {"line.separator", "\n"},
                                                                                        {"path.separator", ":"},
                                                                                        {"user.dir", "/"}}) {
        const auto outcome = get_property(key);
        REQUIRE_FALSE(outcome.exception.IsValid());
        REQUIRE(outcome.value.ref.IsValid());
        CHECK(vm.interpreter.StringUtf8(outcome.value.ref) == expected);
    }

    const auto missing = get_property("ogplay.missing");
    REQUIRE_FALSE(missing.exception.IsValid());
    CHECK_FALSE(missing.value.ref.IsValid());

    const auto frozen_separator = vm.interpreter.Call(line_separator, {});
    REQUIRE_FALSE(frozen_separator.exception.IsValid());
    REQUIRE(frozen_separator.value.ref.IsValid());
    CHECK(vm.interpreter.StringUtf8(frozen_separator.value.ref) == "\n");
    static_cast<void>(vm.interpreter.Call(set, std::vector<VmValue>{string("line.separator"), string("changed")}));
    CHECK(vm.interpreter.StringUtf8(get_property("line.separator").value.ref) == "changed");
    const auto repeated_separator = vm.interpreter.Call(line_separator, {});
    REQUIRE_FALSE(repeated_separator.exception.IsValid());
    CHECK(repeated_separator.value.ref == frozen_separator.value.ref);
    CHECK(vm.interpreter.StringUtf8(repeated_separator.value.ref) == "\n");

    const auto first = vm.interpreter.Call(set, std::vector<VmValue>{string("ogplay.test"), string("first")});
    REQUIRE_FALSE(first.exception.IsValid());
    CHECK_FALSE(first.value.ref.IsValid());
    const auto first_read = get_property("ogplay.test");
    REQUIRE_FALSE(first_read.exception.IsValid());
    REQUIRE(first_read.value.ref.IsValid());
    CHECK(vm.interpreter.StringUtf8(first_read.value.ref) == "first");

    const auto second = vm.interpreter.Call(set, std::vector<VmValue>{string("ogplay.test"), string("second")});
    REQUIRE_FALSE(second.exception.IsValid());
    REQUIRE(second.value.ref.IsValid());
    CHECK(vm.interpreter.StringUtf8(second.value.ref) == "first");
    const auto second_read = get_property("ogplay.test");
    REQUIRE_FALSE(second_read.exception.IsValid());
    REQUIRE(second_read.value.ref.IsValid());
    CHECK(vm.interpreter.StringUtf8(second_read.value.ref) == "second");

    const auto clear = vm.Static("Ljava/lang/System;", "clearProperty", "(Ljava/lang/String;)Ljava/lang/String;");
    const auto removed = vm.interpreter.Call(clear, std::array{string("ogplay.test")});
    REQUIRE_FALSE(removed.exception.IsValid());
    REQUIRE(removed.value.ref.IsValid());
    CHECK(vm.interpreter.StringUtf8(removed.value.ref) == "second");
    CHECK_FALSE(get_property("ogplay.test").value.ref.IsValid());
    CHECK_FALSE(vm.interpreter.Call(clear, std::array{string("ogplay.test")}).value.ref.IsValid());

    ExpectException(vm, vm.interpreter.Call(get, std::vector<VmValue>{VmValue::Ref(VmObjectRef{})}),
                    "Ljava/lang/NullPointerException;");
    ExpectException(vm, vm.interpreter.Call(get, std::vector<VmValue>{string("")}),
                    "Ljava/lang/IllegalArgumentException;");
    ExpectException(vm,
                    vm.interpreter.Call(set, std::vector<VmValue>{string("ogplay.null"), VmValue::Ref(VmObjectRef{})}),
                    "Ljava/lang/NullPointerException;");
}

TEST_CASE("DVM-265 DEX Appendable interface dispatch") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::android_value::AndroidValueVm f(backend, {}, false, "rom_java_replacement.dex");
        const auto builder = f.New("Ljava/lang/StringBuilder;");
        const auto roots = f.vm.ProtectReferences(std::array{builder});
        CHECK(f.Static("Ltest/RomJavaReplacement;", "append", "(Ljava/lang/Appendable;)Ljava/lang/Appendable;",
                       {VmValue::Ref(builder)})
                  .ref == builder);
        CHECK(f.vm.StringUtf8(f.On(builder, "toString", "()Ljava/lang/String;").ref) == "A");
        CHECK(f.linker.Class(f.linker.ResolveDescriptor("Ljava/lang/Appendable;")).is_boot_dex);
    }
}

TEST_CASE("DVM-268 DEX AnnotatedElement ROM interface dispatch") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::android_value::AndroidValueVm f(backend, {}, false, "rom_java_replacement.dex");
        const auto result = f.Static("Ltest/RomJavaReplacement;", "annotated", "()I");
        CHECK(result.AsInt() == 0);
        CHECK(f.linker.Class(f.linker.ResolveDescriptor("Ljava/lang/reflect/AnnotatedElement;")).is_boot_dex);
    }
}

TEST_CASE("DVM-268 DEX Member ROM interface dispatch") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::android_value::AndroidValueVm f(backend, {}, false, "rom_java_replacement.dex");
        const auto result = f.Static("Ltest/RomJavaReplacement;", "member", "()Ljava/lang/Class;");
        CHECK(f.vm.StringUtf8(f.On(result.ref, "getName", "()Ljava/lang/String;").ref) == "java.lang.Object");
        CHECK(f.linker.Class(f.linker.ResolveDescriptor("Ljava/lang/reflect/Member;")).is_boot_dex);
    }
}

TEST_CASE("DVM-268 DEX GenericDeclaration ROM interface dispatch") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::android_value::AndroidValueVm f(backend, {}, false, "rom_java_replacement.dex");
        const auto result = f.Static("Ltest/RomJavaReplacement;", "generic", "()I");
        CHECK(result.AsInt() == 1);
        CHECK(f.linker.Class(f.linker.ResolveDescriptor("Ljava/lang/reflect/GenericDeclaration;")).is_boot_dex);
    }
}

TEST_CASE("DVM-268 DEX AutoCloseable ROM interface dispatch") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::android_value::AndroidValueVm f(backend, {}, false, "rom_java_replacement.dex");
        const auto result = f.Static("Ltest/RomJavaReplacement;", "close", "()I");
        CHECK(result.AsInt() == 0);
        CHECK(f.linker.Class(f.linker.ResolveDescriptor("Ljava/lang/AutoCloseable;")).is_boot_dex);
    }
}
