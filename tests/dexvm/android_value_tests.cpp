#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "boot_dex.h"
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/runtime/integration/host_image_decode.h"
#include "../fixtures/bitmap_decode_data.h"

#include "android_value_fixture.h"

using namespace ogplay::test::android_value;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("DVM-86 Base64 and sparse arrays preserve data semantics") {
    AndroidValueVm fixture;
    const auto input = fixture.Bytes("OGPlay");
    const auto encoded = fixture.Static(
        "Landroid/util/Base64;", "encodeToString", "([BI)Ljava/lang/String;",
        {VmValue::Ref(input), VmValue::Int(2)}).ref;
    CHECK(fixture.vm.StringUtf8(encoded) == "T0dQbGF5");
    const auto decoded = fixture.Static(
        "Landroid/util/Base64;", "decode", "(Ljava/lang/String;I)[B",
        {VmValue::Ref(encoded), VmValue::Int(0)}).ref;
    CHECK(fixture.BytesOf(decoded) == "OGPlay");

    const auto sparse = fixture.New("Landroid/util/SparseArray;");
    const auto first = fixture.vm.NewStringUtf8("first");
    const auto second = fixture.vm.NewStringUtf8("second");
    fixture.On(sparse, "put", "(ILjava/lang/Object;)V",
               {VmValue::Int(7), VmValue::Ref(second)});
    fixture.On(sparse, "put", "(ILjava/lang/Object;)V",
               {VmValue::Int(2), VmValue::Ref(first)});
    CHECK(fixture.On(sparse, "size", "()I").AsInt() == 2);
    CHECK(fixture.On(sparse, "keyAt", "(I)I", {VmValue::Int(0)}).AsInt() == 2);
    CHECK(fixture.On(sparse, "get", "(I)Ljava/lang/Object;",
                     {VmValue::Int(7)}).ref == second);

    const auto ints = fixture.New("Landroid/util/SparseIntArray;");
    fixture.On(ints, "put", "(II)V", {VmValue::Int(9), VmValue::Int(42)});
    CHECK(fixture.On(ints, "get", "(I)I", {VmValue::Int(9)}).AsInt() == 42);
}

TEST_CASE("DVM-98 Android platform enums share generated enum behavior") {
    AndroidValueVm fixture;
    const std::vector<std::pair<std::string, std::vector<std::string>>> specs{
        {"Landroid/graphics/Bitmap$Config;",
         {"ALPHA_8", "RGB_565", "ARGB_4444", "ARGB_8888"}},
        {"Landroid/graphics/Region$Op;", {"REPLACE"}},
        {"Landroid/graphics/Path$Direction;", {"CW", "CCW"}},
        {"Landroid/graphics/PorterDuff$Mode;",
         {"CLEAR", "SRC", "DST", "SRC_OVER", "DST_OVER", "SRC_IN",
          "DST_IN", "SRC_OUT", "DST_OUT", "SRC_ATOP", "DST_ATOP",
          "XOR", "DARKEN", "LIGHTEN", "MULTIPLY", "SCREEN", "ADD",
          "OVERLAY"}},
        {"Landroid/net/NetworkInfo$State;", {"CONNECTING", "CONNECTED", "SUSPENDED", "DISCONNECTING", "DISCONNECTED", "UNKNOWN"}},
        {"Landroid/os/AsyncTask$Status;",
         {"PENDING", "RUNNING", "FINISHED"}},
        {"Landroid/widget/ImageView$ScaleType;",
         {"CENTER", "CENTER_INSIDE", "FIT_CENTER", "FIT_XY",
          "CENTER_CROP"}}};
    const auto enum_class = fixture.linker.ResolveDescriptor("Ljava/lang/Enum;");

    for (const auto& [descriptor, constants] : specs) {
        CAPTURE(descriptor);
        const auto klass = fixture.linker.ResolveDescriptor(descriptor);
        const auto& linked_class = fixture.linker.Class(klass);
        REQUIRE(linked_class.super.has_value());
        CHECK(*linked_class.super == enum_class);
        CHECK((linked_class.access_flags & (kAccFinal | kAccEnum)) ==
              (kAccFinal | kAccEnum));

        const auto array_descriptor = "[" + descriptor;
        const auto values_descriptor = "()" + array_descriptor;
        const auto first = fixture.Static(descriptor.c_str(), "values",
                                          values_descriptor.c_str()).ref;
        const auto second = fixture.Static(descriptor.c_str(), "values",
                                           values_descriptor.c_str()).ref;
        CHECK(first != second);
        REQUIRE(fixture.model.ArrayLength(first) ==
                static_cast<JniSize>(constants.size()));

        for (std::size_t ordinal = 0; ordinal < constants.size(); ++ordinal) {
            const auto& name = constants[ordinal];
            const auto field = fixture.linker.FindFieldRecursive(
                klass, name, descriptor);
            REQUIRE(field.has_value());
            CHECK(fixture.linker.Field(*field).access_flags ==
                  (kAccPublic | kAccStatic | kAccFinal | kAccEnum));
            const auto value = VmObjectRef(
                fixture.linker.Class(klass).static_storage[
                    fixture.linker.Field(*field).slot]);
            CHECK(fixture.model.GetObjectElement(
                      first, static_cast<JniSize>(ordinal)) == value);
            CHECK(fixture.vm.StringUtf8(
                      fixture.On(value, "name", "()Ljava/lang/String;").ref) ==
                  name);
            CHECK(fixture.On(value, "ordinal", "()I").AsInt() ==
                  static_cast<std::int32_t>(ordinal));

            const auto value_of_descriptor =
                "(Ljava/lang/String;)" + descriptor;
            CHECK(fixture.Static(
                      descriptor.c_str(), "valueOf",
                      value_of_descriptor.c_str(),
                      {VmValue::Ref(fixture.vm.NewStringUtf8(name))})
                      .ref == value);
        }

        const auto values_field = fixture.linker.FindFieldRecursive(
            klass, "$VALUES", array_descriptor);
        REQUIRE(values_field.has_value());
        CHECK(fixture.linker.Field(*values_field).access_flags ==
              (kAccPrivate | kAccStatic | kAccFinal | kAccSynthetic));
    }
}

TEST_CASE(
    "DVM-107 framework Pair and sparse containers execute Java on both backends") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto a = f.vm.NewStringUtf8("equal");
        const auto b = f.vm.NewStringUtf8("equal");
        const auto pair =
            f.Static("Landroid/util/Pair;", "create",
                     "(Ljava/lang/Object;Ljava/lang/Object;)Landroid/util/Pair;",
                     {VmValue::Ref(a), VmValue::Ref(VmObjectRef{})})
                .ref;
        const auto other =
            f.New("Landroid/util/Pair;", "(Ljava/lang/Object;Ljava/lang/Object;)V",
                  {VmValue::Ref(b), VmValue::Ref(VmObjectRef{})});
        CHECK(f.On(pair, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(other)})
                  .AsInt() == 1);
        CHECK(f.On(pair, "hashCode", "()I").AsInt() ==
              f.On(other, "hashCode", "()I").AsInt());
        CHECK(
            f.On(pair, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(VmObjectRef{})})
                .AsInt() == 0);
        for (const auto* descriptor :
             {"Landroid/util/SparseArray;", "Landroid/util/LongSparseArray;"}) {
            CAPTURE(descriptor);
            const bool wide =
                std::string(descriptor).find("LongSparse") != std::string::npos;
            const auto sparse = f.New(descriptor, "(I)V", {VmValue::Int(0)});
            const auto roots = f.vm.ProtectReferences(std::array{sparse, a, b, pair});
            const auto key = [&](std::int64_t value) {
                return wide ? VmValue::Long(value)
                            : VmValue::Int(static_cast<std::int32_t>(value));
            };
            const auto put_sig =
                wide ? "(JLjava/lang/Object;)V" : "(ILjava/lang/Object;)V";
            const auto get_sig =
                wide ? "(J)Ljava/lang/Object;" : "(I)Ljava/lang/Object;";
            const auto delete_sig = wide ? "(J)V" : "(I)V";
            const auto max_key = wide ? INT64_C(0x100000001) : INT64_C(0x7fffffff);
            f.On(sparse, "append", put_sig, {key(max_key), VmValue::Ref(pair)});
            f.On(sparse, "append", put_sig, {key(-7), VmValue::Ref(a)});
            f.On(sparse, "put", put_sig, {key(0), VmValue::Ref(b)});
            f.On(sparse, "delete", delete_sig, {key(0)});
            CHECK(f.On(sparse, "size", "()I").AsInt() == 2);
            const auto first_key =
                f.On(sparse, "keyAt", wide ? "(I)J" : "(I)I", {VmValue::Int(0)});
            CHECK((wide ? first_key.AsLong() : first_key.AsInt()) == -7);
            CHECK(
                f.On(sparse, "indexOfValue", "(Ljava/lang/Object;)I", {VmValue::Ref(b)})
                    .AsInt() == -1);
            CHECK(f.On(sparse, "get",
                       wide ? "(JLjava/lang/Object;)Ljava/lang/Object;"
                            : "(ILjava/lang/Object;)Ljava/lang/Object;",
                       {key(0), VmValue::Ref(pair)})
                      .ref == pair);
            const auto clone_sig = std::string("()") + descriptor;
            const auto clone = f.On(sparse, "clone", clone_sig.c_str()).ref;
            const auto clone_root = f.vm.ProtectReferences(std::array{clone});
            f.On(sparse, "clear", "()V");
            CHECK(f.On(clone, "get", get_sig, {key(max_key)}).ref == pair);
            CHECK(f.On(clone, "size", "()I").AsInt() == 2);
            f.On(clone, "removeAt", "(I)V", {VmValue::Int(0)});
            CHECK(f.On(clone, "size", "()I").AsInt() == 1);
        }
        for (const auto* descriptor :
             {"Landroid/util/SparseIntArray;", "Landroid/util/SparseBooleanArray;",
              "Landroid/util/SparseLongArray;"}) {
            CAPTURE(descriptor);
            const std::string name = descriptor;
            const bool wide = name.find("Long") != std::string::npos;
            const bool boolean = name.find("Boolean") != std::string::npos;
            const auto sparse = f.New(descriptor, "(I)V", {VmValue::Int(0)});
            const auto value = wide ? VmValue::Long(INT64_C(0x123456789))
                                    : VmValue::Int(boolean ? 1 : -42);
            const auto put_sig = wide ? "(IJ)V" : boolean ? "(IZ)V" : "(II)V";
            f.On(sparse, "append", put_sig, {VmValue::Int(9), value});
            f.On(sparse, "append", put_sig, {VmValue::Int(-9), value});
            f.On(sparse, "delete", "(I)V", {VmValue::Int(9)});
            CHECK(f.On(sparse, "size", "()I").AsInt() == 1);
            CHECK(f.On(sparse, "keyAt", "(I)I", {VmValue::Int(0)}).AsInt() == -9);
            const auto result = f.On(sparse, "get",
                                     wide      ? "(I)J"
                                     : boolean ? "(I)Z"
                                               : "(I)I",
                                     {VmValue::Int(-9)});
            CHECK((wide ? result.AsLong() : result.AsInt()) ==
                  (wide      ? INT64_C(0x123456789)
                   : boolean ? 1
                             : -42));
            const auto clone_sig = std::string("()") + descriptor;
            const auto clone = f.On(sparse, "clone", clone_sig.c_str()).ref;
            f.On(sparse, "clear", "()V");
            CHECK(f.On(clone, "size", "()I").AsInt() == 1);
        }
    }
}

TEST_CASE(
    "DVM-107 sparse and Pair fields keep children alive and release deleted values") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto sparse = f.New("Landroid/util/SparseArray;");
        const auto roots = f.vm.ProtectReferences(std::array{sparse});
        const auto child = f.New("Ljava/lang/Object;");
        const auto pair =
            f.New("Landroid/util/Pair;", "(Ljava/lang/Object;Ljava/lang/Object;)V",
                  {VmValue::Ref(child), VmValue::Ref(VmObjectRef{})});
        f.On(sparse, "put", "(ILjava/lang/Object;)V",
             {VmValue::Int(3), VmValue::Ref(pair)});
        CHECK(f.vm.MarkReachable().IsMarked(child));
        static_cast<void>(f.vm.CollectGarbage("dvm107-sparse-strong-edge"));
        CHECK(f.On(sparse, "get", "(I)Ljava/lang/Object;", {VmValue::Int(3)}).ref ==
              pair);
        f.On(sparse, "delete", "(I)V", {VmValue::Int(3)});
        CHECK_FALSE(f.vm.MarkReachable().IsMarked(pair));
        CHECK_FALSE(f.vm.MarkReachable().IsMarked(child));
        static_cast<void>(f.vm.CollectGarbage("dvm107-sparse-deleted"));
        CHECK(f.On(sparse, "size", "()I").AsInt() == 0);
    }
}

TEST_CASE("DVM-107 builder CharSequence range append uses UTF16 and validates before "
          "mutation") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        for (const auto* descriptor :
             {"Ljava/lang/StringBuilder;", "Ljava/lang/StringBuffer;"}) {
            const auto builder = f.New(descriptor);
            const auto signature =
                std::string("(Ljava/lang/CharSequence;II)") + descriptor;
            const auto text = f.model.NewString(u"a😀中z");
            CHECK(f.On(builder, "append", signature.c_str(),
                       {VmValue::Ref(text), VmValue::Int(1), VmValue::Int(4)})
                      .ref == builder);
            CHECK(f.model.StringValue(
                      f.On(builder, "toString", "()Ljava/lang/String;").ref) ==
                  u"😀中");
            f.On(builder, "append", signature.c_str(),
                 {VmValue::Ref(builder), VmValue::Int(0), VmValue::Int(2)});
            f.On(builder, "append", signature.c_str(),
                 {VmValue::Ref(VmObjectRef{}), VmValue::Int(1), VmValue::Int(3)});
            CHECK(f.model.StringValue(
                      f.On(builder, "toString", "()Ljava/lang/String;").ref) ==
                  u"😀中😀ul");
            for (const auto range :
                 {std::pair{-1, 1}, std::pair{2, 1}, std::pair{0, 6}}) {
                const auto result =
                    f.OnOutcome(builder, "append", signature.c_str(),
                                {VmValue::Ref(text), VmValue::Int(range.first),
                                 VmValue::Int(range.second)});
                REQUIRE(result.exception.IsValid());
                CHECK(f.linker.Class(result.exception_class).descriptor ==
                      "Ljava/lang/IndexOutOfBoundsException;");
                CHECK(f.model.StringValue(
                          f.On(builder, "toString", "()Ljava/lang/String;").ref) ==
                      u"😀中😀ul");
            }
        }
    }
}

TEST_CASE("DVM-109 UUID deserialization restores private readObject invariants") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto uuid = f.Static("Ljava/util/UUID;", "fromString", "(Ljava/lang/String;)Ljava/util/UUID;", {VmValue::Ref(f.vm.NewStringUtf8("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"))}).ref;
        const auto buffer = f.New("Ljava/io/ByteArrayOutputStream;");
        const auto out = f.New("Ljava/io/ObjectOutputStream;", "(Ljava/io/OutputStream;)V", {VmValue::Ref(buffer)});
        f.On(out, "writeObject", "(Ljava/lang/Object;)V", {VmValue::Ref(uuid)});
        f.On(out, "flush", "()V");
        const auto bytes = f.On(buffer, "toByteArray", "()[B").ref;
        const auto source = f.New("Ljava/io/ByteArrayInputStream;", "([B)V", {VmValue::Ref(bytes)});
        const auto input = f.New("Ljava/io/ObjectInputStream;", "(Ljava/io/InputStream;)V", {VmValue::Ref(source)});
        const auto result = f.OnOutcome(input, "readObject", "()Ljava/lang/Object;");
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        const auto restored = result.value.ref;
        CHECK(f.On(restored, "version", "()I").AsInt() == 1);
        CHECK(f.On(restored, "variant", "()I").AsInt() == 2);
        CHECK(f.On(restored, "timestamp", "()J").AsLong() == INT64_C(130742845922168750));
        CHECK(f.On(restored, "clockSequence", "()I").AsInt() == 0x2765);
        CHECK(f.On(restored, "node", "()J").AsLong() == INT64_C(0x00a0c91e6bf6));
        CHECK(f.On(restored, "hashCode", "()I").AsInt() == f.On(uuid, "hashCode", "()I").AsInt());
        CHECK(f.On(restored, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(uuid)}).AsInt() == 1);
    }
}

TEST_CASE("DVM-108 UUID values and UTF16 substring search follow API19") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto failure = f.New("Ljava/security/ProviderException;", "(Ljava/lang/String;)V",
                                   {VmValue::Ref(f.vm.NewStringUtf8("provider failed"))});
        CHECK(f.linker.IsAssignable(f.linker.ResolveDescriptor("Ljava/lang/RuntimeException;"), f.model.ObjectClass(failure)));
        CHECK(f.vm.StringUtf8(f.On(failure, "getMessage", "()Ljava/lang/String;").ref) == "provider failed");
        const auto text = f.model.NewString(u"a😀中😀z");
        const auto search = [&](std::u16string_view needle, int start) {
            return f.On(text, "indexOf", "(Ljava/lang/String;I)I",
                        {VmValue::Ref(f.model.NewString(std::u16string(needle))), VmValue::Int(start)}).AsInt();
        };
        CHECK(search(u"😀", -1) == 1);
        CHECK(search(u"😀", 2) == 4);
        CHECK(search(u"中", 4) == -1);
        CHECK(search(u"", 100) == 7);
        CHECK(search(u"z", 100) == -1);
        const auto null_search = f.OnOutcome(text, "indexOf", "(Ljava/lang/String;I)I",
                                            {VmValue::Ref(VmObjectRef{}), VmValue::Int(100)});
        REQUIRE(null_search.exception.IsValid());
        CHECK(f.linker.Class(null_search.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto prefix = [&](std::u16string_view value, int start) {
            return f.On(text, "startsWith", "(Ljava/lang/String;I)Z",
                        {VmValue::Ref(f.model.NewString(std::u16string(value))), VmValue::Int(start)}).AsInt();
        };
        CHECK(prefix(u"😀", 1) == 1);
        CHECK(prefix(u"😀", 2) == 0);
        CHECK(prefix(u"", 7) == 1);
        CHECK(prefix(u"", 8) == 0);
        CHECK(prefix(u"", -1) == 0);
        const auto missing_prefix = f.OnOutcome(text, "startsWith", "(Ljava/lang/String;I)Z",
                                               {VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
        REQUIRE(missing_prefix.exception.IsValid());
        CHECK(f.linker.Class(missing_prefix.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto zero = f.New("Ljava/util/UUID;", "(JJ)V", {VmValue::Long(0), VmValue::Long(0)});
        const auto negative = f.New("Ljava/util/UUID;", "(JJ)V", {VmValue::Long(INT64_MIN), VmValue::Long(-1)});
        const auto equal = f.New("Ljava/util/UUID;", "(JJ)V", {VmValue::Long(INT64_MIN), VmValue::Long(-1)});
        CHECK(f.On(negative, "getMostSignificantBits", "()J").AsLong() == INT64_MIN);
        CHECK(f.On(negative, "getLeastSignificantBits", "()J").AsLong() == -1);
        CHECK(f.vm.StringUtf8(f.On(negative, "toString", "()Ljava/lang/String;").ref) == "80000000-0000-0000-ffff-ffffffffffff");
        CHECK(f.On(negative, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(equal)}).AsInt() == 1);
        CHECK(f.On(negative, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(zero)}).AsInt() == 0);
        CHECK(f.On(negative, "hashCode", "()I").AsInt() == f.On(equal, "hashCode", "()I").AsInt());
        CHECK(f.On(negative, "compareTo", "(Ljava/util/UUID;)I", {VmValue::Ref(zero)}).AsInt() < 0);
        CHECK(f.On(equal, "compareTo", "(Ljava/util/UUID;)I", {VmValue::Ref(negative)}).AsInt() == 0);
        CHECK(f.On(negative, "variant", "()I").AsInt() == 7);
        CHECK(f.On(zero, "variant", "()I").AsInt() == 0);
        const auto parse = *f.linker.FindDirectMethod(f.linker.ResolveDescriptor("Ljava/util/UUID;"), "fromString", "(Ljava/lang/String;)Ljava/util/UUID;");
        for (const auto* invalid : {"", "a-b-c-d", "a-b-c-d-e-f", "invalid-1-1-1-1"}) {
            const auto result = f.vm.Call(parse, std::array{VmValue::Ref(f.vm.NewStringUtf8(invalid))});
            REQUIRE(result.exception.IsValid());
            CHECK(f.linker.IsAssignable(f.linker.ResolveDescriptor("Ljava/lang/IllegalArgumentException;"), result.exception_class));
        }
    }
}

TEST_CASE("DVM-143 method lookup ignores unrelated unavailable signature types") {
    auto builder = IntrinsicClassBuilder::Class(
        "Ltest/SelectiveLookup;", "Ljava/lang/Object;");
    builder.Constructor("()V", [](IntrinsicContext&) {
        return VmValue::Void();
    });
    builder.VirtualMethod("wanted", "()I", [](IntrinsicContext&) {
        return VmValue::Int(7);
    });
    builder.VirtualMethod(
        "unrelated", "()Lmissing/UnavailableReturnType;",
        [](IntrinsicContext&) { return VmValue::Ref(VmObjectRef{}); });
    const std::vector<IntrinsicClassDecl> extras{
        std::move(builder).Build()};

    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, extras);
        const auto represented = f.model.ClassObject(
            f.linker.ResolveDescriptor("Ltest/SelectiveLookup;"));
        const auto lookup = [&](const char* operation) {
            return f.OnOutcome(
                represented, operation,
                "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;",
                {VmValue::Ref(f.vm.NewStringUtf8("wanted")),
                 VmValue::Ref(VmObjectRef{})});
        };

        const auto exact = f.vm.Reflection().FindDeclaredMethodByDescriptor(
            f.linker.ResolveDescriptor("Ltest/SelectiveLookup;"), "wanted", "()I");
        REQUIRE(exact.has_value());
        CHECK(f.linker.Method(exact->method).descriptor == "()I");
        CHECK_FALSE(f.vm.Reflection().FindDeclaredMethodByDescriptor(
            exact->declaring_class, "wanted", "()J").has_value());
        const auto public_method = lookup("getMethod");
        REQUIRE_MESSAGE(!public_method.exception.IsValid(),
                        public_method.exception_message);
        const auto declared_method = lookup("getDeclaredMethod");
        REQUIRE_MESSAGE(!declared_method.exception.IsValid(),
                        declared_method.exception_message);
        CHECK(f.vm.StringUtf8(f.On(public_method.value.ref, "getName",
                                   "()Ljava/lang/String;").ref) == "wanted");
        CHECK(f.vm.Reflection().MethodMetadata(public_method.value.ref).method ==
              f.vm.Reflection().MethodMetadata(declared_method.value.ref).method);
    }
}

TEST_CASE("small framework Java values execute from BootDex") {
    constexpr std::array migrated{
        "Landroid/graphics/Point;",
        "Landroid/graphics/Rect;",
        "Landroid/util/AndroidException;",
        "Landroid/util/AndroidRuntimeException;",
        "Landroid/util/ArraySet;",
        "Landroid/util/Base64DataException;",
        "Landroid/util/LruCache;",
        "Landroid/util/MathUtils;",
        "Landroid/util/NoSuchPropertyException;",
        "Landroid/util/Patterns;",
        "Landroid/util/Pools$SimplePool;",
        "Landroid/util/Property;",
        "Landroid/util/TimeFormatException;",
    };
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        for (const auto* descriptor : migrated) {
            CAPTURE(descriptor);
            CHECK(f.linker.Class(f.linker.ResolveDescriptor(descriptor)).is_boot_dex);
        }

        const auto first = f.vm.NewStringUtf8("first");
        const auto second = f.vm.NewStringUtf8("second");
        const auto set = f.New("Landroid/util/ArraySet;");
        CHECK(f.On(set, "add", "(Ljava/lang/Object;)Z",
                   {VmValue::Ref(first)}).AsInt() == 1);
        CHECK(f.On(set, "add", "(Ljava/lang/Object;)Z",
                   {VmValue::Ref(first)}).AsInt() == 0);
        CHECK(f.On(set, "contains", "(Ljava/lang/Object;)Z",
                   {VmValue::Ref(first)}).AsInt() == 1);

        const auto cache = f.New("Landroid/util/LruCache;", "(I)V",
                                 {VmValue::Int(1)});
        CHECK_FALSE(f.On(cache, "put",
                         "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                         {VmValue::Ref(first), VmValue::Ref(second)}).ref.IsValid());
        CHECK(f.On(cache, "get", "(Ljava/lang/Object;)Ljava/lang/Object;",
                   {VmValue::Ref(first)}).ref == second);

        const auto pool = f.New("Landroid/util/Pools$SimplePool;", "(I)V",
                                {VmValue::Int(1)});
        CHECK(f.On(pool, "release", "(Ljava/lang/Object;)Z",
                   {VmValue::Ref(first)}).AsInt() == 1);
        CHECK(f.On(pool, "acquire", "()Ljava/lang/Object;").ref == first);

        const auto point = f.New("Landroid/graphics/Point;", "(II)V",
                                 {VmValue::Int(3), VmValue::Int(4)});
        f.On(point, "negate", "()V");
        CHECK(f.On(point, "equals", "(II)Z",
                   {VmValue::Int(-3), VmValue::Int(-4)}).AsInt() == 1);
        const auto rect = f.New("Landroid/graphics/Rect;", "(IIII)V",
                                {VmValue::Int(1), VmValue::Int(2),
                                 VmValue::Int(6), VmValue::Int(9)});
        CHECK(f.On(rect, "width", "()I").AsInt() == 5);
        CHECK(f.On(rect, "contains", "(II)Z",
                   {VmValue::Int(3), VmValue::Int(4)}).AsInt() == 1);
        CHECK(f.Static("Landroid/util/MathUtils;", "constrain", "(III)I",
                       {VmValue::Int(12), VmValue::Int(0), VmValue::Int(10)})
                  .AsInt() == 10);

    }
}

TEST_CASE("ClipData value objects execute from BootDex") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        for (const auto* descriptor : {
                 "Landroid/content/ClipData;",
                 "Landroid/content/ClipData$Item;",
                 "Landroid/content/ClipDescription;",
             }) {
            CAPTURE(descriptor);
            CHECK(f.linker.Class(f.linker.ResolveDescriptor(descriptor)).is_boot_dex);
        }

        const auto label = f.vm.NewStringUtf8("label");
        const auto first_text = f.vm.NewStringUtf8("first");
        const auto clip = f.Static(
            "Landroid/content/ClipData;", "newPlainText",
            "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)"
            "Landroid/content/ClipData;",
            {VmValue::Ref(label), VmValue::Ref(first_text)}).ref;
        CHECK(f.On(clip, "getItemCount", "()I").AsInt() == 1);
        const auto description = f.On(
            clip, "getDescription", "()Landroid/content/ClipDescription;").ref;
        CHECK(f.On(description, "getLabel", "()Ljava/lang/CharSequence;").ref ==
              label);
        CHECK(f.On(description, "getMimeTypeCount", "()I").AsInt() == 1);
        CHECK(f.vm.StringUtf8(f.On(description, "getMimeType", "(I)Ljava/lang/String;",
                                   {VmValue::Int(0)}).ref) == "text/plain");
        CHECK(f.On(description, "hasMimeType", "(Ljava/lang/String;)Z",
                   {VmValue::Ref(f.vm.NewStringUtf8("text/*"))}).AsInt() == 1);

        const auto first = f.On(
            clip, "getItemAt", "(I)Landroid/content/ClipData$Item;",
            {VmValue::Int(0)}).ref;
        CHECK(f.On(first, "getText", "()Ljava/lang/CharSequence;").ref == first_text);
        CHECK_FALSE(f.On(first, "getIntent", "()Landroid/content/Intent;").ref.IsValid());
        CHECK_FALSE(f.On(first, "getUri", "()Landroid/net/Uri;").ref.IsValid());

        const auto second_text = f.vm.NewStringUtf8("second");
        const auto second = f.New(
            "Landroid/content/ClipData$Item;", "(Ljava/lang/CharSequence;)V",
            {VmValue::Ref(second_text)});
        f.On(clip, "addItem", "(Landroid/content/ClipData$Item;)V",
             {VmValue::Ref(second)});
        CHECK(f.On(clip, "getItemCount", "()I").AsInt() == 2);
        CHECK(f.On(clip, "getItemAt", "(I)Landroid/content/ClipData$Item;",
                   {VmValue::Int(1)}).ref == second);

        const auto intent = f.New("Landroid/content/Intent;");
        CHECK(f.linker.Class(f.model.ObjectClass(intent)).is_boot_dex);
        const auto package_name = f.vm.NewStringUtf8("org.example");
        CHECK(f.On(intent, "setPackage",
                   "(Ljava/lang/String;)Landroid/content/Intent;",
                   {VmValue::Ref(package_name)}).ref == intent);
        f.On(intent, "setClipData", "(Landroid/content/ClipData;)V",
             {VmValue::Ref(clip)});
        const auto bounds = f.New("Landroid/graphics/Rect;", "(IIII)V",
                                  {VmValue::Int(1), VmValue::Int(2),
                                   VmValue::Int(6), VmValue::Int(9)});
        f.On(intent, "setSourceBounds", "(Landroid/graphics/Rect;)V",
             {VmValue::Ref(bounds)});
        const auto copy = f.New("Landroid/content/Intent;",
                                "(Landroid/content/Intent;)V",
                                {VmValue::Ref(intent)});
        CHECK(f.On(copy, "getPackage", "()Ljava/lang/String;").ref == package_name);
        CHECK(f.On(copy, "getClipData", "()Landroid/content/ClipData;").ref != clip);
        CHECK(f.On(f.On(copy, "getClipData", "()Landroid/content/ClipData;").ref,
                   "getItemCount", "()I").AsInt() == 2);
        const auto copied_bounds =
            f.On(copy, "getSourceBounds", "()Landroid/graphics/Rect;").ref;
        CHECK(copied_bounds != bounds);
        CHECK(f.On(copied_bounds, "width", "()I").AsInt() == 5);

        const auto roots = f.vm.ProtectReferences(std::array{clip, intent, copy});
        static_cast<void>(f.vm.CollectGarbage("clip-data-values"));
        CHECK(f.On(clip, "getItemAt", "(I)Landroid/content/ClipData$Item;",
                   {VmValue::Int(1)}).ref == second);
    }
}

TEST_CASE("DVM-151 BootDex builders own UTF16 fields capacity and immutable snapshots") {
    for (const bool bridge : {false, true})
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, bridge);
        for (const auto* owner : {"Ljava/lang/StringBuilder;", "Ljava/lang/StringBuffer;"}) {
            CAPTURE(owner);
            const auto b = f.New(owner);
            const auto root = f.vm.ProtectReferences(std::array{b});
            const auto invoke = [&](const char* name, const std::string& sig, std::vector<VmValue> args = {}) {
                return f.On(b, name, sig.c_str(), args);
            };
            const auto text = [&] { return f.model.StringValue(invoke("toString", "()Ljava/lang/String;").ref); };
            CHECK(invoke("capacity", "()I").AsInt() == 16);
            const auto append = std::string("(Ljava/lang/String;)") + owner;
            const auto literal = f.vm.NewStringUtf8("/getLatest/unknown/0/terms-1.9-137%2Ctermsapi-1.2/com.popcap.pvz_na/Android/false/0/ageSensitive");
            CHECK(invoke("append", append, {VmValue::Ref(literal)}).ref == b);
            CHECK(f.vm.StringUtf8(invoke("substring", "(II)Ljava/lang/String;", {VmValue::Int(0), VmValue::Int(3)}).ref) == "/ge");
            CHECK(f.vm.StringUtf8(invoke("substring", "(I)Ljava/lang/String;", {VmValue::Int(0)}).ref) == f.vm.StringUtf8(literal));
            CHECK(f.vm.StringUtf8(invoke("subSequence", "(II)Ljava/lang/CharSequence;", {VmValue::Int(0), VmValue::Int(3)}).ref) == "/ge");
            invoke("setLength", "(I)V", {VmValue::Int(0)});
            invoke("trimToSize", "()V");
            CHECK(invoke("capacity", "()I").AsInt() == 0);
            invoke("ensureCapacity", "(I)V", {VmValue::Int(16)});
            invoke("append", append, {VmValue::Ref(f.vm.NewStringUtf8("abcdefghijklmnop"))});
            const auto snapshot = invoke("toString", "()Ljava/lang/String;").ref;
            const auto snapshot_root = f.vm.ProtectReferences(std::array{snapshot});
            const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(b), "value", "[C");
            REQUIRE(field.has_value());
            const auto backing = VmObjectRef(f.model.InstanceSlots(b)[f.linker.Field(*field).slot].bits);
            CHECK(f.vm.MarkReachable().IsMarked(backing));
            invoke("setCharAt", "(IC)V", {VmValue::Int(0), VmValue::Int('Z')});
            CHECK(f.model.StringValue(snapshot) == u"abcdefghijklmnop");
            const auto detached = VmObjectRef(f.model.InstanceSlots(b)[f.linker.Field(*field).slot].bits);
            CHECK(detached != backing);
            static_cast<void>(f.vm.CollectGarbage());
            CHECK(text() == u"Zbcdefghijklmnop");
            invoke("append", std::string("(C)") + owner, {VmValue::Int('q')});
            CHECK(invoke("capacity", "()I").AsInt() == 26); // API 19 append growth: 1.5x + 2
            invoke("ensureCapacity", "(I)V", {VmValue::Int(27)});
            CHECK(invoke("capacity", "()I").AsInt() == 54); // ensureCapacity: 2x + 2
            invoke("setLength", "(I)V", {VmValue::Int(1)});
            invoke("setLength", "(I)V", {VmValue::Int(3)});
            CHECK(text() == std::u16string({u'Z', 0, 0}));
            invoke("setLength", "(I)V", {VmValue::Int(0)});
            invoke("append", append, {VmValue::Ref(f.model.NewString(u"A😀B"))});
            CHECK(invoke("length", "()I").AsInt() == 4);
            CHECK(invoke("charAt", "(I)C", {VmValue::Int(1)}).AsInt() == 0xd83d);
            CHECK(invoke("codePointAt", "(I)I", {VmValue::Int(1)}).AsInt() == 0x1f600);
            CHECK(invoke("codePointBefore", "(I)I", {VmValue::Int(3)}).AsInt() == 0x1f600);
            CHECK(invoke("codePointCount", "(II)I", {VmValue::Int(0), VmValue::Int(4)}).AsInt() == 3);
            CHECK(invoke("offsetByCodePoints", "(II)I", {VmValue::Int(0), VmValue::Int(2)}).AsInt() == 3);
            invoke("reverse", std::string("()") + owner);
            CHECK(text() == u"B😀A");
            invoke("insert", std::string("(ILjava/lang/String;)") + owner, {VmValue::Int(1), VmValue::Ref(f.vm.NewStringUtf8("xy"))});
            invoke("delete", std::string("(II)") + owner, {VmValue::Int(1), VmValue::Int(3)});
            invoke("replace", std::string("(IILjava/lang/String;)") + owner, {VmValue::Int(0), VmValue::Int(1), VmValue::Ref(f.vm.NewStringUtf8("C"))});
            CHECK(text() == u"C😀A");
            invoke("appendCodePoint", std::string("(I)") + owner, {VmValue::Int(0x1f601)});
            CHECK(text() == u"C😀A😁");
            const auto unchanged = text();
            const auto bad = f.OnOutcome(b, "substring", "(II)Ljava/lang/String;", {VmValue::Int(-1), VmValue::Int(2)});
            REQUIRE(bad.exception.IsValid());
            CHECK(f.linker.Class(bad.exception_class).descriptor == "Ljava/lang/StringIndexOutOfBoundsException;");
            CHECK(text() == unchanged);
        }
    }
}

TEST_CASE("DVM-151 builder integer and floating appends execute original conversion classes") {
    for (const bool bridge : {false, true})
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, bridge);
        for (const auto* owner : {"Ljava/lang/StringBuilder;", "Ljava/lang/StringBuffer;"}) {
            const auto b = f.New(owner);
            const auto root = f.vm.ProtectReferences(std::array{b});
            const auto check = [&](const char* type, VmValue value, const char* expected) {
                f.On(b, "setLength", "(I)V", {VmValue::Int(0)});
                const auto sig = std::string("(") + type + ")" + owner;
                CHECK(f.On(b, "append", sig.c_str(), {value}).ref == b);
                CHECK(f.vm.StringUtf8(f.On(b, "toString", "()Ljava/lang/String;").ref) == expected);
            };
            check("I", VmValue::Int(INT32_MIN), "-2147483648");
            check("I", VmValue::Int(INT32_MAX), "2147483647");
            check("J", VmValue::Long(INT64_MIN), "-9223372036854775808");
            check("J", VmValue::Long(INT64_MAX), "9223372036854775807");
            check("I", VmValue::Int(0), "0");
            check("F", VmValue::Float(1.5f), "1.5");
            check("F", VmValue::Float(std::bit_cast<float>(std::uint32_t{1})), "1.4E-45");
            check("F", VmValue::Float(std::bit_cast<float>(std::uint32_t{0x7f7fffff})), "3.4028235E38");
            check("D", VmValue::Double(-0.0), "-0.0");
            check("D", VmValue::Double(1e100), "1.0E100");
            check("D", VmValue::Double(-1e100), "-1.0E100");
            check("D", VmValue::Double(1.234123412431233E107), "1.234123412431233E107");
            check("D", VmValue::Double(std::bit_cast<double>(std::uint64_t{0x7fefffffffffffff})), "1.7976931348623157E308");
            check("D", VmValue::Double(std::bit_cast<double>(std::uint64_t{0x0010000000000000})), "2.2250738585072014E-308");
            check("D", VmValue::Double(std::bit_cast<double>(std::uint64_t{2})), "1.0E-323");
            check("D", VmValue::Double(std::bit_cast<double>(std::uint64_t{1})), "4.9E-324");
            check("D", VmValue::Double(std::bit_cast<double>(std::uint64_t{0x7ff0000000000000})), "Infinity");
            check("D", VmValue::Double(std::bit_cast<double>(std::uint64_t{0x7ff8000000000000})), "NaN");
        }
    }
}

TEST_CASE("DVM-151 builders retain constructor null array and bridge contracts") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        for (const auto* owner : {"Ljava/lang/StringBuilder;", "Ljava/lang/StringBuffer;"}) {
            const auto b = f.New(owner, "(I)V", {VmValue::Int(0)});
            const auto root = f.vm.ProtectReferences(std::array{b});
            const auto text = [&] { return f.model.StringValue(f.On(b, "toString", "()Ljava/lang/String;").ref); };
            const auto expect_exception = [&](const VmCallOutcome& result, const char* exception) {
                REQUIRE(result.exception.IsValid());
                CHECK(f.linker.Class(result.exception_class).descriptor == exception);
            };
            const auto raw = f.vm.NewIntrinsicInstance(owner);
            expect_exception(f.StaticOutcome(owner, "<init>", "(I)V", {VmValue::Ref(raw), VmValue::Int(-1)}), "Ljava/lang/NegativeArraySizeException;");
            for (auto ctor : {"(Ljava/lang/String;)V", "(Ljava/lang/CharSequence;)V"}) {
                const auto object = f.vm.NewIntrinsicInstance(owner);
                expect_exception(f.StaticOutcome(owner, "<init>", ctor, {VmValue::Ref(object), VmValue::Ref(VmObjectRef{})}), "Ljava/lang/NullPointerException;");
            }
            CHECK(f.On(b, "append", "(Ljava/lang/CharSequence;)Ljava/lang/Appendable;", {VmValue::Ref(VmObjectRef{})}).ref == b);
            CHECK(text() == u"null");
            const auto chars = f.On(f.model.NewString(u"x😀y"), "toCharArray", "()[C").ref;
            const auto chars_root = f.vm.ProtectReferences(std::array{chars});
            const auto append_array = std::string("([CII)") + owner;
            f.On(b, "append", append_array.c_str(), {VmValue::Ref(chars), VmValue::Int(1), VmValue::Int(2)});
            CHECK(text() == u"null😀");
            const auto before = text();
            expect_exception(f.OnOutcome(b, "append", append_array.c_str(), {VmValue::Ref(chars), VmValue::Int(3), VmValue::Int(2)}), "Ljava/lang/ArrayIndexOutOfBoundsException;");
            CHECK(text() == before);
            f.On(b, "getChars", "(II[CI)V", {VmValue::Int(4), VmValue::Int(6), VmValue::Ref(chars), VmValue::Int(0)});
            CHECK(f.model.GetPrimitiveElement(chars, 0) == 0xd83d);
            CHECK(f.model.GetPrimitiveElement(chars, 1) == 0xde00);
            CHECK(f.On(b, "indexOf", "(Ljava/lang/String;)I", {VmValue::Ref(f.vm.NewStringUtf8("ll"))}).AsInt() == 2);
            CHECK(f.On(b, "lastIndexOf", "(Ljava/lang/String;)I", {VmValue::Ref(f.vm.NewStringUtf8("l"))}).AsInt() == 3);
            const auto copied = f.New(owner, "(Ljava/lang/CharSequence;)V", {VmValue::Ref(b)});
            CHECK(f.model.StringValue(f.On(copied, "toString", "()Ljava/lang/String;").ref) == before);
            f.On(b, "deleteCharAt", (std::string("(I)") + owner).c_str(), {VmValue::Int(4)});
            CHECK(f.On(b, "charAt", "(I)C", {VmValue::Int(4)}).AsInt() == 0xde00); // code unit, not a code point
            expect_exception(f.OnOutcome(b, "setLength", "(I)V", {VmValue::Int(-1)}), "Ljava/lang/StringIndexOutOfBoundsException;");
            expect_exception(f.OnOutcome(b, "appendCodePoint", (std::string("(I)") + owner).c_str(), {VmValue::Int(0x110000)}), "Ljava/lang/IllegalArgumentException;");
        }
    }
}

TEST_CASE("DVM-266 ContentValues clinit constructs the ROM Parcelable creator") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, "platform_tables.dex", true);
        const auto creator =
            f.Static("Lfixture/PlatformTables;", "contentValuesCreator", "()Landroid/os/Parcelable$Creator;").ref;
        REQUIRE(creator.IsValid());
        CHECK(f.linker.Class(f.model.ObjectClass(creator)).descriptor == "Landroid/content/ContentValues$1;");
        CHECK(f.linker.Class(f.model.ObjectClass(creator)).is_boot_dex);
        CHECK(f.Static("Lfixture/PlatformTables;", "contentValuesCreator", "()Landroid/os/Parcelable$Creator;").ref ==
              creator);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-267 ROM Enum subclass executes values valueOf and ordinal") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, "platform_tables.dex", true);
        CHECK(f.Static("Lfixture/PlatformTables;", "romEnum", "()I").AsInt() == 31);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-267 ROM overlay Exception subclass preserves throw catch and message") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, "platform_tables.dex", true);
        const auto message = f.Static("Lfixture/PlatformTables;", "romException", "()Ljava/lang/String;").ref;
        CHECK(f.vm.StringUtf8(message) == "ROM exception");
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-268 DEX Location owns provider and time") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::android_value::AndroidValueVm f(backend, {}, false, "rom_java_replacement.dex");
        CHECK(f.Static("Ltest/RomJavaReplacement;", "location", "()J").AsLong() == 42);
        CHECK(f.linker.Class(f.linker.ResolveDescriptor("Landroid/location/Location;")).is_boot_dex);
    }
}
