// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_lang_interfaces.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

void AppendJavaLangInterfaces(std::vector<IntrinsicClassDecl>& catalog) {
    {
        auto b = IntrinsicClassBuilder::Class("Ldalvik/system/VMRuntime;");
        b.MergesBootClass();
        for (const auto& [name, signature] :
             std::array{std::pair{"bootClassPath", "()Ljava/lang/String;"},
                        std::pair{"classPath", "()Ljava/lang/String;"}, std::pair{"preloadDexCaches", "()V"},
                        std::pair{"properties", "()[Ljava/lang/String;"}, std::pair{"setTargetSdkVersion", "(I)V"},
                        std::pair{"vmLibrary", "()Ljava/lang/String;"}, std::pair{"vmVersion", "()Ljava/lang/String;"}})
            b.UnimplementedVirtual(name, signature, kAccPublic | kAccNative);
        b.VirtualMethod(
            "newNonMovableArray", "(Ljava/lang/Class;I)Ljava/lang/Object;",
            [](IntrinsicContext& c) {
                const auto element = c.arguments[0].ref;
                const auto length = c.arguments[1].AsInt();
                if (!element.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "elementClass == null"};
                if (length < 0) throw VmJavaThrow{"Ljava/lang/NegativeArraySizeException;", "negative array length"};
                const auto element_class = c.vm.Model().ClassOfClassObject(element);
                const auto descriptor = c.vm.Linker().Class(element_class).descriptor;
                return VmValue::Ref(BuildReflectArray(c.vm, descriptor, std::array{length}));
            },
            kAccPublic | kAccNative);
        b.VirtualMethod(
            "addressOf", "(Ljava/lang/Object;)J",
            [](IntrinsicContext& c) {
                const auto array = c.arguments[0].ref;
                if (!array.IsValid() || (c.vm.Model().Kind(array) != VmObjectKind::primitive_array &&
                                         c.vm.Model().Kind(array) != VmObjectKind::object_array))
                    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "object is not an array"};
                if (c.vm.Model().Kind(array) == VmObjectKind::object_array) {
                    if (auto* ledger = c.vm.Ledger())
                        ledger->RecordUnimplemented("dexvm.runtime.address_of_object_array", 0);
                    throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "object arrays have no guest payload"};
                }
                return VmValue::Long(static_cast<std::int64_t>(c.vm.Model().PrimitiveArrayAddress(array).Value()));
            },
            kAccPublic | kAccNative);
        b.VirtualMethod(
            "getTargetHeapUtilization", "()F",
            [](IntrinsicContext& c) { return VmValue::Float(c.vm.Model().TargetHeapUtilization()); },
            kAccPublic | kAccNative);
        b.DirectMethod(
            "nativeSetTargetHeapUtilization", "(F)V",
            [](IntrinsicContext& c) {
                c.vm.Model().SetTargetHeapUtilization(c.arguments[0].AsFloat());
                return VmValue::Void();
            },
            kAccPrivate | kAccNative);
        b.VirtualMethod(
            "clearGrowthLimit", "()V",
            [](IntrinsicContext& c) {
                c.vm.Model().ClearGrowthLimit();
                return VmValue::Void();
            },
            kAccPublic | kAccNative);
        b.VirtualMethod(
            "isDebuggerActive", "()Z", [](IntrinsicContext&) { return VmValue::Int(0); }, kAccPublic | kAccNative);
        for (const auto* name : {"startJitCompilation", "disableJitCompilation"})
            b.VirtualMethod(name, "()V", [](IntrinsicContext&) { return VmValue::Void(); }, kAccPublic | kAccNative);
        b.VirtualMethod(
            "registerNativeAllocation", "(I)V",
            [](IntrinsicContext& c) {
                const auto bytes = c.arguments[0].AsInt();
                if (bytes < 0) throw VmJavaThrow{"Ljava/lang/RuntimeException;", "allocation size negative"};
                if (c.vm.Model().RegisterNativeAllocation(static_cast<std::uint64_t>(bytes))) {
                    static_cast<void>(c.vm.CollectGarbage("native-allocation"));
                    c.vm.RunFinalization();
                    c.vm.Model().UpdateNativeWatermark();
                }
                return VmValue::Void();
            },
            kAccPublic | kAccNative);
        b.VirtualMethod(
            "registerNativeFree", "(I)V",
            [](IntrinsicContext& c) {
                const auto bytes = c.arguments[0].AsInt();
                if (bytes < 0) throw VmJavaThrow{"Ljava/lang/RuntimeException;", "allocation size negative"};
                c.vm.Model().RegisterNativeFree(static_cast<std::uint64_t>(bytes));
                return VmValue::Void();
            },
            kAccPublic | kAccNative);
        catalog.push_back(std::move(b).Build());
    }
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_Math.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_Runtime(const CoreIntrinsicServices& services) {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Runtime;", "Ljava/lang/Object;");
    builder.MergesBootClass();
    // Runtime state and shutdown-hook protocol belong to pinned API 19 DEX.
    builder.VirtualMethod("availableProcessors", "()I", [cores = services.cpu_cores](IntrinsicContext&) {
        return VmValue::Int(static_cast<std::int32_t>(cores));
    });
    builder.StaticMethod(
        "nativeExit", "(I)V",
        [](IntrinsicContext& context) -> VmValue { context.vm.Exit(context.arguments[0].AsInt()); },
        kAccPrivate | kAccNative);
    // These unrelated native boundaries remain explicitly unsupported.
    builder.UnimplementedStatic("nativeLoad",
                                "(Ljava/lang/String;Ljava/lang/ClassLoader;Ljava/lang/String;)Ljava/lang/String;",
                                kAccPrivate | kAccNative);
    builder.VirtualMethod(
        "maxMemory", "()J",
        [](IntrinsicContext& c) {
            return VmValue::Long(static_cast<std::int64_t>(c.vm.Model().HeapGrowthLimitBytes()));
        },
        kAccPublic | kAccNative);
    builder.VirtualMethod(
        "totalMemory", "()J",
        [](IntrinsicContext& c) {
            return VmValue::Long(
                static_cast<std::int64_t>(std::max(c.vm.Model().HeapTargetBytes(), c.vm.Model().AllocatedBytes())));
        },
        kAccPublic | kAccNative);
    builder.VirtualMethod(
        "freeMemory", "()J",
        [](IntrinsicContext& c) {
            return VmValue::Long(
                static_cast<std::int64_t>(std::max(c.vm.Model().HeapTargetBytes(), c.vm.Model().AllocatedBytes()) -
                                          c.vm.Model().AllocatedBytes()));
        },
        kAccPublic | kAccNative);
    builder.VirtualMethod(
        "gc", "()V",
        [](IntrinsicContext& c) {
            static_cast<void>(c.vm.CollectGarbage("Runtime.gc"));
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    builder.VirtualMethod("runFinalization", "()V", [](IntrinsicContext& c) {
        c.vm.RunFinalization();
        return VmValue::Void();
    });
    builder.StaticMethod("runFinalizersOnExit", "(Z)V", [](IntrinsicContext& context) {
        if (context.arguments[0].AsInt() != 0) {
            if (auto* ledger = context.vm.Ledger()) ledger->RecordUnimplemented("dexvm.runtime.finalizers_on_exit", 0);
            throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "Java finalization on exit is not implemented"};
        }
        return VmValue::Void();
    });
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_libcore_io_IoUtils() {
    auto builder = IntrinsicClassBuilder::Class("Llibcore/io/IoUtils;", "Ljava/lang/Object;");
    builder.MergesBootClass();
    builder.StaticMethod("closeQuietly", "(Ljava/lang/AutoCloseable;)V", [](IntrinsicContext& context) {
        if (auto* ledger = context.vm.Ledger()) {
            ledger->RecordUnimplemented("dexvm.libcore.io_utils.close_quietly", 0);
        }
        return VmValue::Void();
    });
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_java_lang_Math() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Math;");
    builder.MergesBootClass();
    // API 19 java_lang_Math.cpp delegates these native primitives to libm.
    // Rounding, min/max, bit operations and random state remain original DEX.
    const auto unary = [&](const char* name, double (*operation)(double)) {
        builder.StaticMethod(
            name, "(D)D",
            [operation](IntrinsicContext& call) { return VmValue::Double(operation(call.arguments[0].AsDouble())); },
            kAccPublic | kAccNative);
    };
    const auto binary = [&](const char* name, double (*operation)(double, double), std::uint32_t access = kAccPublic) {
        builder.StaticMethod(
            name, "(DD)D",
            [operation](IntrinsicContext& call) {
                return VmValue::Double(operation(call.arguments[0].AsDouble(), call.arguments[1].AsDouble()));
            },
            access | kAccNative);
    };
    unary("acos", std::acos);
    unary("asin", std::asin);
    unary("atan", std::atan);
    binary("atan2", std::atan2);
    unary("cbrt", std::cbrt);
    unary("ceil", std::ceil);
    unary("cos", std::cos);
    unary("cosh", std::cosh);
    unary("exp", std::exp);
    unary("expm1", std::expm1);
    unary("floor", std::floor);
    binary("hypot", std::hypot);
    binary("IEEEremainder", std::remainder);
    unary("log", std::log);
    unary("log10", std::log10);
    unary("log1p", std::log1p);
    binary("pow", std::pow);
    unary("rint", std::rint);
    unary("sin", std::sin);
    unary("sinh", std::sinh);
    unary("sqrt", std::sqrt);
    unary("tan", std::tan);
    unary("tanh", std::tanh);
    binary("nextafter", std::nextafter, kAccPrivate);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_Object.cpp ----
#include "catalog.h"
#include "shared.h"

#include <charconv>
#include <limits>
#include <vector>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

namespace {

[[nodiscard]] std::string HashCodeHex(const std::int32_t hash) {
    char buffer[8];
    const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<std::uint32_t>(hash), 16);
    if (error != std::errc{}) {
        throw DexVmError(DexVmErrorReason::internal_invariant, "hashCode formatting failed");
    }
    return std::string(buffer, end);
}

}  // namespace

IntrinsicClassDecl Declare_java_lang_Object() {
    auto builder = IntrinsicClassBuilder::RootClass("Ljava/lang/Object;");

    builder.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });

    // API19 Object.finalize has an empty body; this is the superclass
    // call target, not an automatic finalizer scheduler.
    builder.VirtualMethod("finalize", "()V", [](IntrinsicContext&) { return VmValue::Void(); }, kAccProtected);

    builder.VirtualMethod("equals", "(Ljava/lang/Object;)Z", [](IntrinsicContext& context) {
        const auto other = context.arguments.empty() ? VmObjectRef{} : context.arguments[0].ref;
        return VmValue::Int(context.receiver == other ? 1 : 0);
    });

    builder.VirtualMethod("hashCode", "()I", [](IntrinsicContext& context) {
        return VmValue::Int(context.vm.Model().IdentityHashCode(context.receiver));
    });

    builder.VirtualMethod("toString", "()Ljava/lang/String;", [](IntrinsicContext& context) {
        auto& vm = context.vm;
        const auto java_class = vm.Model().ObjectClass(context.receiver);
        if (!java_class.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "object has no VM class"};
        }
        auto& linker = vm.Linker();
        const auto hash_index = linker.FindVtableIndex(java_class, "hashCode", "()I");
        if (!hash_index.has_value()) {
            throw DexVmError(DexVmErrorReason::internal_invariant, "Object.toString receiver has no hashCode()");
        }
        const auto hash_outcome =
            vm.Call(linker.Class(java_class).vtable[*hash_index], std::vector<VmValue>{VmValue::Ref(context.receiver)});
        if (hash_outcome.exception.IsValid()) {
            vm.SetPendingException(hash_outcome.exception);
            return VmValue::Ref(VmObjectRef{});
        }
        if (hash_outcome.value.kind != VmValue::Kind::cat1) {
            throw DexVmError(DexVmErrorReason::internal_invariant, "hashCode() returned a non-int value");
        }
        const auto descriptor = linker.Class(java_class).descriptor;
        return VmValue::Ref(vm.NewStringUtf8(DottedName(descriptor) + "@" + HashCodeHex(hash_outcome.value.AsInt())));
    });

    builder.VirtualMethod(
        "clone", "()Ljava/lang/Object;",
        [](IntrinsicContext& context) {
            // Folded Object.clone() + internalClone: Cloneable check is the
            // Java half (libcore Object.java); CloneObject is dvmCloneObject.
            auto& vm = context.vm;
            const auto java_class = vm.Model().ObjectClass(context.receiver);
            if (!java_class.IsValid()) {
                throw VmJavaThrow{"Ljava/lang/CloneNotSupportedException;", "object has no VM class"};
            }
            const auto cloneable = vm.Linker().ResolveDescriptor("Ljava/lang/Cloneable;");
            if (!vm.Linker().IsAssignable(cloneable, java_class)) {
                throw VmJavaThrow{"Ljava/lang/CloneNotSupportedException;", "Class doesn't implement Cloneable"};
            }
            return VmValue::Ref(vm.CloneObject(context.receiver));
        },
        kAccProtected);

    builder.FinalMethod("getClass", "()Ljava/lang/Class;", [](IntrinsicContext& context) {
        auto& vm = context.vm;
        const auto java_class = vm.Model().ObjectClass(context.receiver);
        if (!java_class.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "object has no VM class"};
        }
        return VmValue::Ref(vm.Model().ClassObject(java_class));
    });

    builder.FinalMethod("notify", "()V", [](IntrinsicContext& context) {
        context.vm.NotifyMonitor(context.receiver, false);
        return VmValue::Void();
    });

    builder.FinalMethod("notifyAll", "()V", [](IntrinsicContext& context) {
        context.vm.NotifyMonitor(context.receiver, true);
        return VmValue::Void();
    });

    builder.FinalMethod("wait", "()V", [](IntrinsicContext& context) {
        context.vm.WaitOnMonitor(context.receiver, 0);
        return VmValue::Void();
    });

    builder.FinalMethod("wait", "(J)V", [](IntrinsicContext& context) {
        // 0 means "no deadline" in the Java contract, which is exactly what
        // WaitOnMonitor treats it as.
        context.vm.WaitOnMonitor(context.receiver, context.arguments[0].AsLong());
        return VmValue::Void();
    });

    builder.FinalMethod("wait", "(JI)V", [](IntrinsicContext& context) {
        // AOSP vm/Sync.cpp waitMonitor validates the (msec, nsec) pair
        // as a unit before parking. The monitor table parks on whole
        // milliseconds only, so a nonzero nanos rounds the deadline up
        // to the next millisecond; (0, 0) stays untimed like wait().
        const auto millis = context.arguments[0].AsLong();
        const auto nanos = context.arguments[1].AsInt();
        if (millis < 0 || nanos < 0 || nanos > 999999) {
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "timeout arguments out of range"};
        }
        auto deadline = millis;
        if (nanos > 0 && deadline < std::numeric_limits<std::int64_t>::max()) {
            ++deadline;
        }
        context.vm.WaitOnMonitor(context.receiver, deadline);
        return VmValue::Void();
    });

    auto result = std::move(builder).Build();
    return result;
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_primitive_wrappers.cpp ----
#include "catalog.h"
#include "shared.h"

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

namespace {

[[nodiscard]] VmValue Bool(bool value) {
    return VmValue::Int(value ? 1 : 0);
}

// Ordinary decimal rendering uses the pinned libcore algorithm, shared with builders.
[[nodiscard]] VmValue FormatFloating(Interpreter& vm, VmValue value, bool wide) {
    const auto type = vm.Linker().ResolveDescriptor("Ljava/lang/RealToString;");
    const auto getter = vm.Linker().FindDirectMethod(type, "getInstance", "()Ljava/lang/RealToString;");
    if (!getter) throw DexVmError(DexVmErrorReason::unresolved_reference, "RealToString.getInstance is unavailable");
    const auto converter = vm.Call(*getter, {});
    if (converter.exception.IsValid())
        throw VmJavaThrow{vm.Linker().Class(converter.exception_class).descriptor, converter.exception_message,
                          converter.exception};
    const auto roots = vm.ProtectReferences(std::array{converter.value.ref});
    return InvokeGuest(vm, converter.value.ref, wide ? "doubleToString" : "floatToString",
                       wide ? "(D)Ljava/lang/String;" : "(F)Ljava/lang/String;", {value});
}

[[nodiscard]] std::uint32_t CanonicalFloatBits(float value) {
    return std::isnan(value) ? 0x7fc00000U : std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint64_t CanonicalDoubleBits(double value) {
    return std::isnan(value) ? 0x7ff8000000000000ULL : std::bit_cast<std::uint64_t>(value);
}

template <typename Float, typename Bits>
IntrinsicClassDecl DeclareFloatingNatives(const char* descriptor, const char* primitive, const char* bits_to_name,
                                          const char* raw_bits_name, const char* from_bits_name) {
    auto builder = IntrinsicClassBuilder::Class(descriptor);
    builder.MergesBootClass();
    constexpr bool wide = sizeof(Float) == 8;
    const auto argument = [](const VmValue& value) -> Float {
        if constexpr (wide)
            return value.AsDouble();
        else
            return value.AsFloat();
    };
    const auto answer = [](Float value) -> VmValue {
        if constexpr (wide)
            return VmValue::Double(value);
        else
            return VmValue::Float(value);
    };
    const auto bits = [](Float value) -> Bits { return std::bit_cast<Bits>(value); };
    builder.StaticMethod(
        bits_to_name, std::string("(") + primitive + ")" + (wide ? "J" : "I"),
        [argument](IntrinsicContext& c) {
            if constexpr (wide)
                return VmValue::Long(static_cast<std::int64_t>(CanonicalDoubleBits(argument(c.arguments[0]))));
            else
                return VmValue::Int(static_cast<std::int32_t>(CanonicalFloatBits(argument(c.arguments[0]))));
        },
        kAccPublic | kAccNative);
    builder.StaticMethod(
        raw_bits_name, std::string("(") + primitive + ")" + (wide ? "J" : "I"),
        [argument, bits](IntrinsicContext& c) {
            if constexpr (wide)
                return VmValue::Long(static_cast<std::int64_t>(bits(argument(c.arguments[0]))));
            else
                return VmValue::Int(static_cast<std::int32_t>(bits(argument(c.arguments[0]))));
        },
        kAccPublic | kAccNative);
    builder.StaticMethod(
        from_bits_name, std::string("(") + (wide ? "J" : "I") + ")" + primitive,
        [answer](IntrinsicContext& c) {
            Bits raw;
            if constexpr (wide)
                raw = static_cast<Bits>(c.arguments[0].AsLong());
            else
                raw = static_cast<Bits>(c.arguments[0].AsInt());
            return answer(std::bit_cast<Float>(raw));
        },
        kAccPublic | kAccNative);
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_java_lang_Float() {
    return DeclareFloatingNatives<float, std::uint32_t>("Ljava/lang/Float;", "F", "floatToIntBits", "floatToRawIntBits",
                                                        "intBitsToFloat");
}

IntrinsicClassDecl Declare_java_lang_Double() {
    return DeclareFloatingNatives<double, std::uint64_t>("Ljava/lang/Double;", "D", "doubleToLongBits",
                                                         "doubleToRawLongBits", "longBitsToDouble");
}

IntrinsicClassDecl Declare_java_lang_Character() {
    auto b = IntrinsicClassBuilder::Class("Ljava/lang/Character;");
    b.AdmitBootNativeMethods();
    return std::move(b).Build();
}

// Host tokens remain owned by BigIntRuntime. Guest arithmetic sees only value snapshots.
VmValue NativeBnArithmetic(IntrinsicContext& c, const char* method, bool division, bool unary, bool in_place = false) {
    auto& vm = c.vm;
    const auto token = [&](std::size_t i) { return static_cast<std::uint64_t>(c.arguments[i].AsLong()); };
    const auto destination = token(0);
    const auto remainder = division ? token(1) : 0;
    if (destination)
        static_cast<void>(vm.BigInts().Require(destination));
    else if (!division)
        static_cast<void>(vm.BigInts().Require(destination));
    if (remainder) static_cast<void>(vm.BigInts().Require(remainder));
    if (division && ((!destination && !remainder) || (destination && destination == remainder)))
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid quotient/remainder destinations"};
    const auto a_index = division ? 2U : in_place ? 0U : 1U;
    const auto a = vm.BigInts().Require(token(a_index));
    const auto b = unary ? BigIntRuntime::Number{} : vm.BigInts().Require(token(a_index + 1));
    const auto encode = [&](const BigIntRuntime::Number& value) {
        auto bytes = value.Bytes();
        bytes.insert(bytes.begin(), value.negative ? std::byte{1} : std::byte{0});
        const auto array = vm.Model().NewPrimitiveArray(vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                                                        static_cast<JniSize>(bytes.size()));
        vm.Model().WriteByteRegion(array, 0, bytes);
        return array;
    };
    const auto first = encode(a);
    const auto first_root = vm.ProtectReferences(std::array{first});
    const auto second = unary ? VmObjectRef{} : encode(b);
    const auto second_root = vm.ProtectReferences(std::array{second});
    const auto type = vm.Linker().ResolveDescriptor("Lorg/ogplay/math/NativeBigInteger;");
    const auto signature = unary ? "([BI)[B" : "([B[B)[B";
    const auto target = vm.Linker().FindDirectMethod(type, method, signature);
    if (!target)
        throw DexVmError(DexVmErrorReason::unresolved_reference, "NativeBigInteger arithmetic boundary missing");
    const auto argument = unary ? VmValue::Int(c.arguments[in_place ? 1 : 2].AsInt()) : VmValue::Ref(second);
    const auto outcome = vm.Call(*target, std::array{VmValue::Ref(first), argument});
    if (outcome.exception.IsValid()) {
        const auto& descriptor = vm.Linker().Class(outcome.exception_class).descriptor;
        if (descriptor == "Ljava/lang/UnsupportedOperationException;")
            if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.native_bn_arithmetic.budget", 0);
        throw VmJavaThrow{descriptor, outcome.exception_message, outcome.exception};
    }
    const auto result_root = vm.ProtectReferences(std::array{outcome.value.ref});
    const auto length = vm.Model().ArrayLength(outcome.value.ref);
    if (length < 1 || length > (division ? 2097158 : 1048577) ||
        vm.Model().PrimitiveArrayKind(outcome.value.ref) != JniPrimitiveKind::byte)
        throw DexVmError(DexVmErrorReason::internal_invariant, "invalid integer arithmetic result");
    const auto encoded = vm.Model().ReadByteRegion(outcome.value.ref, 0, length);
    const auto decode = [&](std::span<const std::byte> bytes) {
        if (bytes.empty() || bytes.size() > 1048577 || (bytes[0] != std::byte{0} && bytes[0] != std::byte{1}))
            throw DexVmError(DexVmErrorReason::internal_invariant, "invalid integer result encoding");
        BigIntRuntime::Number value;
        value.SetBytes(bytes.subspan(1), bytes[0] == std::byte{1}, false);
        return value;
    };
    if (division) {
        if (encoded.size() < 6)
            throw DexVmError(DexVmErrorReason::internal_invariant, "invalid integer division result");
        std::uint32_t quotient_length = 0;
        for (unsigned i = 0; i < 4; ++i)
            quotient_length = (quotient_length << 8) | std::to_integer<std::uint32_t>(encoded[i]);
        if (quotient_length < 1 || quotient_length > encoded.size() - 5)
            throw DexVmError(DexVmErrorReason::internal_invariant, "invalid integer quotient length");
        const auto payload = std::span<const std::byte>{encoded}.subspan(4);
        auto quotient_value = decode(payload.first(quotient_length));
        auto remainder_value = decode(payload.subspan(quotient_length));
        // Validate both destinations again after nested execution, before either commit.
        auto* quotient_slot = destination ? &vm.BigInts().Require(destination) : nullptr;
        auto* remainder_slot = remainder ? &vm.BigInts().Require(remainder) : nullptr;
        if (quotient_slot) *quotient_slot = std::move(quotient_value);
        if (remainder_slot) *remainder_slot = std::move(remainder_value);
    } else {
        auto value = decode(encoded);
        vm.BigInts().Require(destination) = std::move(value);
    }
    return VmValue::Void();
}
}  // namespace

IntrinsicClassDecl Declare_java_math_NativeBN() {
    auto b = IntrinsicClassBuilder::Class("Ljava/math/NativeBN;");
    b.MergesBootClass();
    b.StaticMethod(
        "BN_new", "()J",
        [](IntrinsicContext& c) { return VmValue::Long(static_cast<std::int64_t>(c.vm.BigInts().New())); },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "BN_free", "(J)V",
        [](IntrinsicContext& c) {
            c.vm.BigInts().Free(static_cast<std::uint64_t>(c.arguments[0].AsLong()));
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    const auto get = [](IntrinsicContext& c, std::size_t i) -> BigIntRuntime::Number& {
        return c.vm.BigInts().Require(static_cast<std::uint64_t>(c.arguments[i].AsLong()));
    };
    b.StaticMethod(
        "putULongInt", "(JJZ)V",
        [get](IntrinsicContext& c) {
            get(c, 0).SetLong(static_cast<std::uint64_t>(c.arguments[1].AsLong()), c.arguments[2].AsInt() != 0);
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "putLongInt", "(JJ)V",
        [get](IntrinsicContext& c) {
            const auto v = c.arguments[1].AsLong();
            get(c, 0).SetLong(v < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v),
                              v < 0);
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "BN_copy", "(JJ)V",
        [get](IntrinsicContext& c) {
            get(c, 0) = get(c, 1);
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "BN_cmp", "(JJ)I", [get](IntrinsicContext& c) { return VmValue::Int(get(c, 0).Compare(get(c, 1))); },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "sign", "(J)I",
        [get](IntrinsicContext& c) {
            const auto& n = get(c, 0);
            return VmValue::Int(n.words.empty() ? 0 : n.negative ? -1 : 1);
        },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "longInt", "(J)J",
        [get](IntrinsicContext& c) {
            const auto& n = get(c, 0);
            return VmValue::Long(
                std::bit_cast<std::int64_t>(n.negative ? std::uint64_t{0} - n.LowLong() : n.LowLong()));
        },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "bitLength", "(J)I", [get](IntrinsicContext& c) { return VmValue::Int(get(c, 0).BitLength()); },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "BN_set_negative", "(JI)V",
        [get](IntrinsicContext& c) {
            auto& n = get(c, 0);
            n.negative = !n.words.empty() && c.arguments[1].AsInt();
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "bn2litEndInts", "(J)[I",
        [get](IntrinsicContext& c) {
            const auto words = get(c, 0).words;
            if (words.empty()) return VmValue::Ref(VmObjectRef{});
            const auto a = c.vm.Model().NewPrimitiveArray(
                c.vm.Linker().ResolveDescriptor("[I"), JniPrimitiveKind::integer, static_cast<JniSize>(words.size()));
            for (std::size_t i = 0; i < words.size(); ++i)
                c.vm.Model().SetPrimitiveElement(a, static_cast<JniSize>(i), words[i]);
            return VmValue::Ref(a);
        },
        kAccPublic | kAccNative);
    const auto read_count = [](IntrinsicContext& c, JniPrimitiveKind kind) {
        const auto a = IntrinsicCall(c).NonNullRef(0, "integer array");
        const auto count = c.arguments[1].AsInt();
        if (c.vm.Model().PrimitiveArrayKind(a) != kind)
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "wrong integer array type"};
        if (count < 0 || count > c.vm.Model().ArrayLength(a))
            throw VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "integer input length"};
        if (count > (kind == JniPrimitiveKind::integer ? 262144 : 1048576))
            throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "integer input exceeds value codec limit"};
        return count;
    };
    b.StaticMethod(
        "BN_bin2bn", "([BIZJ)V",
        [get, read_count](IntrinsicContext& c) {
            const auto count = read_count(c, JniPrimitiveKind::byte);
            const auto bytes = c.vm.Model().ReadByteRegion(c.arguments[0].ref, 0, count);
            get(c, 3).SetBytes(bytes, c.arguments[2].AsInt() != 0, false);
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "twosComp2bn", "([BIJ)V",
        [get, read_count](IntrinsicContext& c) {
            const auto count = read_count(c, JniPrimitiveKind::byte);
            const auto bytes = c.vm.Model().ReadByteRegion(c.arguments[0].ref, 0, count);
            get(c, 2).SetBytes(bytes, false, true);
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "litEndInts2bn", "([IIZJ)V",
        [get, read_count](IntrinsicContext& c) {
            const auto count = read_count(c, JniPrimitiveKind::integer);
            BigIntRuntime::Number value;
            for (int i = 0; i < count; ++i)
                value.words.push_back(
                    static_cast<std::uint32_t>(c.vm.Model().GetPrimitiveElement(c.arguments[0].ref, i)));
            value.negative = c.arguments[2].AsInt() != 0;
            value.Normalize();
            get(c, 3) = std::move(value);
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "BN_bn2bin", "(J)[B",
        [get](IntrinsicContext& c) {
            const auto bytes = get(c, 0).Bytes();
            const auto a = c.vm.Model().NewPrimitiveArray(c.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                                                          static_cast<JniSize>(bytes.size()));
            c.vm.Model().WriteByteRegion(a, 0, bytes);
            return VmValue::Ref(a);
        },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "BN_bn2dec", "(J)Ljava/lang/String;",
        [get](IntrinsicContext& c) { return VmValue::Ref(c.vm.NewStringUtf8(get(c, 0).String(10))); },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "BN_bn2hex", "(J)Ljava/lang/String;",
        [get](IntrinsicContext& c) { return VmValue::Ref(c.vm.NewStringUtf8(get(c, 0).String(16))); },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "BN_dec2bn", "(JLjava/lang/String;)I",
        [get](IntrinsicContext& c) {
            auto& value = get(c, 0);
            const auto text = IntrinsicCall(c).NonNullRef(1, "decimal string");
            return VmValue::Int(value.SetDecimal(c.vm.StringUtf8(text)));
        },
        kAccPublic | kAccNative);
    // BootDex admission requires every native signature to be classified.
    // Retain explicit failing declarations for the unsupported big-number API.
    b.StaticMethod(
        "BN_add", "(JJJ)V", [](IntrinsicContext& c) { return NativeBnArithmetic(c, "add", false, false, false); },
        kAccPublic | kAccNative);
    b.UnimplementedStatic("BN_add_word", "(JI)V", kAccPublic | kAccNative);
    b.StaticMethod(
        "BN_div", "(JJJJ)V", [](IntrinsicContext& c) { return NativeBnArithmetic(c, "divide", true, false, false); },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "BN_exp", "(JJJ)V", [](IntrinsicContext& c) { return NativeBnArithmetic(c, "power", false, false, false); },
        kAccPublic | kAccNative);
    b.UnimplementedStatic("BN_gcd", "(JJJ)V", kAccPublic | kAccNative);
    b.UnimplementedStatic("BN_generate_prime_ex", "(JIZJJJ)V", kAccPublic | kAccNative);
    b.UnimplementedStatic("BN_hex2bn", "(JLjava/lang/String;)I", kAccPublic | kAccNative);
    b.UnimplementedStatic("BN_is_bit_set", "(JI)Z", kAccPublic | kAccNative);
    b.UnimplementedStatic("BN_is_prime_ex", "(JIJ)Z", kAccPublic | kAccNative);
    b.UnimplementedStatic("BN_mod_exp", "(JJJJ)V", kAccPublic | kAccNative);
    b.UnimplementedStatic("BN_mod_inverse", "(JJJ)V", kAccPublic | kAccNative);
    b.UnimplementedStatic("BN_mod_word", "(JI)I", kAccPublic | kAccNative);
    b.StaticMethod(
        "BN_mul", "(JJJ)V", [](IntrinsicContext& c) { return NativeBnArithmetic(c, "multiply", false, false, false); },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "BN_mul_word", "(JI)V",
        [](IntrinsicContext& c) { return NativeBnArithmetic(c, "multiplyWord", false, true, true); },
        kAccPublic | kAccNative);
    b.UnimplementedStatic("BN_nnmod", "(JJJ)V", kAccPublic | kAccNative);
    b.StaticMethod(
        "BN_shift", "(JJI)V", [](IntrinsicContext& c) { return NativeBnArithmetic(c, "shift", false, true, false); },
        kAccPublic | kAccNative);
    b.UnimplementedStatic("BN_sub", "(JJJ)V", kAccPublic | kAccNative);
    return std::move(b).Build();
}

void AppendJavaLangPrimitiveWrappers(std::vector<IntrinsicClassDecl>& catalog) {
    auto arithmetic = IntrinsicClassBuilder::Class("Lorg/ogplay/math/NativeBigInteger;");
    arithmetic.AdmitBootNativeMethods();
    catalog.push_back(std::move(arithmetic).Build());
    catalog.push_back(Declare_java_math_NativeBN());
    catalog.push_back(Declare_java_lang_Float());
    catalog.push_back(Declare_java_lang_Double());
    catalog.push_back(Declare_java_lang_Character());
}

}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_String.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

namespace {

VmValue InvokeGuestDirect(Interpreter& vm, const char* owner, const char* name, const char* signature,
                          std::vector<VmValue> arguments) {
    const auto type = vm.Linker().ResolveDescriptor(owner);
    const auto method = vm.Linker().FindDirectMethod(type, name, signature);
    if (!method) throw DexVmError(DexVmErrorReason::unresolved_reference, std::string(owner) + "->" + name + signature);
    const auto result = vm.Call(*method, arguments);
    if (result.exception.IsValid())
        throw VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                          result.exception};
    return result.value;
}

VmValue FormatWithFormatter(IntrinsicContext& context, const VmObjectRef locale, const VmObjectRef format,
                            const VmObjectRef arguments) {
    if (!format.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "format == null"};
    }
    auto& vm = context.vm;
    const auto input_roots = vm.ProtectReferences(std::array{locale, format, arguments});
    const auto argument_count = arguments.IsValid() ? vm.Model().ArrayLength(arguments) : 0;
    const auto capacity =
        static_cast<std::int32_t>(Value(context, format).size() + static_cast<std::size_t>(argument_count) * 10U);
    const auto builder = vm.NewIntrinsicInstance("Ljava/lang/StringBuilder;");
    const auto builder_roots = vm.ProtectReferences(std::array{locale, format, arguments, builder});
    InvokeGuestDirect(vm, "Ljava/lang/StringBuilder;", "<init>", "(I)V",
                      {VmValue::Ref(builder), VmValue::Int(capacity)});
    const auto formatter = vm.NewIntrinsicInstance("Ljava/util/Formatter;");
    const auto formatter_roots = vm.ProtectReferences(std::array{locale, format, arguments, builder, formatter});
    InvokeGuestDirect(vm, "Ljava/util/Formatter;", "<init>", "(Ljava/lang/Appendable;Ljava/util/Locale;)V",
                      {VmValue::Ref(formatter), VmValue::Ref(builder), VmValue::Ref(locale)});
    const auto formatted =
        InvokeGuest(vm, formatter, "format", "(Ljava/lang/String;[Ljava/lang/Object;)Ljava/util/Formatter;",
                    {VmValue::Ref(format), VmValue::Ref(arguments)})
            .ref;
    const auto result_roots = vm.ProtectReferences(std::array{builder, formatter, formatted});
    return InvokeGuest(vm, formatted, "toString", "()Ljava/lang/String;");
}

VmValue InvokeGuestStatic(Interpreter& vm, const char* owner, const char* name, const char* signature,
                          std::vector<VmValue> arguments = {}) {
    const auto type = vm.Linker().ResolveDescriptor(owner);
    const auto method = vm.Linker().FindDirectMethod(type, name, signature);
    if (!method) throw DexVmError(DexVmErrorReason::unresolved_reference, std::string(owner) + "->" + name + signature);
    const auto result = vm.Call(*method, arguments);
    if (result.exception.IsValid())
        throw VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                          result.exception};
    return result.value;
}

VmValue MapCase(IntrinsicContext& context, bool upper, VmObjectRef locale) {
    if (!locale.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "locale == null"};
    const auto roots = context.vm.ProtectReferences(std::array{context.receiver, locale});
    const auto language = detail::InvokeGuest(context.vm, locale, "getLanguage", "()Ljava/lang/String;");
    const auto locale_name = context.vm.StringUtf8(language.ref);
    auto result = Value(context, context.receiver);
    const bool simple_locale = locale_name.empty() || locale_name == "en" || locale_name == "zh";
    const bool simple_units = std::ranges::all_of(result, [upper](const char16_t unit) {
        return unit < 0x80 ||
               (!upper && ((unit >= 0x00c0U && unit <= 0x00d6U) || (unit >= 0x00d8U && unit <= 0x00deU))) ||
               (upper && ((unit >= 0x00e0U && unit <= 0x00f6U) || (unit >= 0x00f8U && unit <= 0x00feU)));
    });
    if (!simple_locale || !simple_units) {
        const auto java_locale_name = detail::InvokeGuest(context.vm, locale, "toString", "()Ljava/lang/String;").ref;
        const auto call_roots = context.vm.ProtectReferences(std::array{context.receiver, locale, java_locale_name});
        return InvokeGuestStatic(context.vm, "Llibcore/icu/ICU;", upper ? "toUpperCase" : "toLowerCase",
                                 "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                                 {VmValue::Ref(context.receiver), VmValue::Ref(java_locale_name)});
    }
    for (auto& unit : result) {
        if (unit >= u'A' && unit <= u'Z' && !upper)
            unit = static_cast<char16_t>(unit - u'A' + u'a');
        else if (unit >= u'a' && unit <= u'z' && upper)
            unit = static_cast<char16_t>(unit - u'a' + u'A');
        else if (!upper && ((unit >= 0x00c0U && unit <= 0x00d6U) || (unit >= 0x00d8U && unit <= 0x00deU)))
            unit = static_cast<char16_t>(unit + 0x20U);
        else if (upper && ((unit >= 0x00e0U && unit <= 0x00f6U) || (unit >= 0x00f8U && unit <= 0x00feU)))
            unit = static_cast<char16_t>(unit - 0x20U);
    }
    return result == Value(context, context.receiver) ? VmValue::Ref(context.receiver) : Make(context, result);
}

}  // namespace

IntrinsicClassDecl Declare_java_lang_String() {
    auto builder =
        IntrinsicClassBuilder::Class("Ljava/lang/String;", "Ljava/lang/Object;",
                                     {"Ljava/lang/CharSequence;", "Ljava/lang/Comparable;", "Ljava/io/Serializable;"});
    builder.ConstantInt("serialVersionUID", "J", INT64_C(-6849794470754667710), kAccPrivate);
    builder.FinalMethod(
        "intern", "()Ljava/lang/String;",
        [](IntrinsicContext& c) { return VmValue::Ref(c.vm.Model().InternString(c.receiver)); },
        kAccPublic | kAccNative);
    builder.Constructor("()V", [](IntrinsicContext& context) {
        context.vm.Model().BindString(context.receiver, {});
        return VmValue::Void();
    });
    builder.Constructor("(Ljava/lang/String;)V", [](IntrinsicContext& context) {
        context.vm.Model().BindString(context.receiver, Value(context, context.arguments[0].ref));
        return VmValue::Void();
    });
    builder.Constructor("([B)V", [](IntrinsicContext& context) {
        auto& model = context.vm.Model();
        const auto array = RequireArray(context.arguments[0].ref);
        const auto bytes = model.ReadByteRegion(array, 0, model.ArrayLength(array));
        model.BindString(context.receiver, Utf8DecodeReplace(bytes));
        return VmValue::Void();
    });
    builder.Constructor("([BII)V", [](IntrinsicContext& context) {
        auto& model = context.vm.Model();
        const auto array = RequireArray(context.arguments[0].ref);
        const auto offset = context.arguments[1].AsInt();
        const auto count = context.arguments[2].AsInt();
        CheckRegion(model.ArrayLength(array), offset, count);
        const auto bytes = model.ReadByteRegion(array, offset, count);
        model.BindString(context.receiver, Utf8DecodeReplace(bytes));
        return VmValue::Void();
    });
    builder.Constructor("([BLjava/lang/String;)V", [](IntrinsicContext& c) {
        const auto array = RequireArray(c.arguments[0].ref);
        if (!c.arguments[1].ref.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "charset == null"};
        std::string name;
        try {
            name = CanonicalCharset(c.vm.StringUtf8(c.arguments[1].ref));
        } catch (const VmJavaThrow& e) {
            throw VmJavaThrow{"Ljava/io/UnsupportedEncodingException;", e.message};
        }
        c.vm.Model().BindString(
            c.receiver, DecodeCharset(c.vm.Model().ReadByteRegion(array, 0, c.vm.Model().ArrayLength(array)), name));
        return VmValue::Void();
    });
    builder.Constructor("([BIILjava/lang/String;)V", [](IntrinsicContext& c) {
        const auto array = RequireArray(c.arguments[0].ref);
        if (!c.arguments[3].ref.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "charset == null"};
        const auto offset = c.arguments[1].AsInt();
        const auto count = c.arguments[2].AsInt();
        CheckRegion(c.vm.Model().ArrayLength(array), offset, count);
        std::string name;
        try {
            name = CanonicalCharset(c.vm.StringUtf8(c.arguments[3].ref));
        } catch (const VmJavaThrow& e) {
            throw VmJavaThrow{"Ljava/io/UnsupportedEncodingException;", e.message};
        }
        c.vm.Model().BindString(c.receiver, DecodeCharset(c.vm.Model().ReadByteRegion(array, offset, count), name));
        return VmValue::Void();
    });
    for (const bool ranged : {false, true}) {
        builder.Constructor(ranged ? "([BIILjava/nio/charset/Charset;)V" : "([BLjava/nio/charset/Charset;)V",
                            [ranged](IntrinsicContext& c) {
                                const auto array = RequireArray(c.arguments[0].ref);
                                const auto length = c.vm.Model().ArrayLength(array);
                                const auto offset = ranged ? c.arguments[1].AsInt() : 0;
                                const auto count = ranged ? c.arguments[2].AsInt() : length;
                                CheckRegion(length, offset, count);
                                const auto name = CharsetName(c.vm, c.arguments[ranged ? 3 : 1].ref);
                                c.vm.Model().BindString(
                                    c.receiver, DecodeCharset(c.vm.Model().ReadByteRegion(array, offset, count), name));
                                return VmValue::Void();
                            });
    }
    for (const bool named : {false, true}) {
        builder.FinalMethod("getBytes", named ? "(Ljava/lang/String;)[B" : "(Ljava/nio/charset/Charset;)[B",
                            [named](IntrinsicContext& c) {
                                if (!c.arguments[0].ref.IsValid())
                                    throw VmJavaThrow{"Ljava/lang/NullPointerException;", "charset == null"};
                                std::string name;
                                try {
                                    name = named ? CanonicalCharset(c.vm.StringUtf8(c.arguments[0].ref))
                                                 : CharsetName(c.vm, c.arguments[0].ref);
                                } catch (const VmJavaThrow& e) {
                                    if (named) throw VmJavaThrow{"Ljava/io/UnsupportedEncodingException;", e.message};
                                    throw;
                                }
                                const auto bytes = EncodeCharset(Value(c, c.receiver), name);
                                const auto array = c.vm.Model().NewPrimitiveArray(c.vm.Linker().ResolveDescriptor("[B"),
                                                                                  JniPrimitiveKind::byte,
                                                                                  static_cast<JniSize>(bytes.size()));
                                c.vm.Model().WriteByteRegion(array, 0, bytes);
                                return VmValue::Ref(array);
                            });
    }
    builder.Constructor("([C)V", [](IntrinsicContext& context) {
        auto& model = context.vm.Model();
        const auto array = RequireArray(context.arguments[0].ref);
        context.vm.Model().BindString(context.receiver, CharsValue(context, array, 0, model.ArrayLength(array)));
        return VmValue::Void();
    });
    // String remains the VM/JNI immutable UTF-16 owner. Snapshot the input:
    // builder fields/arrays remain the only mutable content source.
    builder.Constructor(
        "(II[C)V",
        [](IntrinsicContext& c) {
            const auto array = RequireArray(c.arguments[2].ref);
            const auto offset = c.arguments[0].AsInt(), count = c.arguments[1].AsInt();
            CheckRegion(c.vm.Model().ArrayLength(array), offset, count);
            c.vm.Model().BindString(c.receiver, CharsValue(c, array, offset, count));
            return VmValue::Void();
        },
        0);
    builder.Constructor("([CII)V", [](IntrinsicContext& context) {
        auto& model = context.vm.Model();
        const auto array = RequireArray(context.arguments[0].ref);
        const auto offset = context.arguments[1].AsInt();
        const auto count = context.arguments[2].AsInt();
        CheckRegion(model.ArrayLength(array), offset, count);
        model.BindString(context.receiver, CharsValue(context, array, offset, count));
        return VmValue::Void();
    });
    builder.FinalMethod("getBytes", "()[B", [](IntrinsicContext& context) {
        auto& vm = context.vm;
        const auto bytes = Utf8Encode(Value(context, context.receiver));
        const auto array_class = vm.Linker().ResolveDescriptor("[B");
        const auto array =
            vm.Model().NewPrimitiveArray(array_class, JniPrimitiveKind::byte, static_cast<JniSize>(bytes.size()));
        if (!bytes.empty()) {
            vm.Model().WriteByteRegion(array, 0, bytes);
        }
        return VmValue::Ref(array);
    });
    builder.FinalMethod("length", "()I", [](IntrinsicContext& context) {
        return VmValue::Int(static_cast<std::int32_t>(context.vm.Model().StringValue(context.receiver).size()));
    });
    builder.FinalMethod("charAt", "(I)C", [](IntrinsicContext& context) {
        const auto value = context.vm.Model().StringValue(context.receiver);
        const auto index = context.arguments[0].AsInt();
        if (index < 0 || static_cast<std::size_t>(index) >= value.size()) {
            throw VmJavaThrow{"Ljava/lang/StringIndexOutOfBoundsException;", "index " + std::to_string(index)};
        }
        return VmValue::Int(value[static_cast<std::size_t>(index)]);
    });
    builder.FinalOverrideMethod("equals", "(Ljava/lang/Object;)Z", [](IntrinsicContext& context) {
        const auto other = context.arguments[0].ref;
        if (!other.IsValid()) return VmValue::Int(0);
        auto& model = context.vm.Model();
        const auto other_kind = model.Kind(other);
        if (other_kind != VmObjectKind::string && other_kind != VmObjectKind::external) {
            return VmValue::Int(0);
        }
        return VmValue::Int(model.StringValue(context.receiver) == model.StringValue(other) ? 1 : 0);
    });
    builder.FinalMethod("equalsIgnoreCase", "(Ljava/lang/String;)Z", [](IntrinsicContext& context) {
        const auto other = context.arguments[0].ref;
        if (!other.IsValid()) return VmValue::Int(0);
        return VmValue::Int(CompareStrings(Value(context, context.receiver), Value(context, other), true) == 0 ? 1 : 0);
    });
    builder.FinalOverrideMethod("hashCode", "()I", [](IntrinsicContext& context) {
        return VmValue::Int(JavaStringHash(context.vm.Model().StringValue(context.receiver)));
    });
    builder.FinalOverrideMethod("toString", "()Ljava/lang/String;",
                                [](IntrinsicContext& context) { return VmValue::Ref(context.receiver); });
    builder.FinalMethod("compareTo", "(Ljava/lang/String;)I", [](IntrinsicContext& context) {
        return VmValue::Int(
            CompareStrings(Value(context, context.receiver), Value(context, context.arguments[0].ref), false));
    });
    builder.FinalMethod(
        "compareTo", "(Ljava/lang/Object;)I",
        [](IntrinsicContext& context) {
            const auto other = context.arguments[0].ref;
            if (!other.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "other == null"};
            if (context.vm.Model().Kind(other) != VmObjectKind::string)
                throw VmJavaThrow{"Ljava/lang/ClassCastException;", "other is not a String"};
            return VmValue::Int(CompareStrings(Value(context, context.receiver), Value(context, other), false));
        },
        kAccPublic | kAccFinal | kAccBridge | kAccSynthetic);
    builder.FinalMethod("compareToIgnoreCase", "(Ljava/lang/String;)I", [](IntrinsicContext& context) {
        return VmValue::Int(
            CompareStrings(Value(context, context.receiver), Value(context, context.arguments[0].ref), true));
    });
    const auto region_matches = [](IntrinsicContext& context, bool overloaded) {
        const bool ignore_case = overloaded && context.arguments[0].AsInt() != 0;
        const std::size_t shift = overloaded ? 1U : 0U;
        const auto text = Value(context, context.receiver);
        const auto other_ref = context.arguments[shift + 1].ref;
        if (!other_ref.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "string == null"};
        const auto other = Value(context, other_ref);
        const auto start = context.arguments[shift].AsInt();
        const auto other_start = context.arguments[shift + 2].AsInt();
        const auto length = context.arguments[shift + 3].AsInt();
        if (start < 0 || other_start < 0 || static_cast<std::int64_t>(text.size()) - start < length ||
            static_cast<std::int64_t>(other.size()) - other_start < length)
            return Bool(false);
        const auto fold = [&](char16_t unit) {
            if (unit < 128) return AsciiLower(unit);
            const auto upper =
                InvokeGuestStatic(context.vm, "Ljava/lang/Character;", "toUpperCase", "(C)C", {VmValue::Int(unit)});
            return static_cast<char16_t>(
                InvokeGuestStatic(context.vm, "Ljava/lang/Character;", "toLowerCase", "(C)C", {upper}).AsInt());
        };
        for (std::int32_t i = 0; i < length; ++i) {
            const auto left = text[static_cast<std::size_t>(start) + static_cast<std::size_t>(i)];
            const auto right = other[static_cast<std::size_t>(other_start) + static_cast<std::size_t>(i)];
            if (left != right && (!ignore_case || fold(left) != fold(right))) return Bool(false);
        }
        return Bool(true);
    };
    builder.FinalMethod("regionMatches", "(ILjava/lang/String;II)Z",
                        [region_matches](IntrinsicContext& context) { return region_matches(context, false); });
    builder.FinalMethod("regionMatches", "(ZILjava/lang/String;II)Z",
                        [region_matches](IntrinsicContext& context) { return region_matches(context, true); });
    builder.FinalMethod("concat", "(Ljava/lang/String;)Ljava/lang/String;", [](IntrinsicContext& context) {
        return Make(context, Value(context, context.receiver) + Value(context, context.arguments[0].ref));
    });
    builder.FinalMethod("startsWith", "(Ljava/lang/String;)Z", [](IntrinsicContext& context) {
        return VmValue::Int(Value(context, context.receiver).starts_with(Value(context, context.arguments[0].ref)) ? 1
                                                                                                                   : 0);
    });
    builder.FinalMethod("startsWith", "(Ljava/lang/String;I)Z", [](IntrinsicContext& c) {
        const auto text = Value(c, c.receiver);
        const auto prefix = Value(c, IntrinsicCall(c).NonNullRef(0, "prefix"));
        const auto offset = c.arguments[1].AsInt();
        return VmValue::Int(offset >= 0 && static_cast<std::size_t>(offset) <= text.size() &&
                            std::u16string_view(text).substr(static_cast<std::size_t>(offset)).starts_with(prefix));
    });
    builder.FinalMethod("endsWith", "(Ljava/lang/String;)Z", [](IntrinsicContext& context) {
        return VmValue::Int(Value(context, context.receiver).ends_with(Value(context, context.arguments[0].ref)) ? 1
                                                                                                                 : 0);
    });
    builder.FinalMethod("indexOf", "(I)I", [](IntrinsicContext& context) {
        const auto haystack = Value(context, context.receiver);
        const auto found = haystack.find(static_cast<char16_t>(context.arguments[0].cat1 & 0xffffU));
        return VmValue::Int(found == std::u16string::npos ? -1 : static_cast<std::int32_t>(found));
    });
    builder.FinalMethod("indexOf", "(II)I", [](IntrinsicContext& context) {
        const auto haystack = Value(context, context.receiver);
        const auto from = context.arguments[1].AsInt();
        const auto start = from < 0 ? 0 : static_cast<std::size_t>(from);
        if (start >= haystack.size()) return VmValue::Int(-1);
        const auto found = haystack.find(static_cast<char16_t>(context.arguments[0].cat1 & 0xffffU), start);
        return VmValue::Int(found == std::u16string::npos ? -1 : static_cast<std::int32_t>(found));
    });
    builder.FinalMethod("contains", "(Ljava/lang/CharSequence;)Z", [](IntrinsicContext& context) {
        const auto haystack = Value(context, context.receiver);
        const auto needle = Value(context, context.arguments[0].ref);
        return VmValue::Int(haystack.find(needle) != std::u16string::npos ? 1 : 0);
    });
    const IntrinsicHandler copy_chars = [](IntrinsicContext& context) {
        auto& model = context.vm.Model();
        const auto value = Value(context, context.receiver);
        const auto src_begin = context.arguments[0].AsInt();
        const auto src_end = context.arguments[1].AsInt();
        const auto target = RequireArray(context.arguments[2].ref);
        const auto dst_begin = context.arguments[3].AsInt();
        if (src_begin < 0 || src_begin > src_end || static_cast<std::size_t>(src_end) > value.size()) {
            throw VmJavaThrow{"Ljava/lang/StringIndexOutOfBoundsException;", "getChars source range is invalid"};
        }
        const auto count = src_end - src_begin;
        if (dst_begin < 0 || static_cast<std::int64_t>(dst_begin) + count > model.ArrayLength(target)) {
            throw VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "getChars destination range is invalid"};
        }
        for (std::int32_t index = 0; index < count; ++index) {
            model.SetPrimitiveElement(target, dst_begin + index, value[static_cast<std::size_t>(src_begin + index)]);
        }
        return VmValue::Void();
    };
    builder.FinalMethod("getChars", "(II[CI)V", copy_chars);
    builder.FinalMethod("_getChars", "(II[CI)V", copy_chars, 0);
    builder.FinalMethod("toCharArray", "()[C", [](IntrinsicContext& context) {
        auto& vm = context.vm;
        const auto value = Value(context, context.receiver);
        const auto array_class = vm.Linker().ResolveDescriptor("[C");
        const auto array =
            vm.Model().NewPrimitiveArray(array_class, JniPrimitiveKind::character, static_cast<JniSize>(value.size()));
        for (std::size_t index = 0; index < value.size(); ++index) {
            vm.Model().SetPrimitiveElement(array, static_cast<JniSize>(index), value[index]);
        }
        return VmValue::Ref(array);
    });
    builder.FinalMethod("replace", "(CC)Ljava/lang/String;", [](IntrinsicContext& context) {
        auto value = Value(context, context.receiver);
        const auto before = static_cast<char16_t>(context.arguments[0].AsInt());
        const auto after = static_cast<char16_t>(context.arguments[1].AsInt());
        bool changed = false;
        for (auto& unit : value) {
            if (unit == before) {
                unit = after;
                changed = true;
            }
        }
        return changed ? Make(context, value) : VmValue::Ref(context.receiver);
    });
    builder.FinalMethod("replace", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Ljava/lang/String;",
                        [](IntrinsicContext& context) {
                            const auto value = Value(context, context.receiver);
                            const auto target = Value(context, context.arguments[0].ref);
                            const auto replacement = Value(context, context.arguments[1].ref);
                            std::u16string out;
                            if (target.empty()) {
                                // Empty target: the replacement goes between every char.
                                out = replacement;
                                for (const auto unit : value) {
                                    out.push_back(unit);
                                    out += replacement;
                                }
                            } else {
                                std::size_t cursor = 0;
                                while (cursor <= value.size()) {
                                    const auto found = value.find(target, cursor);
                                    if (found == std::u16string::npos) {
                                        out.append(value, cursor, value.size() - cursor);
                                        break;
                                    }
                                    out.append(value, cursor, found - cursor);
                                    out += replacement;
                                    cursor = found + target.size();
                                }
                            }
                            return Make(context, out);
                        });
    for (const auto* method : {"replaceAll", "replaceFirst"}) {
        builder.FinalMethod(
            method, "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", [method](IntrinsicContext& c) {
                auto& vm = c.vm;
                const auto pattern =
                    InvokeGuestStatic(vm, "Ljava/util/regex/Pattern;", "compile",
                                      "(Ljava/lang/String;)Ljava/util/regex/Pattern;", {c.arguments[0]})
                        .ref;
                const auto pattern_root = vm.ProtectReferences(std::array{pattern});
                const auto matcher =
                    InvokeGuest(vm, pattern, "matcher", "(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;",
                                {VmValue::Ref(c.receiver)})
                        .ref;
                const auto matcher_root = vm.ProtectReferences(std::array{matcher});
                return InvokeGuest(vm, matcher, method, "(Ljava/lang/String;)Ljava/lang/String;", {c.arguments[1]});
            });
    }
    builder.FinalMethod("matches", "(Ljava/lang/String;)Z", [](IntrinsicContext& c) {
        return InvokeGuestStatic(c.vm, "Ljava/util/regex/Pattern;", "matches",
                                 "(Ljava/lang/String;Ljava/lang/CharSequence;)Z",
                                 {c.arguments[0], VmValue::Ref(c.receiver)});
    });
    for (const bool limit : {false, true}) {
        builder.FinalMethod(
            "split", limit ? "(Ljava/lang/String;I)[Ljava/lang/String;" : "(Ljava/lang/String;)[Ljava/lang/String;",
            [limit](IntrinsicContext& c) {
                const auto count = limit ? c.arguments[1] : VmValue::Int(0);
                const auto fast = InvokeGuestStatic(c.vm, "Ljava/util/regex/Splitter;", "fastSplit",
                                                    "(Ljava/lang/String;Ljava/lang/String;I)[Ljava/lang/String;",
                                                    {c.arguments[0], VmValue::Ref(c.receiver), count});
                if (fast.ref.IsValid()) return fast;
                const auto pattern =
                    InvokeGuestStatic(c.vm, "Ljava/util/regex/Pattern;", "compile",
                                      "(Ljava/lang/String;)Ljava/util/regex/Pattern;", {c.arguments[0]})
                        .ref;
                const auto root = c.vm.ProtectReferences(std::array{pattern});
                return InvokeGuest(c.vm, pattern, "split", "(Ljava/lang/CharSequence;I)[Ljava/lang/String;",
                                   {VmValue::Ref(c.receiver), count});
            });
    }
    builder.FinalMethod("indexOf", "(Ljava/lang/String;)I", [](IntrinsicContext& context) {
        const auto haystack = Value(context, context.receiver);
        const auto found = haystack.find(Value(context, context.arguments[0].ref));
        return VmValue::Int(found == std::u16string::npos ? -1 : static_cast<std::int32_t>(found));
    });
    builder.FinalMethod("indexOf", "(Ljava/lang/String;I)I", [](IntrinsicContext& context) {
        const auto haystack = Value(context, context.receiver);
        const auto needle = Value(context, IntrinsicCall(context).NonNullRef(0, "string"));
        const auto start =
            std::min(haystack.size(), static_cast<std::size_t>(std::max(0, context.arguments[1].AsInt())));
        const auto found = haystack.find(needle, start);
        return VmValue::Int(found == std::u16string::npos ? -1 : static_cast<std::int32_t>(found));
    });
    builder.FinalMethod("lastIndexOf", "(I)I", [](IntrinsicContext& context) {
        const auto haystack = Value(context, context.receiver);
        const auto found = haystack.rfind(static_cast<char16_t>(context.arguments[0].cat1 & 0xffffU));
        return VmValue::Int(found == std::u16string::npos ? -1 : static_cast<std::int32_t>(found));
    });
    builder.FinalMethod("lastIndexOf", "(II)I", [](IntrinsicContext& context) {
        const auto haystack = Value(context, context.receiver);
        const auto from = context.arguments[1].AsInt();
        if (from < 0 || haystack.empty()) return VmValue::Int(-1);
        const auto start = std::min<std::size_t>(static_cast<std::size_t>(from), haystack.size() - 1U);
        const auto found = haystack.rfind(static_cast<char16_t>(context.arguments[0].cat1 & 0xffffU), start);
        return VmValue::Int(found == std::u16string::npos ? -1 : static_cast<std::int32_t>(found));
    });
    builder.FinalMethod("lastIndexOf", "(Ljava/lang/String;)I", [](IntrinsicContext& context) {
        const auto haystack = Value(context, context.receiver);
        const auto found = haystack.rfind(Value(context, context.arguments[0].ref));
        return VmValue::Int(found == std::u16string::npos ? -1 : static_cast<std::int32_t>(found));
    });
    builder.FinalMethod("substring", "(I)Ljava/lang/String;", [](IntrinsicContext& context) {
        const auto value = Value(context, context.receiver);
        return Substring(context, context.arguments[0].AsInt(), static_cast<std::int32_t>(value.size()));
    });
    builder.FinalMethod("substring", "(II)Ljava/lang/String;", [](IntrinsicContext& context) {
        return Substring(context, context.arguments[0].AsInt(), context.arguments[1].AsInt());
    });
    builder.FinalMethod("subSequence", "(II)Ljava/lang/CharSequence;", [](IntrinsicContext& context) {
        return InvokeGuest(context.vm, context.receiver, "substring", "(II)Ljava/lang/String;",
                           {context.arguments[0], context.arguments[1]});
    });
    builder.FinalMethod("toLowerCase", "()Ljava/lang/String;", [](IntrinsicContext& context) {
        const auto locale =
            InvokeGuestStatic(context.vm, "Ljava/util/Locale;", "getDefault", "()Ljava/util/Locale;").ref;
        return MapCase(context, false, locale);
    });
    builder.FinalMethod("toLowerCase", "(Ljava/util/Locale;)Ljava/lang/String;",
                        [](IntrinsicContext& context) { return MapCase(context, false, context.arguments[0].ref); });
    builder.FinalMethod("toUpperCase", "(Ljava/util/Locale;)Ljava/lang/String;",
                        [](IntrinsicContext& context) { return MapCase(context, true, context.arguments[0].ref); });
    builder.FinalMethod("toUpperCase", "()Ljava/lang/String;", [](IntrinsicContext& context) {
        const auto locale =
            InvokeGuestStatic(context.vm, "Ljava/util/Locale;", "getDefault", "()Ljava/util/Locale;").ref;
        return MapCase(context, true, locale);
    });
    builder.FinalMethod("trim", "()Ljava/lang/String;", [](IntrinsicContext& context) {
        const auto value = Value(context, context.receiver);
        std::size_t begin = 0;
        std::size_t end = value.size();
        while (begin < end && value[begin] <= u' ')
            ++begin;
        while (end > begin && value[end - 1] <= u' ')
            --end;
        return Make(context, value.substr(begin, end - begin));
    });
    builder.FinalMethod("isEmpty", "()Z", [](IntrinsicContext& context) {
        return VmValue::Int(Value(context, context.receiver).empty() ? 1 : 0);
    });
    builder.StaticMethod(
        "format", "(Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;", [](IntrinsicContext& context) {
            const auto locale =
                InvokeGuestStatic(context.vm, "Ljava/util/Locale;", "getDefault", "()Ljava/util/Locale;").ref;
            return FormatWithFormatter(context, locale, context.arguments[0].ref, context.arguments[1].ref);
        });
    builder.StaticMethod("format", "(Ljava/util/Locale;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;",
                         [](IntrinsicContext& context) {
                             return FormatWithFormatter(context, context.arguments[0].ref, context.arguments[1].ref,
                                                        context.arguments[2].ref);
                         });
    builder.StaticMethod("valueOf", "(I)Ljava/lang/String;", [](IntrinsicContext& context) {
        return Make(context, Widen(std::to_string(context.arguments[0].AsInt())));
    });
    builder.StaticMethod("valueOf", "(J)Ljava/lang/String;", [](IntrinsicContext& context) {
        return Make(context, Widen(std::to_string(context.arguments[0].AsLong())));
    });
    builder.StaticMethod("valueOf", "(F)Ljava/lang/String;", [](IntrinsicContext& context) {
        return FormatFloating(context.vm, context.arguments[0], false);
    });
    builder.StaticMethod("valueOf", "(D)Ljava/lang/String;", [](IntrinsicContext& context) {
        return FormatFloating(context.vm, context.arguments[0], true);
    });
    builder.StaticMethod("valueOf", "(Z)Ljava/lang/String;", [](IntrinsicContext& context) {
        return Make(context, context.arguments[0].AsInt() != 0 ? u"true" : std::u16string(u"false"));
    });
    builder.StaticMethod("valueOf", "([CII)Ljava/lang/String;", [](IntrinsicContext& c) {
        const auto array = RequireArray(c.arguments[0].ref);
        const auto start = c.arguments[1].AsInt(), count = c.arguments[2].AsInt();
        const auto length = c.vm.Model().ArrayLength(array);
        if (start < 0 || count < 0 || start > length || count > length - start)
            throw VmJavaThrow{"Ljava/lang/StringIndexOutOfBoundsException;", "char array region out of bounds"};
        return Make(c, CharsValue(c, array, start, count));
    });
    builder.StaticMethod("valueOf", "(C)Ljava/lang/String;", [](IntrinsicContext& context) {
        return Make(context, std::u16string(1, static_cast<char16_t>(context.arguments[0].cat1 & 0xffffU)));
    });
    builder.StaticMethod("valueOf", "(Ljava/lang/Object;)Ljava/lang/String;", [](IntrinsicContext& context) {
        const auto argument = context.arguments[0].ref;
        if (!argument.IsValid()) {
            return Make(context, std::u16string(u"null"));
        }
        return InvokeGuest(context.vm, argument, "toString", "()Ljava/lang/String;");
    });
    // AOSP 4.4 ROM line 494.
    builder.Constructor(
        "(Ljava/lang/StringBuffer;)V",
        [](IntrinsicContext& c) {
            const auto source = IntrinsicCall(c).NonNullRef(0, "buffer");
            const auto token = c.vm.CurrentContextToken();
            c.vm.Monitors().Enter(source, token);
            try {
                const auto chars = InvokeGuest(c.vm, source, "shareValue", "()[C").ref;
                const auto root = c.vm.ProtectReferences(std::array{chars});
                const auto length = InvokeGuest(c.vm, source, "length", "()I").AsInt();
                c.vm.Model().BindString(c.receiver, CharsValue(c, chars, 0, length));
            } catch (...) {
                c.vm.Monitors().Exit(source, token);
                throw;
            }
            c.vm.Monitors().Exit(source, token);
            return VmValue::Void();
        },
        kAccPublic);

    // AOSP 4.4 ROM line 540.
    builder.Constructor(
        "(Ljava/lang/StringBuilder;)V",
        [](IntrinsicContext& c) {
            const auto source = IntrinsicCall(c).NonNullRef(0, "stringBuilder");
            const auto length = InvokeGuest(c.vm, source, "length", "()I").AsInt();
            const auto chars = c.vm.Model().NewPrimitiveArray(c.vm.Linker().ResolveDescriptor("[C"),
                                                              JniPrimitiveKind::character, length);
            const auto root = c.vm.ProtectReferences(std::array{chars});
            static_cast<void>(
                InvokeGuest(c.vm, source, "getChars", "(II[CI)V",
                            {VmValue::Int(0), VmValue::Int(length), VmValue::Ref(chars), VmValue::Int(0)}));
            c.vm.Model().BindString(c.receiver, CharsValue(c, chars, 0, length));
            return VmValue::Void();
        },
        kAccPublic);

    // AOSP 4.4 ROM line 186.
    builder.Constructor(
        "([BIII)V",
        [](IntrinsicContext& c) {
            const auto array = IntrinsicCall(c).NonNullRef(0, "bytes");
            const auto start = c.arguments[2].AsInt(), count = c.arguments[3].AsInt();
            const auto length = c.vm.Model().ArrayLength(array);
            if (start < 0 || count < 0 || start > length || count > length - start)
                throw VmJavaThrow{"Ljava/lang/StringIndexOutOfBoundsException;", "byte range"};
            const auto bytes = c.vm.Model().ReadByteRegion(array, start, count);
            const auto high = static_cast<std::uint32_t>(c.arguments[1].AsInt()) << 8U;
            std::u16string text;
            text.reserve(static_cast<std::size_t>(count));
            for (const auto byte : bytes)
                text.push_back(static_cast<char16_t>(high + std::to_integer<std::uint8_t>(byte)));
            c.vm.Model().BindString(c.receiver, text);
            return VmValue::Void();
        },
        kAccPublic);

    // AOSP 4.4 ROM line 158.
    builder.Constructor(
        "([BI)V",
        [](IntrinsicContext& c) {
            const auto bytes = IntrinsicCall(c).NonNullRef(0, "bytes");
            const auto length = c.vm.Model().ArrayLength(bytes);
            const auto target = c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Ljava/lang/String;"),
                                                               "<init>", "([BIII)V");
            const auto result =
                c.vm.Call(*target, std::vector<VmValue>{VmValue::Ref(c.receiver), c.arguments[0], c.arguments[1],
                                                        VmValue::Int(0), VmValue::Int(length)});
            if (result.exception.IsValid())
                throw VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
            return result.value;
        },
        kAccPublic);

    // AOSP 4.4 ROM line 515.
    builder.Constructor(
        "([III)V",
        [](IntrinsicContext& c) {
            const auto points = IntrinsicCall(c).NonNullRef(0, "codePoints");
            const auto start = c.arguments[1].AsInt(), count = c.arguments[2].AsInt();
            const auto length = c.vm.Model().ArrayLength(points);
            if (start < 0 || count < 0 || start > length || count > length - start)
                throw VmJavaThrow{"Ljava/lang/StringIndexOutOfBoundsException;", "code point range"};
            if (count > INT32_MAX / 2) throw VmJavaThrow{"Ljava/lang/NegativeArraySizeException;", "code point output"};
            const auto chars = c.vm.Model().NewPrimitiveArray(c.vm.Linker().ResolveDescriptor("[C"),
                                                              JniPrimitiveKind::character, count * 2);
            const auto root = c.vm.ProtectReferences(std::array{chars});
            const auto target = c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Ljava/lang/Character;"),
                                                               "toChars", "(I[CI)I");
            int written = 0;
            for (int i = 0; i < count; ++i) {
                const auto result =
                    c.vm.Call(*target, std::array{VmValue::Int(static_cast<std::int32_t>(
                                                      c.vm.Model().GetPrimitiveElement(points, start + i))),
                                                  VmValue::Ref(chars), VmValue::Int(written)});
                if (result.exception.IsValid())
                    throw VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
                written += result.value.AsInt();
            }
            c.vm.Model().BindString(c.receiver, CharsValue(c, chars, 0, written));
            return VmValue::Void();
        },
        kAccPublic);

    // AOSP 4.4 ROM line 804.
    builder.VirtualMethod(
        "getBytes", "(II[BI)V",
        [](IntrinsicContext& c) {
            const auto text = Value(c, c.receiver);
            const auto start = c.arguments[0].AsInt(), end = c.arguments[1].AsInt();
            if (start < 0 || end < start || static_cast<std::size_t>(end) > text.size())
                throw VmJavaThrow{"Ljava/lang/StringIndexOutOfBoundsException;", "string range"};
            for (int i = start; i < end; ++i) {
                const auto dst = IntrinsicCall(c).NonNullRef(2, "dst");
                const auto index = static_cast<std::int64_t>(c.arguments[3].AsInt()) + i - start;
                if (index < 0 || index >= c.vm.Model().ArrayLength(dst))
                    throw VmJavaThrow{"Ljava/lang/StringIndexOutOfBoundsException;", "destination range"};
                c.vm.Model().SetPrimitiveElement(dst, static_cast<JniSize>(index),
                                                 static_cast<std::uint8_t>(text[static_cast<std::size_t>(i)]));
            }
            return VmValue::Void();
        },
        kAccPublic);

    // AOSP 4.4 ROM line 1175.
    builder.VirtualMethod(
        "lastIndexOf", "(Ljava/lang/String;I)I",
        [](IntrinsicContext& c) {
            const auto needle = Value(c, c.arguments[0].ref);
            const auto text = Value(c, c.receiver);
            auto from = c.arguments[1].AsInt();
            if (needle.size() > text.size() || from < 0) return VmValue::Int(-1);
            if (needle.empty()) return VmValue::Int(std::min(from, static_cast<int>(text.size())));
            from = std::min(from, static_cast<int>(text.size() - needle.size()));
            for (;;) {
                const auto index =
                    InvokeGuest(c.vm, c.receiver, "lastIndexOf", "(II)I", {VmValue::Int(needle[0]), VmValue::Int(from)})
                        .AsInt();
                if (index < 0) return VmValue::Int(-1);
                if (text.compare(static_cast<std::size_t>(index), needle.size(), needle) == 0)
                    return VmValue::Int(index);
                from = index - 1;
            }
        },
        kAccPublic);

    // AOSP 4.4 ROM line 1585.
    builder.StaticMethod(
        "valueOf", "([C)Ljava/lang/String;",
        [](IntrinsicContext& c) {
            const auto array = IntrinsicCall(c).NonNullRef(0, "chars");
            const auto length = c.vm.Model().ArrayLength(array);
            const auto object = c.vm.NewIntrinsicInstance("Ljava/lang/String;");
            const auto root = c.vm.ProtectReferences(std::array{object});
            const auto target = c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Ljava/lang/String;"),
                                                               "<init>", "([CII)V");
            const auto result = c.vm.Call(*target, std::vector<VmValue>{VmValue::Ref(object), c.arguments[0],
                                                                        VmValue::Int(0), VmValue::Int(length)});
            if (result.exception.IsValid())
                throw VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
            return VmValue::Ref(object);
        },
        kAccPublic);

    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {

namespace {
// Checked scratch for the API19 decimal natives. 5120 bits also cover the
// parser's ROM exponent range (-1024..1024) and binary subnormal alignment.
// Never stored on a Java object or exposed as a general big-number facility.
class DecimalMagnitude final {
    std::array<std::uint32_t, 160> words_{};

public:
    DecimalMagnitude(std::uint64_t value = 0) {
        words_[0] = static_cast<std::uint32_t>(value);
        words_[1] = static_cast<std::uint32_t>(value >> 32);
    }

    int BitLength() const {
        for (std::size_t i = words_.size(); i-- > 0;)
            if (words_[i]) return static_cast<int>(32 * static_cast<int>(i) + 32 - std::countl_zero(words_[i]));
        return 0;
    }

    int TrailingZeros() const {
        for (std::size_t i = 0; i < words_.size(); ++i)
            if (words_[i]) return static_cast<int>(32 * static_cast<int>(i) + std::countr_zero(words_[i]));
        return 0;
    }

    bool Bit(int index) const { return (words_.at(static_cast<std::size_t>(index / 32)) >> (index % 32)) & 1U; }

    void MultiplyWide(std::uint64_t factor) {
        auto high = *this;
        high.Multiply(static_cast<std::uint32_t>(factor >> 32));
        Multiply(static_cast<std::uint32_t>(factor));
        *this = Plus(high.Shift(32));
    }

    void TimesTen(int exponent) {
        for (int i = 0; i < exponent; ++i)
            Multiply(10);
    }

    // cbigint toDoubleHighPrecision: integer conversion, ties to even.
    double ToDouble() const {
        const int width = BitLength();
        if (width == 0) return 0.0;
        if (width > 1024) return std::numeric_limits<double>::infinity();
        const int shift = std::max(0, width - 53);
        std::uint64_t mantissa = 0;
        for (int i = width; i-- > shift;)
            mantissa = (mantissa << 1) | Bit(i);
        if (shift == 0) return static_cast<double>(mantissa);
        auto bits = (std::uint64_t(width - 1 + 1023) << 52) | (mantissa & 0xfffffffffffffULL);
        if (Bit(shift - 1)) {
            bool sticky = false;
            for (int i = 0; i < shift - 1; ++i)
                sticky |= Bit(i);
            if (sticky || (mantissa & 1)) ++bits;
        }
        return std::bit_cast<double>(bits);
    }

    void Multiply(unsigned factor) {
        std::uint64_t carry = 0;
        for (auto& word : words_) {
            const auto product = std::uint64_t(word) * factor + carry;
            word = static_cast<std::uint32_t>(product);
            carry = product >> 32;
        }
        if (carry) throw DexVmError(DexVmErrorReason::internal_invariant, "decimal scratch overflow");
    }

    DecimalMagnitude Shift(int bits) const {
        auto result = *this;
        for (int i = 0; i < bits; ++i)
            result.Multiply(2);
        return result;
    }

    int Compare(const DecimalMagnitude& other) const {
        for (std::size_t i = words_.size(); i-- > 0;)
            if (words_[i] != other.words_[i]) return words_[i] < other.words_[i] ? -1 : 1;
        return 0;
    }

    DecimalMagnitude Plus(const DecimalMagnitude& other) const {
        auto result = *this;
        std::uint64_t carry = 0;
        for (std::size_t i = 0; i < words_.size(); ++i) {
            const auto sum = std::uint64_t(words_[i]) + other.words_[i] + carry;
            result.words_[i] = static_cast<std::uint32_t>(sum);
            carry = sum >> 32;
        }
        if (carry) throw DexVmError(DexVmErrorReason::internal_invariant, "decimal scratch overflow");
        return result;
    }

    void Subtract(const DecimalMagnitude& other) {
        std::uint64_t borrow = 0;
        for (std::size_t i = 0; i < words_.size(); ++i) {
            const auto sub = std::uint64_t(other.words_[i]) + borrow;
            borrow = std::uint64_t(words_[i]) < sub;
            words_[i] = static_cast<std::uint32_t>(std::uint64_t(words_[i]) - sub);
        }
        if (borrow) throw DexVmError(DexVmErrorReason::internal_invariant, "decimal scratch underflow");
    }
};

// Apache-2.0 AOSP libcore java_lang_StringToReal.cpp, API 19.
// createDouble/createDouble1/doubleAlgorithm and their float counterparts:
// preserve the prefix limits, initial approximation, Clinger integer correction,
// ties and oscillation rule. Checked value scratch replaces cbigint buffers.
constexpr std::array double_tens = {1.0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
constexpr std::array<std::uint32_t, 11> float_tens = {0x3f800000, 0x41200000, 0x42c80000, 0x447a0000,
                                                      0x461c4000, 0x47c35000, 0x49742400, 0x4b189680,
                                                      0x4cbebc20, 0x4e6e6b28, 0x501502f9};

template <typename Float>
using RealBits = std::conditional_t<sizeof(Float) == 8, std::uint64_t, std::uint32_t>;

template <typename Float>
Float CorrectReal(const DecimalMagnitude& f, int e, Float z) {
    using Bits = RealBits<Float>;
    constexpr int precision = sizeof(Float) == 8 ? 52 : 23;
    constexpr int offset = sizeof(Float) == 8 ? 1075 : 150;
    constexpr Bits normal = Bits{1} << precision;
    constexpr Bits infinity = sizeof(Float) == 8 ? Bits(0x7ff0000000000000ULL) : Bits(0x7f800000);
    int decrements = 0, increments = 0;
    auto bits = std::bit_cast<Bits>(z);
    for (;;) {
        const auto exponent = static_cast<int>(bits >> precision);
        const std::uint64_t m = (bits & (normal - 1)) | (exponent ? normal : Bits{0});
        const int k = (exponent ? exponent : 1) - offset;
        auto x = f;
        DecimalMagnitude y(m);
        if (e >= 0)
            x.TimesTen(e);
        else
            y.TimesTen(-e);
        if (k >= 0)
            y = y.Shift(k);
        else
            x = x.Shift(-k);
        const int comparison = x.Compare(y);
        auto delta = comparison > 0 ? x : y;
        delta.Subtract(comparison > 0 ? y : x);
        delta.MultiplyWide(2 * m);
        const int comparison2 = delta.Compare(y);
        bool decrement = comparison < 0;
        bool last = false;
        if (comparison2 < 0) {
            if (!decrement || m != normal) break;
            if (delta.Shift(1).Compare(y) <= 0) break;
        } else if (comparison2 == 0) {
            if ((m & 1) == 0) {
                if (!decrement || m != normal) break;
            } else {
                last = true;
            }
        } else if (!decrement && bits == infinity) {
            break;
        }
        if (decrement) {
            --bits;
            ++decrements;
        } else {
            ++bits;
            ++increments;
        }
        if (increments > 2 && decrements > 2) {
            if (decrements > increments)
                bits += static_cast<Bits>(decrements - increments);
            else if (increments > decrements)
                bits -= static_cast<Bits>(increments - decrements);
            break;
        }
        if (last) break;
    }
    return std::bit_cast<Float>(bits);
}

double CreateDouble1(const DecimalMagnitude& f, int e) {
    const int num_bits = f.BitLength() - f.TrailingZeros();
    const auto value = f.ToDouble();
    if (num_bits < 54 && e >= 0 && e < 23) return value * double_tens[static_cast<std::size_t>(e)];
    if (num_bits < 54 && e < 0 && e > -23) return value / double_tens[static_cast<std::size_t>(-e)];
    double result;
    if (e >= 0 && e < 309)
        result = value * std::pow(10.0, e);
    else if (e >= 309) {
        if (value != 0.0) return std::numeric_limits<double>::infinity();
        result = std::bit_cast<double>(std::uint64_t{1});
    } else if (e > -309)
        result = value / std::pow(10.0, -e);
    else {
        result = value * std::pow(10.0, e + 52);
        result = result * std::pow(10.0, -52);
    }
    if (result == 0.0) result = std::bit_cast<double>(std::uint64_t{1});
    return CorrectReal(f, e, result);
}

float CreateFloat1(const DecimalMagnitude& f, int e) {
    const int num_bits = f.BitLength() + 1;
    const auto value = f.ToDouble();
    if (num_bits < 25 && e >= 0 && e < 11)
        return static_cast<float>(value) * std::bit_cast<float>(float_tens[static_cast<std::size_t>(e)]);
    if (num_bits < 25 && e < 0 && e > -11)
        return static_cast<float>(value) / std::bit_cast<float>(float_tens[static_cast<std::size_t>(-e)]);
    float result = 0.0F;
    if (e >= 0 && e < 39)
        result = static_cast<float>(value * std::pow(10.0, e));
    else if (e >= 39) {
        result = static_cast<float>(value);
        result = result == 0.0F ? std::bit_cast<float>(std::uint32_t{1}) : std::numeric_limits<float>::infinity();
    } else if (e > -309) {
        const double approximation = value / std::pow(10.0, -e);
        const auto bits = std::bit_cast<std::uint64_t>(approximation);
        const auto exponent = static_cast<int>(bits >> 52);
        if (exponent == 0) return 0.0F;
        int dexp = exponent - 1075 + 51;
        const auto dmant = (bits & 0xfffffffffffffULL) | 0x10000000000000ULL;
        if (dexp <= -155) return 0.0F;
        if (dexp <= -127) {
            auto fmant = static_cast<std::uint32_t>(dmant >> 29);
            auto fovfl = static_cast<std::uint32_t>(dmant & 0x1fffffff) << 3;
            while (dexp < -127 && (fmant | fovfl) != 0) {
                if (fmant & 1) fovfl |= 0x80000000;
                fovfl >>= 1;
                fmant >>= 1;
                ++dexp;
            }
            if (fovfl & 0x80000000) {
                if ((fovfl & 0x7ffffffc) != 0 || (fmant & 1)) ++fmant;
            } else if ((fovfl & 0x40000000) && (fovfl & 0x3ffffffc))
                ++fmant;
            result = std::bit_cast<float>(fmant);
        } else
            result = static_cast<float>(approximation);
    }
    if (e <= -309 || std::bit_cast<std::uint32_t>(result) == 0) result = std::bit_cast<float>(std::uint32_t{1});
    return CorrectReal(f, e, result);
}

template <typename Float>
Float CreateReal(IntrinsicContext& context) {
    using Bits = RealBits<Float>;
    const auto string = context.arguments[0].ref;
    if (!string.IsValid()) return Float{0};
    const auto text = context.vm.StringUtf8(string);
    // ScopedUtfChars uses modified UTF-8: an embedded Java NUL is a
    // non-digit, not a C terminator. Validate the full host string likewise.
    const std::string_view digits(text);
    int e = context.arguments[1].AsInt();
    DecimalMagnitude f;
    constexpr int limit = sizeof(Float) == 8 ? 1024 : 448;
    std::size_t cursor = 0;
    for (; cursor < digits.size(); ++cursor) {
        const char digit = digits[cursor];
        if (digit < '0' || digit > '9') return std::bit_cast<Float>(~Bits{0});
        auto next = f;
        next.Multiply(10);
        next = next.Plus(DecimalMagnitude(static_cast<unsigned>(digit - '0')));
        if (next.BitLength() > limit) break;
        f = next;
    }
    if (digits.empty()) return std::bit_cast<Float>(~Bits{0});
    const auto remaining = digits.size() - cursor;
    e = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(e) + static_cast<std::uint32_t>(remaining));
    if (remaining && e > 0) return std::numeric_limits<Float>::infinity();
    if constexpr (sizeof(Float) == 8) {
        if (e == 0) return f.ToDouble();
        return CreateDouble1(f, e);
    } else
        return CreateFloat1(f, e);
}

}  // namespace

IntrinsicClassDecl Declare_java_lang_StringToReal() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/StringToReal;");
    builder.MergesBootClass();
    builder.StaticMethod(
        "parseFltImpl", "(Ljava/lang/String;I)F",
        [](IntrinsicContext& context) { return VmValue::Float(CreateReal<float>(context)); },
        kAccPrivate | kAccStatic | kAccNative);
    builder.StaticMethod(
        "parseDblImpl", "(Ljava/lang/String;I)D",
        [](IntrinsicContext& context) { return VmValue::Double(CreateReal<double>(context)); },
        kAccPrivate | kAccStatic | kAccNative);
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_java_lang_RealToString() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/RealToString;", "Ljava/lang/Object;", {}, kAccFinal);
    builder.MergesBootClass();
    const auto digits = builder.BoundInstanceField("digits", "[I", kAccPrivate | kAccFinal);
    const auto count = builder.BoundInstanceField("digitCount", "I", kAccPrivate);
    const auto first = builder.BoundInstanceField("firstK", "I", kAccPrivate);
    builder.DirectMethod(
        "bigIntDigitGenerator", "(JIZI)V",
        [digits, count, first](IntrinsicContext& context) {
            // Apache-2.0 AOSP libcore java_lang_RealToString.cpp, API 19:
            // the same Burger/Dybvig intervals and rounding, using checked
            // multiprecision values instead of cbigint limb scratch buffers.
            // Only this original native digit generator lives on the host;
            // presentation and all append operations remain Java bytecode.
            const IntrinsicCall c(context);
            const auto f = c.Long(0);
            const auto e = c.Int(1), p = c.Int(3);
            if (f <= 0 || f > (std::int64_t{1} << 53) || e < -1074 || e > 1023 || p < 1 || p > 53)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid floating digit input"};
            DecimalMagnitude r, s, plus, minus;
            const bool boundary = static_cast<std::uint64_t>(f) == (std::uint64_t{1} << p);
            if (e >= 0) {
                minus = DecimalMagnitude(1).Shift(e);
                if (!boundary) {
                    r = DecimalMagnitude(static_cast<std::uint64_t>(f)).Shift(e + 1);
                    s = 2;
                    plus = minus;
                } else {
                    r = DecimalMagnitude(static_cast<std::uint64_t>(f)).Shift(e + 2);
                    s = 4;
                    plus = DecimalMagnitude(1).Shift(e + 1);
                }
            } else if (c.Int(2) != 0 || !boundary) {
                r = DecimalMagnitude(static_cast<std::uint64_t>(f)).Shift(1);
                s = DecimalMagnitude(1).Shift(1 - e);
                plus = minus = 1;
            } else {
                r = DecimalMagnitude(static_cast<std::uint64_t>(f)).Shift(2);
                s = DecimalMagnitude(1).Shift(2 - e);
                plus = 2;
                minus = 1;
            }
            const auto k = static_cast<int>(std::ceil((e + p - 1) * 0.30102999566398114 - 1e-10));
            for (int i = 0; i < std::abs(k); ++i) {
                if (k > 0)
                    s.Multiply(10);
                else {
                    r.Multiply(10);
                    plus.Multiply(10);
                    minus.Multiply(10);
                }
            }
            int first_k = k;
            if (r.Plus(plus).Compare(s) < 0) {
                first_k = k - 1;
                r.Multiply(10);
                plus.Multiply(10);
                minus.Multiply(10);
            }
            const auto array = c.GetRef(digits);
            const auto capacity = context.vm.Model().ArrayLength(array);
            if (context.vm.Model().PrimitiveArrayKind(array) != JniPrimitiveKind::integer)
                throw DexVmError(DexVmErrorReason::internal_invariant, "digit array is not int[]");
            std::vector<int> generated;
            for (;;) {
                int digit = 0;
                for (int bit = 3; bit >= 0; --bit) {
                    const auto shifted = s.Shift(bit);
                    if (r.Compare(shifted) >= 0) {
                        r.Subtract(shifted);
                        digit += 1 << bit;
                    }
                }
                const bool low = r.Compare(minus) <= 0, high = r.Plus(plus).Compare(s) >= 0;
                if (static_cast<JniSize>(generated.size()) >= capacity)
                    throw DexVmError(DexVmErrorReason::internal_invariant, "digit array exhausted");
                if (low || high) {
                    if ((high && !low) || (high && low && r.Shift(1).Compare(s) >= 0)) ++digit;
                    generated.push_back(digit);
                    break;
                }
                generated.push_back(digit);
                r.Multiply(10);
                plus.Multiply(10);
                minus.Multiply(10);
            }
            for (std::size_t i = 0; i < generated.size(); ++i)
                context.vm.Model().SetPrimitiveElement(array, static_cast<JniSize>(i),
                                                       static_cast<std::uint64_t>(generated[i]));
            c.SetInt(count, static_cast<std::int32_t>(generated.size()));
            c.SetInt(first, first_k);
            return VmValue::Void();
        },
        kAccPrivate | kAccNative);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_System.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

namespace {

[[nodiscard]] std::string PropertyKey(IntrinsicContext& context, const VmObjectRef reference) {
    if (!reference.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "system property key is null"};
    }
    auto key = context.vm.StringUtf8(reference);
    if (key.empty()) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "system property key is empty"};
    }
    return key;
}

[[nodiscard]] std::string PropertyValue(IntrinsicContext& context, const VmObjectRef reference) {
    if (!reference.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "system property value is null"};
    }
    return context.vm.StringUtf8(reference);
}

void PropagateEnvironmentCall(Interpreter& vm, const VmCallOutcome& outcome) {
    if (!outcome.exception.IsValid()) return;
    throw VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                      outcome.exception};
}

[[nodiscard]] VmObjectRef NewEnvironmentMap(Interpreter& vm,
                                            const std::vector<std::pair<std::string, std::string>>& entries) {
    const auto map_class = vm.Linker().ResolveDescriptor("Ljava/util/HashMap;");
    PropagateEnvironmentCall(vm, vm.EnsureClassInitialized(map_class));
    const auto map = vm.NewIntrinsicInstance("Ljava/util/HashMap;");
    const auto map_root = vm.ProtectReferences(std::array{map});
    const auto constructor = vm.Linker().FindDirectMethod(map_class, "<init>", "()V");
    if (!constructor) {
        throw DexVmError(DexVmErrorReason::internal_invariant, "HashMap constructor is not linked");
    }
    const std::vector<VmValue> map_constructor_arguments{VmValue::Ref(map)};
    PropagateEnvironmentCall(vm, vm.Call(*constructor, map_constructor_arguments));
    for (const auto& [name, value] : entries) {
        const auto key = vm.NewStringUtf8(name);
        const auto value_ref = vm.NewStringUtf8(value);
        const auto item_roots = vm.ProtectReferences(std::array{map, key, value_ref});
        static_cast<void>(InvokeGuest(vm, map, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                                      {VmValue::Ref(key), VmValue::Ref(value_ref)}));
    }

    const auto environment_class = vm.Linker().ResolveDescriptor("Ljava/lang/System$SystemEnvironment;");
    PropagateEnvironmentCall(vm, vm.EnsureClassInitialized(environment_class));
    const auto environment = vm.NewIntrinsicInstance("Ljava/lang/System$SystemEnvironment;");
    const auto environment_roots = vm.ProtectReferences(std::array{map, environment});
    const auto environment_constructor =
        vm.Linker().FindDirectMethod(environment_class, "<init>", "(Ljava/util/Map;)V");
    if (!environment_constructor) {
        throw DexVmError(DexVmErrorReason::internal_invariant, "SystemEnvironment constructor is not linked");
    }
    const std::vector<VmValue> environment_constructor_arguments{VmValue::Ref(environment), VmValue::Ref(map)};
    PropagateEnvironmentCall(vm, vm.Call(*environment_constructor, environment_constructor_arguments));
    return environment;
}

}  // namespace

IntrinsicClassDecl Declare_java_lang_System(const CoreIntrinsicServices& services) {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/System;", "Ljava/lang/Object;");
    builder.StaticField("out", "Ljava/io/PrintStream;");
    builder.StaticField("err", "Ljava/io/PrintStream;");
    const auto line_separator =
        builder.BoundStaticField("lineSeparator", "Ljava/lang/String;", kAccPrivate | kAccStatic | kAccFinal);
    builder.StaticMethod("arraycopy", "(Ljava/lang/Object;ILjava/lang/Object;II)V", [](IntrinsicContext& context) {
        auto& model = context.vm.Model();
        const auto source = context.arguments[0].ref;
        const auto source_pos = context.arguments[1].AsInt();
        const auto target = context.arguments[2].ref;
        const auto target_pos = context.arguments[3].AsInt();
        const auto length = context.arguments[4].AsInt();
        if (!source.IsValid() || !target.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/NullPointerException;", "arraycopy on null array"};
        }
        const auto source_kind = model.Kind(source);
        const auto target_kind = model.Kind(target);
        const auto is_array = [](VmObjectKind kind) {
            return kind == VmObjectKind::object_array || kind == VmObjectKind::primitive_array;
        };
        if (!is_array(source_kind) || !is_array(target_kind)) {
            throw VmJavaThrow{"Ljava/lang/ArrayStoreException;", "arraycopy requires arrays"};
        }
        if (length < 0 || source_pos < 0 || target_pos < 0 || source_pos > model.ArrayLength(source) - length ||
            target_pos > model.ArrayLength(target) - length) {
            throw VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "arraycopy range is out of bounds"};
        }
        if (source_kind == VmObjectKind::object_array && target_kind == VmObjectKind::object_array) {
            if (source == target) {
                model.CopyObjectElementsWithin(source, source_pos, target_pos, length);
                return VmValue::Void();
            }
            // Overlap-safe copy: buffer the source range first.
            std::vector<VmObjectRef> staged;
            staged.reserve(static_cast<std::size_t>(length));
            for (std::int32_t index = 0; index < length; ++index) {
                staged.push_back(model.GetObjectElement(source, source_pos + index));
            }
            const auto element_class = model.ObjectArrayElementClass(target);
            for (std::int32_t index = 0; index < length; ++index) {
                const auto value = staged[static_cast<std::size_t>(index)];
                if (value.IsValid() && !context.vm.Linker().IsAssignable(element_class, model.ObjectClass(value))) {
                    throw VmJavaThrow{"Ljava/lang/ArrayStoreException;", "arraycopy element is incompatible"};
                }
                model.SetObjectElement(target, target_pos + index, value);
            }
            return VmValue::Void();
        }
        if (source_kind == VmObjectKind::primitive_array && target_kind == VmObjectKind::primitive_array &&
            model.PrimitiveArrayKind(source) == model.PrimitiveArrayKind(target)) {
            model.CopyPrimitiveElements(source, source_pos, target, target_pos, length);
            return VmValue::Void();
        }
        throw VmJavaThrow{"Ljava/lang/ArrayStoreException;", "arraycopy element types are incompatible"};
    });
    builder.StaticMethod("runFinalization", "()V", [](IntrinsicContext& c) {
        c.vm.RunFinalization();
        return VmValue::Void();
    });
    builder.StaticMethod("gc", "()V", [](IntrinsicContext& context) {
        // Dalvik Runtime.gc requests an ordinary collection, preserving
        // soft references; reuse the VM's existing STW collector.
        static_cast<void>(context.vm.CollectGarbage("System.gc"));
        return VmValue::Void();
    });
    builder.StaticMethod("identityHashCode", "(Ljava/lang/Object;)I", [](IntrinsicContext& context) {
        return VmValue::Int(context.vm.Model().IdentityHashCode(context.arguments[0].ref));
    });
    const auto system_log = [](const core::LogLevel level) {
        return [level](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto message = call.Vm().StringUtf8(call.NonNullRef(0, "message"));
            if (auto* logger = call.Vm().Log(); logger != nullptr) {
                logger->Write(level, "guest.java.lang.System", message);
            }
            return VmValue::Void();
        };
    };
    for (const auto descriptor : {"(Ljava/lang/String;)V", "(Ljava/lang/String;Ljava/lang/Throwable;)V"}) {
        builder.StaticMethod("logI", descriptor, system_log(core::LogLevel::info));
        builder.StaticMethod("logW", descriptor, system_log(core::LogLevel::warn));
        builder.StaticMethod("logE", descriptor, system_log(core::LogLevel::error));
    }
    // API 19 libcore has no installed SecurityManager. This is a source-
    // compatibility query, not a permission facade or host security hook.
    builder.StaticMethod("getSecurityManager", "()Ljava/lang/SecurityManager;",
                         [](IntrinsicContext&) { return VmValue::Ref(VmObjectRef{}); });
    builder.StaticMethod("getProperties", "()Ljava/util/Properties;",
                         [](IntrinsicContext& call) { return VmValue::Ref(call.vm.SystemProperties()); });
    builder.StaticMethod("getProperty", "(Ljava/lang/String;)Ljava/lang/String;", [](IntrinsicContext& context) {
        const auto key = PropertyKey(context, context.arguments[0].ref);
        const auto value = context.vm.GetSystemProperty(key);
        if (!value.has_value()) {
            return VmValue::Ref(VmObjectRef{});
        }
        return VmValue::Ref(context.vm.NewStringUtf8(*value));
    });
    builder.StaticMethod(
        "getProperty", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", [](IntrinsicContext& context) {
            const auto key = PropertyKey(context, context.arguments[0].ref);
            const auto value = context.vm.GetSystemProperty(key);
            return VmValue::Ref(value.has_value() ? context.vm.NewStringUtf8(*value) : context.arguments[1].ref);
        });
    builder.StaticMethod("lineSeparator", "()Ljava/lang/String;", [line_separator](IntrinsicContext& context) {
        return VmValue::Ref(IntrinsicCall(context).GetRef(line_separator));
    });
    builder.StaticMethod("getenv", "(Ljava/lang/String;)Ljava/lang/String;",
                         [lookup = services.environment_value](IntrinsicContext& context) {
                             const auto name_ref = context.arguments[0].ref;
                             if (!name_ref.IsValid()) {
                                 throw VmJavaThrow{"Ljava/lang/NullPointerException;", "name == null"};
                             }
                             if (!lookup) {
                                 throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                                   "System.getenv needs an attached guest process environment"};
                             }
                             const auto value = lookup(context.vm.StringUtf8(name_ref));
                             return value.has_value() ? VmValue::Ref(context.vm.NewStringUtf8(*value))
                                                      : VmValue::Ref(VmObjectRef{});
                         });
    builder.StaticMethod("getenv", "()Ljava/util/Map;",
                         [snapshot = services.environment_entries](IntrinsicContext& context) {
                             if (!snapshot) {
                                 throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                                   "System.getenv needs an attached guest process environment"};
                             }
                             return VmValue::Ref(NewEnvironmentMap(context.vm, snapshot()));
                         });
    builder.StaticMethod("setProperty", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                         [](IntrinsicContext& context) {
                             const auto key = PropertyKey(context, context.arguments[0].ref);
                             const auto value = PropertyValue(context, context.arguments[1].ref);
                             const auto previous = context.vm.SetSystemProperty(key, value);
                             return previous.has_value() ? VmValue::Ref(context.vm.NewStringUtf8(*previous))
                                                         : VmValue::Ref(VmObjectRef{});
                         });
    builder.StaticMethod("clearProperty", "(Ljava/lang/String;)Ljava/lang/String;", [](IntrinsicContext& context) {
        const auto key = context.arguments[0];
        static_cast<void>(PropertyKey(context, key.ref));
        const auto removed = InvokeGuest(context.vm, context.vm.SystemProperties(), "remove",
                                         "(Ljava/lang/Object;)Ljava/lang/Object;", {key});
        if (removed.ref.IsValid() &&
            context.vm.Model().ObjectClass(removed.ref) != context.vm.Linker().ResolveDescriptor("Ljava/lang/String;"))
            throw VmJavaThrow{"Ljava/lang/ClassCastException;", "property value is not a String"};
        return removed;
    });
    builder.StaticMethod("mapLibraryName", "(Ljava/lang/String;)Ljava/lang/String;", [](IntrinsicContext& call) {
        const auto name = IntrinsicCall(call).NonNullRef(0, "libName");
        return VmValue::Ref(call.vm.NewStringUtf8("lib" + call.vm.StringUtf8(name) + ".so"));
    });
    builder.StaticMethod("currentTimeMillis", "()J", [now = services.current_time_millis](IntrinsicContext&) {
        if (!now) {
            throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "System.currentTimeMillis needs an injected Clock"};
        }
        return VmValue::Long(now());
    });
    builder.StaticMethod("nanoTime", "()J", [](IntrinsicContext& context) {
        const auto now = context.vm.Monitors().TimeSource();
        if (!now)
            throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "System.nanoTime requires the unified Clock"};
        return VmValue::Long(std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(now()) * 1000000ULL));
    });
    builder.UnimplementedStatic("load", "(Ljava/lang/String;)V");
    builder.StaticMethod("loadLibrary", "(Ljava/lang/String;)V", [](IntrinsicContext& call) {
        const auto reference = call.arguments[0].ref;
        if (!reference.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/NullPointerException;", "libraryName == null"};
        }
        const auto name = call.vm.StringUtf8(reference);
        if (name == "soundpool" || name == "media_jni") {
            return VmValue::Void();
        }
        throw VmJavaThrow{"Ljava/lang/UnsatisfiedLinkError;", "APK native library loader is unavailable"};
    });
    builder.StaticMethod("exit", "(I)V", [](IntrinsicContext& context) {
        const auto runtime =
            InvokeGuestDirect(context.vm, "Ljava/lang/Runtime;", "getRuntime", "()Ljava/lang/Runtime;", {}).ref;
        return InvokeGuest(context.vm, runtime, "exit", "(I)V", {context.arguments[0]});
    });
    builder.ClassInitializer(
        [line_separator, initialize = services.initialize_system_properties](IntrinsicContext& context) {
            auto& vm = context.vm;
            if (initialize) initialize(vm);
            static_cast<void>(vm.SystemProperties());
            vm.SetIntrinsicStaticRef("Ljava/lang/System;", "out", "Ljava/io/PrintStream;",
                                     InvokeGuestDirect(vm, "Lorg/ogplay/io/LogOutputStream;", "createPrintStream",
                                                       "()Ljava/io/PrintStream;", {})
                                         .ref);
            vm.SetIntrinsicStaticRef("Ljava/lang/System;", "err", "Ljava/io/PrintStream;",
                                     InvokeGuestDirect(vm, "Lorg/ogplay/io/LogOutputStream;", "createErrorPrintStream",
                                                       "()Ljava/io/PrintStream;", {})
                                         .ref);
            const auto key = vm.NewStringUtf8("line.separator");
            const auto roots = vm.ProtectReferences(std::array{key});
            const auto value = InvokeGuestDirect(vm, "Ljava/lang/System;", "getProperty",
                                                 "(Ljava/lang/String;)Ljava/lang/String;", {VmValue::Ref(key)})
                                   .ref;
            IntrinsicCall(context).SetRef(line_separator, value);
            return VmValue::Void();
        });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_Thread.cpp ----
// java.lang.Thread core facade for the pinned Android 4.4.4 libdvm class.
// Java-visible fields and validation stay here; lifecycle, parking,
// interrupt state and execution-context identity stay in VmThreadRuntime.

#include "catalog.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/class_name_codec.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/dexvm/vm_threads.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {
struct ThreadFields final {
    IntrinsicFieldHandle target;
    IntrinsicFieldHandle name;
    IntrinsicFieldHandle priority;
    IntrinsicFieldHandle daemon;
    IntrinsicFieldHandle stack_size;
    IntrinsicFieldHandle id;
    IntrinsicFieldHandle has_been_started;
    IntrinsicFieldHandle context_class_loader;
    IntrinsicFieldHandle group;
    IntrinsicFieldHandle park_state;
    IntrinsicFieldHandle interrupt_actions;
    IntrinsicFieldHandle uncaught_handler;
    IntrinsicFieldHandle default_uncaught_handler;
};

void PropagateOutcome(Interpreter& vm, const VmCallOutcome& outcome);

VmValue InvokeThreadVirtual(Interpreter& vm, VmObjectRef receiver, const char* method, const char* signature,
                            std::vector<VmValue> args = {}) {
    if (!receiver.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "null thread or group receiver"};
    }
    const auto type = vm.Model().ObjectClass(receiver);
    const auto slot = vm.Linker().FindVtableIndex(type, method, signature);
    args.insert(args.begin(), VmValue::Ref(receiver));
    const auto result = vm.Call(vm.Linker().Class(type).vtable.at(*slot), args);
    if (result.exception.IsValid()) {
        throw VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                          result.exception};
    }
    return result.value;
}

[[nodiscard]] VmObjectRef StackTraceArray(Interpreter& vm, const std::uint64_t thread_id) {
    const auto snapshots = vm.StackSnapshot();
    const auto found =
        std::ranges::find_if(snapshots, [thread_id](const auto& stack) { return stack.guest_thread_id == thread_id; });
    const auto count = found == snapshots.end() ? 0U : found->frames.size();
    const auto array = vm.Model().NewObjectArray(vm.Linker().ResolveDescriptor("[Ljava/lang/StackTraceElement;"),
                                                 vm.Linker().ResolveDescriptor("Ljava/lang/StackTraceElement;"),
                                                 static_cast<JniSize>(count));
    const std::array array_references{array};
    [[maybe_unused]] const auto array_roots = vm.ProtectReferences(array_references);
    if (found == snapshots.end()) return array;
    const auto constructor =
        vm.Linker().FindDirectMethod(*vm.Linker().FindClass("Ljava/lang/StackTraceElement;"), "<init>",
                                     "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V");
    for (std::size_t index = 0; index < count; ++index) {
        const auto& frame = found->frames[count - index - 1U];
        const auto element = vm.NewIntrinsicInstance("Ljava/lang/StackTraceElement;");
        const std::vector<VmValue> arguments{
            VmValue::Ref(element), VmValue::Ref(vm.NewStringUtf8(ClassNameCodec::ClassGetName(frame.class_descriptor))),
            VmValue::Ref(vm.NewStringUtf8(frame.method_name)), VmValue::Ref(VmObjectRef{}), VmValue::Int(-1)};
        PropagateOutcome(vm, vm.Call(*constructor, arguments));
        vm.Model().SetObjectElement(array, static_cast<JniSize>(index), element);
    }
    return array;
}

[[nodiscard]] VmObjectRef ThreadState(Interpreter& vm, const std::string_view name) {
    const auto java_class = *vm.Linker().FindClass("Ljava/lang/Thread$State;");
    PropagateOutcome(vm, vm.EnsureClassInitialized(java_class));
    const auto field = vm.Linker().FindFieldRecursive(java_class, std::string(name), "Ljava/lang/Thread$State;");
    const auto& linked = vm.Linker().Field(*field);
    return VmObjectRef{vm.Linker().Class(linked.owner).static_storage[linked.slot]};
}

void InitializeFields(const IntrinsicCall& call, const ThreadFields& fields, const VmObjectRef object,
                      const VmObjectRef target, const VmObjectRef name, const std::int64_t id,
                      const std::int32_t priority, const VmObjectRef context_class_loader, const VmObjectRef group,
                      const std::int64_t stack_size = 0) {
    call.SetRef(fields.target, object, target);
    call.SetRef(fields.name, object, name);
    call.SetInt(fields.priority, object, priority);
    call.SetInt(fields.daemon, object, 0);
    call.SetLong(fields.stack_size, object, stack_size);
    call.SetLong(fields.id, object, id);
    call.SetInt(fields.has_been_started, object, 0);
    call.SetRef(fields.context_class_loader, object, context_class_loader);
    call.SetRef(fields.group, object, group);
    call.SetInt(fields.park_state, object, 1);
    auto& vm = call.Vm();
    const auto actions = vm.NewIntrinsicInstance("Ljava/util/ArrayList;");
    call.SetRef(fields.interrupt_actions, object, actions);
    const auto constructor = vm.Linker().FindDirectMethod(vm.Model().ObjectClass(actions), "<init>", "()V");
    if (!constructor) {
        throw DexVmError(DexVmErrorReason::internal_invariant, "BootDex ArrayList constructor is unavailable");
    }
    const VmValue arguments[] = {VmValue::Ref(actions)};
    PropagateOutcome(vm, vm.Call(*constructor, arguments));
    call.SetRef(fields.uncaught_handler, object, VmObjectRef{});
}

[[nodiscard]] VmObjectRef EnsureCurrentThread(const IntrinsicCall& call, const ThreadFields& fields) {
    auto& vm = call.Vm();
    auto& runtime = vm.Threads();
    if (const auto current = runtime.CurrentThreadObject(); current.IsValid()) {
        return current;
    }
    const auto token = vm.CurrentContextToken();
    const auto native_daemon = runtime.NativeContextDaemon(token);
    if (token != 1U && !native_daemon.has_value()) {
        throw DexVmError(DexVmErrorReason::internal_invariant, "child execution context has no Thread object");
    }
    const auto object = vm.NewIntrinsicInstance("Ljava/lang/Thread;");
    // Publish the object before allocating its name: NewStringUtf8 may cross
    // a GC safe allocation point, so the fresh Thread must already be strong.
    std::uint64_t id = 1;
    if (native_daemon.has_value())
        id = runtime.SetNativeThreadObject(token, object);
    else
        runtime.SetRootThreadObject(object);
    const auto group_class = vm.Linker().FindClass("Ljava/lang/ThreadGroup;");
    const auto initialized = vm.EnsureClassInitialized(*group_class);
    PropagateOutcome(vm, initialized);
    const auto group_field = vm.Linker().FindFieldRecursive(*group_class, "mMain", "Ljava/lang/ThreadGroup;");
    const auto& linked_group = vm.Linker().Field(*group_field);
    const auto group = VmObjectRef{vm.Linker().Class(linked_group.owner).static_storage[linked_group.slot]};
    InitializeFields(call, fields, object, VmObjectRef{},
                     vm.NewStringUtf8(runtime.NativeContextName(token).value_or(
                         native_daemon.has_value() ? "Thread-" + std::to_string(id) : "main")),
                     static_cast<std::int64_t>(id), 5,
                     native_daemon.has_value() ? VmObjectRef{} : vm.ClassLoaders().SystemLoader(), group);
    call.SetInt(fields.has_been_started, object, 1);
    call.SetInt(fields.daemon, object, native_daemon.value_or(false) ? 1 : 0);
    return object;
}

[[nodiscard]] VmValue Construct(IntrinsicContext& context, const ThreadFields& fields, const VmObjectRef target,
                                const VmObjectRef explicit_name, const bool has_explicit_name,
                                const VmObjectRef explicit_group = VmObjectRef{}, const std::int64_t stack_size = 0) {
    if (has_explicit_name && !explicit_name.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "threadName == null"};
    }
    const IntrinsicCall call(context);
    auto& runtime = context.vm.Threads();
    const auto current = EnsureCurrentThread(call, fields);
    const auto priority = call.GetInt(fields.priority, current);
    const auto context_class_loader = call.GetRef(fields.context_class_loader, current);
    const auto group = explicit_group.IsValid() ? explicit_group : call.GetRef(fields.group, current);
    const auto id = runtime.AllocateThreadId();
    const auto name = has_explicit_name ? explicit_name : context.vm.NewStringUtf8("Thread-" + std::to_string(id));
    InitializeFields(call, fields, context.receiver, target, name, static_cast<std::int64_t>(id), priority,
                     context_class_loader, group, stack_size);
    return VmValue::Void();
}

void PropagateOutcome(Interpreter& vm, const VmCallOutcome& outcome) {
    if (!outcome.exception.IsValid()) return;
    throw VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message};
}

[[nodiscard]] std::int64_t RoundedMillis(const std::int64_t millis, const std::int32_t nanos) {
    if (nanos == 0) return millis;
    if (millis == std::numeric_limits<std::int64_t>::max()) return millis;
    return millis + 1;
}

void ValidateTimeout(const std::int64_t millis, const std::int32_t nanos) {
    if (millis < 0 || nanos < 0 || nanos >= 1'000'000) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "timeout arguments out of range"};
    }
}
}  // namespace

IntrinsicClassDecl Declare_java_lang_Thread() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Thread;", "Ljava/lang/Object;", {"Ljava/lang/Runnable;"});
    builder.InstanceField("localValues", "Ljava/lang/ThreadLocal$Values;", kAccNone);
    builder.InstanceField("parkBlocker", "Ljava/lang/Object;", kAccPrivate);
    const ThreadFields fields{
        builder.BoundInstanceField("target", "Ljava/lang/Runnable;"),
        builder.BoundInstanceField("name", "Ljava/lang/String;"),
        builder.BoundInstanceField("priority", "I"),
        builder.BoundInstanceField("daemon", "Z"),
        builder.BoundInstanceField("stackSize", "J"),
        builder.BoundInstanceField("id", "J"),
        builder.BoundInstanceField("hasBeenStarted", "Z"),
        builder.BoundInstanceField("contextClassLoader", "Ljava/lang/ClassLoader;", kAccPrivate),
        builder.BoundInstanceField("group", "Ljava/lang/ThreadGroup;"),
        builder.BoundInstanceField("parkState", "I", kAccPrivate),
        builder.BoundInstanceField("interruptActions", "Ljava/util/List;", kAccPrivate | kAccFinal),
        builder.BoundInstanceField("uncaughtHandler", "Ljava/lang/Thread$UncaughtExceptionHandler;", kAccPrivate),
        builder.BoundStaticField("defaultUncaughtHandler", "Ljava/lang/Thread$UncaughtExceptionHandler;",
                                 kAccPrivate | kAccStatic),
    };
    builder.ConstantInt("MIN_PRIORITY", "I", 1)
        .ConstantInt("NORM_PRIORITY", "I", 5)
        .ConstantInt("MAX_PRIORITY", "I", 10);

    builder.Constructor("()V", [fields](IntrinsicContext& context) {
        return Construct(context, fields, VmObjectRef{}, VmObjectRef{}, false);
    });
    builder.Constructor("(Ljava/lang/Runnable;)V", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        return Construct(context, fields, call.Ref(0), VmObjectRef{}, false);
    });
    builder.Constructor("(Ljava/lang/String;)V", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        return Construct(context, fields, VmObjectRef{}, call.Ref(0), true);
    });
    builder.Constructor("(Ljava/lang/Runnable;Ljava/lang/String;)V", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        return Construct(context, fields, call.Ref(0), call.Ref(1), true);
    });
    builder.Constructor("(Ljava/lang/ThreadGroup;Ljava/lang/Runnable;)V", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        return Construct(context, fields, call.Ref(1), VmObjectRef{}, false, call.Ref(0));
    });
    builder.Constructor("(Ljava/lang/ThreadGroup;Ljava/lang/Runnable;Ljava/lang/String;)V",
                        [fields](IntrinsicContext& context) {
                            IntrinsicCall call(context);
                            return Construct(context, fields, call.Ref(1), call.Ref(2), true, call.Ref(0));
                        });
    builder.Constructor("(Ljava/lang/ThreadGroup;Ljava/lang/String;)V", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        return Construct(context, fields, VmObjectRef{}, call.Ref(1), true, call.Ref(0));
    });
    builder.Constructor(
        "(Ljava/lang/ThreadGroup;Ljava/lang/Runnable;Ljava/lang/String;J)V", [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            return Construct(context, fields, call.Ref(1), call.Ref(2), true, call.Ref(0), call.Long(3));
        });

    // AOSP Thread.run: target dispatch belongs here. VmThreadRuntime always
    // dispatches virtual this.run(), so a Thread subclass override wins.
    builder.VirtualMethod("run", "()V", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto target = call.GetRef(fields.target);
        if (!target.IsValid()) return VmValue::Void();
        auto& linker = context.vm.Linker();
        const auto target_class = context.vm.Model().ObjectClass(target);
        const auto index = linker.FindVtableIndex(target_class, "run", "()V");
        if (!index.has_value()) {
            throw VmJavaThrow{"Ljava/lang/AbstractMethodError;", "Runnable target has no run()"};
        }
        const auto outcome =
            context.vm.Call(linker.Class(target_class).vtable[*index], std::vector<VmValue>{VmValue::Ref(target)});
        PropagateOutcome(context.vm, outcome);
        return VmValue::Void();
    });

    builder.VirtualMethod(
        "start", "()V",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            if (call.GetInt(fields.has_been_started) != 0) {
                throw VmJavaThrow{"Ljava/lang/IllegalThreadStateException;", "Thread already started"};
            }
            call.SetInt(fields.has_been_started, 1);
            const auto name = call.GetRef(fields.name);
            context.vm.Threads().Start(
                context.receiver, context.vm.StringUtf8(name), static_cast<std::uint64_t>(call.GetLong(fields.id)),
                [fields](Interpreter& vm, const VmObjectRef thread, const VmObjectRef throwable) {
                    const std::span<const VmValue> no_arguments;
                    IntrinsicContext field_context{vm, thread, no_arguments};
                    const IntrinsicCall field_call(field_context);
                    auto handler = field_call.GetRef(fields.uncaught_handler, thread);
                    if (!handler.IsValid()) {
                        handler = field_call.GetRef(fields.default_uncaught_handler);
                    }
                    if (!handler.IsValid()) return false;
                    const auto handler_class = vm.Model().ObjectClass(handler);
                    const auto index = vm.Linker().FindVtableIndex(handler_class, "uncaughtException",
                                                                   "(Ljava/lang/Thread;Ljava/lang/Throwable;)V");
                    if (!index.has_value()) return true;
                    // AOSP ignores failures raised by an uncaught-exception
                    // handler; invoking a selected handler completes dispatch.
                    const std::vector<VmValue> arguments{VmValue::Ref(handler), VmValue::Ref(thread),
                                                         VmValue::Ref(throwable)};
                    static_cast<void>(vm.Call(vm.Linker().Class(handler_class).vtable[*index], arguments));
                    return true;
                });
            return VmValue::Void();
        },
        kAccPublic | kAccSynchronized);

    builder.StaticMethod("currentThread", "()Ljava/lang/Thread;", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        return VmValue::Ref(EnsureCurrentThread(call, fields));
    });
    builder.VirtualMethod("getId", "()J", [fields](IntrinsicContext& context) {
        return VmValue::Long(IntrinsicCall(context).GetLong(fields.id));
    });
    builder.FinalMethod("getName", "()Ljava/lang/String;", [fields](IntrinsicContext& context) {
        return VmValue::Ref(IntrinsicCall(context).GetRef(fields.name));
    });
    builder.FinalMethod("setName", "(Ljava/lang/String;)V", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto name = call.NonNullRef(0, "threadName");
        call.SetRef(fields.name, name);
        context.vm.Threads().Rename(context.receiver, context.vm.StringUtf8(name));
        return VmValue::Void();
    });
    // 返回该线程继承或显式设置的上下文类加载器。
    builder.VirtualMethod("getContextClassLoader", "()Ljava/lang/ClassLoader;", [fields](IntrinsicContext& context) {
        return VmValue::Ref(IntrinsicCall(context).GetRef(fields.context_class_loader));
    });
    // 设置线程上下文类加载器；API 19 允许传入 null。
    builder.VirtualMethod("setContextClassLoader", "(Ljava/lang/ClassLoader;)V", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        call.SetRef(fields.context_class_loader, call.Ref(0));
        return VmValue::Void();
    });
    // 返回该线程当前可观测的 DexVM Java 调用栈。
    builder.VirtualMethod("getStackTrace", "()[Ljava/lang/StackTraceElement;", [fields](IntrinsicContext& context) {
        return VmValue::Ref(
            StackTraceArray(context.vm, static_cast<std::uint64_t>(IntrinsicCall(context).GetLong(fields.id))));
    });
    // 返回线程当前的 API 19 状态枚举。
    builder.VirtualMethod("getState", "()Ljava/lang/Thread$State;", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        if (call.GetInt(fields.has_been_started) == 0) {
            return VmValue::Ref(ThreadState(context.vm, "NEW"));
        }
        if (!context.vm.Threads().IsAlive(context.receiver)) {
            return VmValue::Ref(ThreadState(context.vm, "TERMINATED"));
        }
        auto state = std::string_view{"RUNNABLE"};
        for (const auto& thread : context.vm.Threads().Snapshot()) {
            if (thread.object != context.receiver.Value()) continue;
            if (thread.wait_state == VmThreadWaitState::sleeping)
                state = "TIMED_WAITING";
            else if (thread.wait_state == VmThreadWaitState::joining || thread.wait_state == VmThreadWaitState::monitor)
                state = "WAITING";
            break;
        }
        return VmValue::Ref(ThreadState(context.vm, state));
    });
    // 返回当前 VM 内所有存活线程及其 Java 调用栈。
    builder.StaticMethod("getAllStackTraces", "()Ljava/util/Map;", [](IntrinsicContext& context) {
        auto& vm = context.vm;
        const auto map = vm.NewIntrinsicInstance("Ljava/util/HashMap;");
        const std::array map_references{map};
        [[maybe_unused]] const auto map_roots = vm.ProtectReferences(map_references);
        const auto map_class = vm.Model().ObjectClass(map);
        const auto constructor = vm.Linker().FindDirectMethod(map_class, "<init>", "()V");
        const std::vector<VmValue> constructor_arguments{VmValue::Ref(map)};
        PropagateOutcome(vm, vm.Call(*constructor, constructor_arguments));
        const auto put_index =
            vm.Linker().FindVtableIndex(map_class, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
        for (const auto& thread : vm.Threads().Snapshot()) {
            if (thread.status != VmThreadStatus::created && thread.status != VmThreadStatus::running) {
                continue;
            }
            const auto stack = StackTraceArray(vm, thread.id);
            const std::vector<VmValue> arguments{VmValue::Ref(map), VmValue::Ref(VmObjectRef{thread.object}),
                                                 VmValue::Ref(stack)};
            PropagateOutcome(vm, vm.Call(vm.Linker().Class(map_class).vtable[*put_index], arguments));
        }
        return VmValue::Ref(map);
    });
    // 返回此线程显式设置的未捕获异常处理器。
    builder.VirtualMethod("getUncaughtExceptionHandler", "()Ljava/lang/Thread$UncaughtExceptionHandler;",
                          [fields](IntrinsicContext& context) {
                              return VmValue::Ref(IntrinsicCall(context).GetRef(fields.uncaught_handler));
                          });
    // 设置此线程专用的未捕获异常处理器，可传入 null 清除。
    builder.VirtualMethod("setUncaughtExceptionHandler", "(Ljava/lang/Thread$UncaughtExceptionHandler;)V",
                          [fields](IntrinsicContext& context) {
                              IntrinsicCall call(context);
                              call.SetRef(fields.uncaught_handler, call.Ref(0));
                              return VmValue::Void();
                          });
    // 返回进程内所有线程共用的默认未捕获异常处理器。
    builder.StaticMethod("getDefaultUncaughtExceptionHandler", "()Ljava/lang/Thread$UncaughtExceptionHandler;",
                         [fields](IntrinsicContext& context) {
                             return VmValue::Ref(IntrinsicCall(context).GetRef(fields.default_uncaught_handler));
                         });
    // 设置进程内所有线程共用的默认未捕获异常处理器。
    builder.StaticMethod("setDefaultUncaughtExceptionHandler", "(Ljava/lang/Thread$UncaughtExceptionHandler;)V",
                         [fields](IntrinsicContext& context) {
                             IntrinsicCall call(context);
                             call.SetRef(fields.default_uncaught_handler, call.Ref(0));
                             return VmValue::Void();
                         });
    builder.FinalMethod("isAlive", "()Z", [](IntrinsicContext& context) {
        return VmValue::Int(context.vm.Threads().IsAlive(context.receiver) ? 1 : 0);
    });
    // 返回线程所属的线程组；已终止线程返回 null。
    builder.FinalMethod("getThreadGroup", "()Ljava/lang/ThreadGroup;", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        if (call.GetInt(fields.has_been_started) != 0 && !context.vm.Threads().IsAlive(context.receiver)) {
            return VmValue::Ref(VmObjectRef{});
        }
        return VmValue::Ref(call.GetRef(fields.group));
    });
    builder.FinalMethod("getPriority", "()I", [fields](IntrinsicContext& context) {
        return VmValue::Int(IntrinsicCall(context).GetInt(fields.priority));
    });
    builder.FinalMethod("setPriority", "(I)V", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto priority = call.Int(0);
        if (priority < 1 || priority > 10) {
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "Priority out of range: " + std::to_string(priority)};
        }
        // Guest-visible only: host scheduler priority is
        // deliberately not modeled yet.
        call.SetInt(fields.priority, priority);
        return VmValue::Void();
    });

    builder.VirtualMethod("interrupt", "()V", [fields](IntrinsicContext& context) {
        context.vm.Threads().Interrupt(context.receiver);
        IntrinsicCall call(context);
        const auto actions = call.GetRef(fields.interrupt_actions);
        auto& monitors = context.vm.Monitors();
        const auto token = context.vm.CurrentContextToken();
        monitors.Enter(actions, token);
        try {
            const auto actions_class = context.vm.Model().ObjectClass(actions);
            const auto size_index = context.vm.Linker().FindVtableIndex(actions_class, "size", "()I");
            const std::vector<VmValue> size_args{VmValue::Ref(actions)};
            const auto size_outcome =
                context.vm.Call(context.vm.Linker().Class(actions_class).vtable[*size_index], size_args);
            PropagateOutcome(context.vm, size_outcome);
            const auto size = size_outcome.value.AsInt();
            const auto get_index = context.vm.Linker().FindVtableIndex(actions_class, "get", "(I)Ljava/lang/Object;");
            for (auto index = size - 1; index >= 0; --index) {
                const std::vector<VmValue> get_args{VmValue::Ref(actions), VmValue::Int(index)};
                const auto get_outcome =
                    context.vm.Call(context.vm.Linker().Class(actions_class).vtable[*get_index], get_args);
                PropagateOutcome(context.vm, get_outcome);
                const auto action = get_outcome.value.ref;
                if (!action.IsValid()) continue;
                const auto action_class = context.vm.Model().ObjectClass(action);
                const auto run_index = context.vm.Linker().FindVtableIndex(action_class, "run", "()V");
                const std::vector<VmValue> run_args{VmValue::Ref(action)};
                PropagateOutcome(context.vm,
                                 context.vm.Call(context.vm.Linker().Class(action_class).vtable[*run_index], run_args));
            }
        } catch (...) {
            monitors.Exit(actions, token);
            throw;
        }
        monitors.Exit(actions, token);
        return VmValue::Void();
    });
    builder.VirtualMethod("isInterrupted", "()Z", [](IntrinsicContext& context) {
        return VmValue::Int(context.vm.Threads().IsInterrupted(context.receiver) ? 1 : 0);
    });
    builder.StaticMethod("interrupted", "()Z", [](IntrinsicContext& context) {
        return VmValue::Int(context.vm.Threads().ClearCurrentInterrupt() ? 1 : 0);
    });

    builder.FinalMethod("join", "()V", [](IntrinsicContext& context) {
        context.vm.Threads().Join(context.receiver);
        return VmValue::Void();
    });
    builder.FinalMethod("join", "(J)V", [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto millis = call.Long(0);
        ValidateTimeout(millis, 0);
        if (millis == 0) {
            context.vm.Threads().Join(context.receiver);
        } else {
            context.vm.Threads().Join(context.receiver, millis);
        }
        return VmValue::Void();
    });
    builder.FinalMethod("join", "(JI)V", [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto millis = call.Long(0);
        const auto nanos = call.Int(1);
        ValidateTimeout(millis, nanos);
        if (millis == 0 && nanos == 0) {
            context.vm.Threads().Join(context.receiver);
        } else {
            context.vm.Threads().Join(context.receiver, RoundedMillis(millis, nanos));
        }
        return VmValue::Void();
    });

    builder.StaticMethod("sleep", "(J)V", [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto millis = call.Long(0);
        ValidateTimeout(millis, 0);
        context.vm.Threads().Sleep(millis);
        return VmValue::Void();
    });
    builder.StaticMethod("sleep", "(JI)V", [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto millis = call.Long(0);
        const auto nanos = call.Int(1);
        ValidateTimeout(millis, nanos);
        context.vm.Threads().Sleep(RoundedMillis(millis, nanos));
        return VmValue::Void();
    });
    builder.StaticMethod("yield", "()V", [](IntrinsicContext& context) {
        context.vm.Threads().Yield();
        return VmValue::Void();
    });
    builder.StaticMethod("holdsLock", "(Ljava/lang/Object;)Z", [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto object = call.NonNullRef(0, "object");
        return VmValue::Int(context.vm.Monitors().IsOwner(object, context.vm.CurrentContextToken()) ? 1 : 0);
    });

    // 返回当前线程组内存活线程的估计数量。
    builder.StaticMethod("activeCount", "()I", [fields](IntrinsicContext& context) {
        const IntrinsicCall call(context);
        const auto thread = EnsureCurrentThread(call, fields);
        return InvokeThreadVirtual(context.vm, call.GetRef(fields.group, thread), "activeCount", "()I");
    });
    // 将当前线程的 Java 栈打印到标准错误流。
    builder.StaticMethod("dumpStack", "()V", [](IntrinsicContext& context) {
        const auto throwable = context.vm.NewIntrinsicInstance("Ljava/lang/Throwable;");
        const std::array throwable_references{throwable};
        [[maybe_unused]] const auto throwable_roots = context.vm.ProtectReferences(throwable_references);
        const auto throwable_class = context.vm.Model().ObjectClass(throwable);
        const auto constructor =
            context.vm.Linker().FindDirectMethod(throwable_class, "<init>", "(Ljava/lang/String;)V");
        const std::vector<VmValue> ctor_args{VmValue::Ref(throwable),
                                             VmValue::Ref(context.vm.NewStringUtf8("stack dump"))};
        PropagateOutcome(context.vm, context.vm.Call(*constructor, ctor_args));
        const auto print = context.vm.Linker().FindVtableIndex(throwable_class, "printStackTrace", "()V");
        const std::vector<VmValue> print_args{VmValue::Ref(throwable)};
        PropagateOutcome(context.vm,
                         context.vm.Call(context.vm.Linker().Class(throwable_class).vtable[*print], print_args));
        return VmValue::Void();
    });
    // 将当前线程组及其子组的存活线程复制到调用方数组。
    builder.StaticMethod("enumerate", "([Ljava/lang/Thread;)I", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto thread = EnsureCurrentThread(call, fields);
        return InvokeThreadVirtual(context.vm, call.GetRef(fields.group, thread), "enumerate", "([Ljava/lang/Thread;)I",
                                   {VmValue::Ref(call.Ref(0))});
    });
    // API 19 中访问检查为空操作。
    builder.FinalMethod("checkAccess", "()V", [](IntrinsicContext&) { return VmValue::Void(); });
    // 返回当前可观测 Java 栈帧数。
    builder.VirtualMethod("countStackFrames", "()I", [fields](IntrinsicContext& context) {
        const auto array =
            StackTraceArray(context.vm, static_cast<std::uint64_t>(IntrinsicCall(context).GetLong(fields.id)));
        return VmValue::Int(context.vm.Model().ArrayLength(array));
    });

    // 为当前线程添加一个中断时执行的 Runnable。
    builder.FinalMethod("pushInterruptAction$", "(Ljava/lang/Runnable;)V", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto action = call.Ref(0);
        const auto list = call.GetRef(fields.interrupt_actions);
        auto& monitors = context.vm.Monitors();
        const auto token = context.vm.CurrentContextToken();
        monitors.Enter(list, token);
        const auto list_class = context.vm.Model().ObjectClass(list);
        const auto add = context.vm.Linker().FindVtableIndex(list_class, "add", "(Ljava/lang/Object;)Z");
        const std::vector<VmValue> args{VmValue::Ref(list), VmValue::Ref(action)};
        try {
            PropagateOutcome(context.vm, context.vm.Call(context.vm.Linker().Class(list_class).vtable[*add], args));
        } catch (...) {
            monitors.Exit(list, token);
            throw;
        }
        monitors.Exit(list, token);
        if (action.IsValid() && context.vm.Threads().IsInterrupted(context.receiver)) {
            const auto action_class = context.vm.Model().ObjectClass(action);
            const auto run = context.vm.Linker().FindVtableIndex(action_class, "run", "()V");
            const std::vector<VmValue> run_args{VmValue::Ref(action)};
            PropagateOutcome(context.vm,
                             context.vm.Call(context.vm.Linker().Class(action_class).vtable[*run], run_args));
        }
        return VmValue::Void();
    });
    // 按栈顺序移除最近注册的中断 Runnable。
    builder.FinalMethod("popInterruptAction$", "(Ljava/lang/Runnable;)V", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto expected = call.Ref(0);
        const auto list = call.GetRef(fields.interrupt_actions);
        auto& monitors = context.vm.Monitors();
        const auto token = context.vm.CurrentContextToken();
        monitors.Enter(list, token);
        try {
            const auto list_class = context.vm.Model().ObjectClass(list);
            const auto size_method = context.vm.Linker().FindVtableIndex(list_class, "size", "()I");
            const std::vector<VmValue> size_args{VmValue::Ref(list)};
            const auto size_outcome =
                context.vm.Call(context.vm.Linker().Class(list_class).vtable[*size_method], size_args);
            PropagateOutcome(context.vm, size_outcome);
            const auto size = size_outcome.value.AsInt();
            if (size <= 0) {
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "interrupt action stack is empty"};
            }
            const auto remove = context.vm.Linker().FindVtableIndex(list_class, "remove", "(I)Ljava/lang/Object;");
            const std::vector<VmValue> args{VmValue::Ref(list), VmValue::Int(size - 1)};
            const auto remove_outcome = context.vm.Call(context.vm.Linker().Class(list_class).vtable[*remove], args);
            PropagateOutcome(context.vm, remove_outcome);
            const auto removed = remove_outcome.value.ref;
            if (removed != expected) {
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "interrupt action order mismatch"};
            }
        } catch (...) {
            monitors.Exit(list, token);
            throw;
        }
        monitors.Exit(list, token);
        return VmValue::Void();
    });
    // 向线程发放一个 park 许可，或唤醒正在 park 的线程。
    builder.VirtualMethod("unpark", "()V", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        auto& monitors = context.vm.Monitors();
        const auto token = context.vm.CurrentContextToken();
        monitors.Enter(context.receiver, token);
        const auto state = call.GetInt(fields.park_state);
        if (state == 3) {
            call.SetInt(fields.park_state, 1);
            monitors.NotifyAll(context.receiver, token);
        } else if (state == 1) {
            call.SetInt(fields.park_state, 2);
        }
        monitors.Exit(context.receiver, token);
        return VmValue::Void();
    });
    const auto park = [fields](IntrinsicContext& context, const std::int64_t millis) {
        IntrinsicCall call(context);
        if (context.vm.Threads().CurrentThreadObject() != context.receiver) {
            throw VmJavaThrow{"Ljava/lang/AssertionError;", "park receiver is not current thread"};
        }
        auto& monitors = context.vm.Monitors();
        const auto token = context.vm.CurrentContextToken();
        monitors.Enter(context.receiver, token);
        if (call.GetInt(fields.park_state) == 2) {
            call.SetInt(fields.park_state, 1);
            monitors.Exit(context.receiver, token);
            return VmValue::Void();
        }
        if (call.GetInt(fields.park_state) == 3) {
            monitors.Exit(context.receiver, token);
            throw VmJavaThrow{"Ljava/lang/AssertionError;", "attempt to repark"};
        }
        call.SetInt(fields.park_state, 3);
        try {
            context.vm.WaitOnMonitor(context.receiver, millis);
        } catch (const VmJavaThrow& thrown) {
            if (thrown.descriptor != "Ljava/lang/InterruptedException;") {
                monitors.Exit(context.receiver, token);
                throw;
            }
            context.vm.Threads().Interrupt(context.receiver);
        }
        if (call.GetInt(fields.park_state) == 3) {
            call.SetInt(fields.park_state, 1);
        }
        monitors.Exit(context.receiver, token);
        return VmValue::Void();
    };
    // 按纳秒时长 park 当前线程，0 表示无限等待。
    builder.VirtualMethod("parkFor", "(J)V", [park](IntrinsicContext& context) {
        const auto nanos = IntrinsicCall(context).Long(0);
        if (nanos < 0) throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "nanos < 0"};
        const auto millis = nanos == 0 ? 0 : nanos / 1'000'000 + (nanos % 1'000'000 == 0 ? 0 : 1);
        return park(context, millis);
    });
    // park 当前线程直到统一 Clock 到达指定毫秒时刻。
    builder.VirtualMethod("parkUntil", "(J)V", [fields, park](IntrinsicContext& context) {
        const auto deadline = IntrinsicCall(context).Long(0);
        const auto clock = context.vm.Monitors().TimeSource();
        if (!clock) throw DexVmError(DexVmErrorReason::internal_invariant, "parkUntil needs unified Clock");
        const auto now = clock();
        if (deadline <= now) {
            auto& monitors = context.vm.Monitors();
            const auto token = context.vm.CurrentContextToken();
            monitors.Enter(context.receiver, token);
            IntrinsicCall(context).SetInt(fields.park_state, 1);
            monitors.Exit(context.receiver, token);
            return VmValue::Void();
        }
        return park(context, deadline - now);
    });

    builder.FinalMethod("isDaemon", "()Z", [fields](IntrinsicContext& context) {
        return VmValue::Int(IntrinsicCall(context).GetInt(fields.daemon));
    });
    builder.FinalMethod("setDaemon", "(Z)V", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        if (call.GetInt(fields.has_been_started) != 0) {
            throw VmJavaThrow{"Ljava/lang/IllegalThreadStateException;", "Thread already started"};
        }
        // Bounded semantics: the flag is exposed, but session termination is
        // not daemon-driven in OGPlay.
        call.SetInt(fields.daemon, call.Int(0) != 0 ? 1 : 0);
        return VmValue::Void();
    });
    // 生成包含线程名、优先级和线程组名的 API 19 文本。
    builder.OverrideMethod("toString", "()Ljava/lang/String;", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto name = context.vm.StringUtf8(call.GetRef(fields.name));
        const auto group = call.GetRef(fields.group);
        std::string group_name;
        if (group.IsValid()) {
            const auto index = context.vm.Linker().FindVtableIndex(context.vm.Model().ObjectClass(group), "getName",
                                                                   "()Ljava/lang/String;");
            const std::vector<VmValue> arguments{VmValue::Ref(group)};
            const auto outcome = context.vm.Call(
                context.vm.Linker().Class(context.vm.Model().ObjectClass(group)).vtable[*index], arguments);
            PropagateOutcome(context.vm, outcome);
            group_name = context.vm.StringUtf8(outcome.value.ref);
        }
        return VmValue::Ref(context.vm.NewStringUtf8(
            "Thread[" + name + "," + std::to_string(call.GetInt(fields.priority)) + "," + group_name + "]"));
    });

    builder.UnimplementedFinal("stop", "()V");
    builder.UnimplementedFinal("stop", "(Ljava/lang/Throwable;)V", kAccPublic | kAccSynchronized);
    builder.UnimplementedFinal("suspend", "()V");
    builder.UnimplementedFinal("resume", "()V");
    builder.UnimplementedVirtual("destroy", "()V");
    return std::move(builder).Build();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {

namespace {
bool GroupContains(const IntrinsicCall& call, IntrinsicFieldHandle parent, VmObjectRef group) {
    for (; group.IsValid(); group = call.GetRef(parent, group)) {
        if (group == call.Receiver()) return true;
    }
    return false;
}

std::vector<VmObjectRef> LiveGroupThreads(IntrinsicContext& context, IntrinsicFieldHandle parent, bool recurse) {
    auto& vm = context.vm;
    const IntrinsicCall call(context);
    std::vector<VmObjectRef> threads;
    for (const auto& state : vm.Threads().Snapshot()) {
        const auto thread = VmObjectRef{state.object};
        if (!vm.Threads().IsAlive(thread)) continue;
        const auto group = InvokeThreadVirtual(vm, thread, "getThreadGroup", "()Ljava/lang/ThreadGroup;").ref;
        if (group == context.receiver || (recurse && GroupContains(call, parent, group))) {
            threads.push_back(thread);
        }
    }
    return threads;
}
}  // namespace

IntrinsicClassDecl Declare_java_lang_ThreadGroup() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/ThreadGroup;", "Ljava/lang/Object;",
                                                {"Ljava/lang/Thread$UncaughtExceptionHandler;"});
    const auto name = builder.BoundInstanceField("name", "Ljava/lang/String;", kAccPrivate);
    const auto parent = builder.BoundInstanceField("parent", "Ljava/lang/ThreadGroup;", kAccFinal);
    const auto system = builder.BoundStaticField("mSystem", "Ljava/lang/ThreadGroup;", kAccStatic | kAccFinal);
    const auto main = builder.BoundStaticField("mMain", "Ljava/lang/ThreadGroup;", kAccStatic | kAccFinal);
    builder.ClassInitializer([name, parent, system, main](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto system_group = context.vm.NewIntrinsicInstance("Ljava/lang/ThreadGroup;");
        call.SetRef(system, system_group);
        call.SetRef(parent, system_group, VmObjectRef{});
        call.SetRef(name, system_group, context.vm.NewStringUtf8("system"));
        const auto main_group = context.vm.NewIntrinsicInstance("Ljava/lang/ThreadGroup;");
        call.SetRef(main, main_group);
        call.SetRef(parent, main_group, system_group);
        call.SetRef(name, main_group, context.vm.NewStringUtf8("main"));
        return VmValue::Void();
    });
    builder.Constructor("(Ljava/lang/String;)V", [name, parent](IntrinsicContext& context) {
        const IntrinsicCall call(context);
        const auto thread =
            InvokeGuestDirect(context.vm, "Ljava/lang/Thread;", "currentThread", "()Ljava/lang/Thread;", {}).ref;
        const auto group = InvokeThreadVirtual(context.vm, thread, "getThreadGroup", "()Ljava/lang/ThreadGroup;").ref;
        call.SetRef(parent, group);
        call.SetRef(name, call.Ref(0));
        return VmValue::Void();
    });
    builder.Constructor("(Ljava/lang/ThreadGroup;Ljava/lang/String;)V", [name, parent](IntrinsicContext& context) {
        const IntrinsicCall call(context);
        call.SetRef(parent, call.NonNullRef(0, "parent"));
        call.SetRef(name, call.Ref(1));
        return VmValue::Void();
    });
    builder.FinalMethod("getName", "()Ljava/lang/String;", [name](IntrinsicContext& context) {
        return VmValue::Ref(IntrinsicCall(context).GetRef(name));
    });
    builder.FinalMethod("getParent", "()Ljava/lang/ThreadGroup;", [parent](IntrinsicContext& context) {
        return VmValue::Ref(IntrinsicCall(context).GetRef(parent));
    });
    builder.FinalMethod("parentOf", "(Ljava/lang/ThreadGroup;)Z", [parent](IntrinsicContext& context) {
        const IntrinsicCall call(context);
        return VmValue::Int(GroupContains(call, parent, call.Ref(0)));
    });
    builder.VirtualMethod("activeCount", "()I", [parent](IntrinsicContext& context) {
        return VmValue::Int(static_cast<std::int32_t>(LiveGroupThreads(context, parent, true).size()));
    });
    builder.VirtualMethod("enumerate", "([Ljava/lang/Thread;)I", [](IntrinsicContext& context) {
        return InvokeThreadVirtual(context.vm, context.receiver, "enumerate", "([Ljava/lang/Thread;Z)I",
                                   {context.arguments[0], VmValue::Int(1)});
    });
    builder.VirtualMethod("enumerate", "([Ljava/lang/Thread;Z)I", [parent](IntrinsicContext& context) {
        const IntrinsicCall call(context);
        const auto array = call.NonNullRef(0, "threads");
        const auto capacity = context.vm.Model().ArrayLength(array);
        JniSize written = 0;
        for (const auto thread : LiveGroupThreads(context, parent, call.Int(1) != 0)) {
            if (written >= capacity) break;
            context.vm.Model().SetObjectElement(array, written++, thread);
        }
        return VmValue::Int(written);
    });
    builder.FinalMethod("interrupt", "()V", [parent](IntrinsicContext& context) {
        auto& vm = context.vm;
        const auto threads = LiveGroupThreads(context, parent, true);
        const auto roots = vm.ProtectReferences(threads);
        for (const auto thread : threads) {
            if (vm.Threads().IsAlive(thread)) {
                static_cast<void>(InvokeThreadVirtual(vm, thread, "interrupt", "()V"));
            }
        }
        return VmValue::Void();
    });
    builder.VirtualMethod(
        "uncaughtException", "(Ljava/lang/Thread;Ljava/lang/Throwable;)V", [parent](IntrinsicContext& context) {
            IntrinsicCall call(context);
            auto& vm = context.vm;
            const auto thread = call.Ref(0);
            const auto throwable = call.Ref(1);
            const auto invoke = [&](VmObjectRef receiver, const char* method, const char* signature,
                                    std::vector<VmValue> args) {
                if (!receiver.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;", "null uncaught receiver"};
                }
                const auto type = vm.Model().ObjectClass(receiver);
                const auto slot = vm.Linker().FindVtableIndex(type, method, signature);
                args.insert(args.begin(), VmValue::Ref(receiver));
                const auto result = vm.Call(vm.Linker().Class(type).vtable.at(*slot), args);
                PropagateOutcome(vm, result);
                return result.value;
            };
            auto handler = call.GetRef(parent);
            if (!handler.IsValid()) {
                const auto type = vm.Linker().ResolveDescriptor("Ljava/lang/Thread;");
                const auto getter = vm.Linker().FindDirectMethod(type, "getDefaultUncaughtExceptionHandler",
                                                                 "()Ljava/lang/Thread$UncaughtExceptionHandler;");
                const auto result = vm.Call(*getter, {});
                PropagateOutcome(vm, result);
                handler = result.value.ref;
            }
            if (handler.IsValid()) {
                invoke(handler, "uncaughtException", "(Ljava/lang/Thread;Ljava/lang/Throwable;)V",
                       {VmValue::Ref(thread), VmValue::Ref(throwable)});
            } else if (!throwable.IsValid() ||
                       !vm.Linker().IsAssignable(vm.Linker().ResolveDescriptor("Ljava/lang/ThreadDeath;"),
                                                 vm.Model().ObjectClass(throwable))) {
                const auto system_type = vm.Linker().ResolveDescriptor("Ljava/lang/System;");
                PropagateOutcome(vm, vm.EnsureClassInitialized(system_type));
                const auto err_field = vm.Linker().FindFieldRecursive(system_type, "err", "Ljava/io/PrintStream;");
                const auto& field = vm.Linker().Field(*err_field);
                const auto err = VmObjectRef(vm.Linker().Class(field.owner).static_storage.at(field.slot));
                const auto name = invoke(thread, "getName", "()Ljava/lang/String;", {}).ref;
                const auto prefix = vm.NewStringUtf8("Exception in thread \"" + vm.StringUtf8(name) + "\" ");
                const auto roots = vm.ProtectReferences(std::array{err, prefix});
                invoke(err, "print", "(Ljava/lang/String;)V", {VmValue::Ref(prefix)});
                invoke(throwable, "printStackTrace", "(Ljava/io/PrintStream;)V", {VmValue::Ref(err)});
            }
            return VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics

// Throwable Java behavior and fields come from BootDex; only VM stack primitives remain.
namespace ogplay::runtime::dexvm::intrinsics {
void AppendJavaLangThrowables(std::vector<IntrinsicClassDecl>& catalog) {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Throwable;");
    builder.MergesBootClass();
    builder.StaticMethod(
        "nativeFillInStackTrace", "()Ljava/lang/Object;",
        [](IntrinsicContext& c) { return VmValue::Ref(c.vm.CaptureThrowableStack()); }, kAccPrivate | kAccNative);
    builder.StaticMethod(
        "nativeGetStackTrace", "(Ljava/lang/Object;)[Ljava/lang/StackTraceElement;",
        [](IntrinsicContext& c) { return VmValue::Ref(c.vm.MaterializeThrowableStack(c.arguments[0].ref)); },
        kAccPrivate | kAccNative);
    catalog.push_back(std::move(builder).Build());
}
}  // namespace ogplay::runtime::dexvm::intrinsics
