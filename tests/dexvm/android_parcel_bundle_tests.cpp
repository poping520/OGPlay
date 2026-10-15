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

TEST_CASE("Binder BootDex Bundle Parcel uses the byte protocol and reconstructs values") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto parcel = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto parcel_root = f.vm.ProtectReferences(std::array{parcel});
        const auto bundle = f.New("Landroid/os/Bundle;");
        const auto key = f.vm.NewStringUtf8("bytes");
        const auto key_root = f.vm.ProtectReferences(std::array{key});
        const auto bytes = f.Bytes("snapshot child");
        f.On(bundle, "putSerializable", "(Ljava/lang/String;Ljava/io/Serializable;)V",
             {VmValue::Ref(key), VmValue::Ref(bytes)});
        f.On(bundle, "writeToParcel", "(Landroid/os/Parcel;I)V",
             {VmValue::Ref(parcel), VmValue::Int(0)});
        f.On(bundle, "clear", "()V");
        CHECK_FALSE(f.vm.MarkReachable().IsMarked(bytes));
        CHECK_FALSE(f.vm.MarkReachable().IsMarked(bundle));
        static_cast<void>(f.vm.CollectGarbage("dvm117-parcel-snapshot"));
        f.On(parcel, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto type = f.linker.ResolveDescriptor("Landroid/os/Bundle;");
        const auto creator_field = f.linker.FindFieldRecursive(type, "CREATOR", "Landroid/os/Parcelable$Creator;");
        REQUIRE(creator_field.has_value());
        const auto creator = VmObjectRef(f.linker.Class(type).static_storage[f.linker.Field(*creator_field).slot]);
        const auto copy = f.On(creator, "createFromParcel", "(Landroid/os/Parcel;)Ljava/lang/Object;",
                               {VmValue::Ref(parcel)}).ref;
        const auto copy_root = f.vm.ProtectReferences(std::array{copy});
        CHECK(f.BytesOf(f.On(copy, "getByteArray", "(Ljava/lang/String;)[B", {VmValue::Ref(key)}).ref) ==
              "snapshot child");
        const auto array = f.On(creator, "newArray", "(I)[Ljava/lang/Object;", {VmValue::Int(2)}).ref;
        CHECK(f.linker.Class(f.model.ObjectClass(array)).descriptor == "[Landroid/os/Bundle;");
        f.On(copy, "clear", "()V");
        f.On(parcel, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto another = f.On(parcel, "readBundle", "()Landroid/os/Bundle;").ref;
        const auto another_bytes = f.On(
            another, "getSerializable",
            "(Ljava/lang/String;)Ljava/io/Serializable;",
            {VmValue::Ref(key)}).ref;
        CHECK(f.BytesOf(another_bytes) == "snapshot child");
        f.On(parcel, "recycle", "()V");
        // Retire the nested Bundle copy constructor's last Java return root.
        CHECK(f.On(copy, "isEmpty", "()Z").AsInt() == 1);
    }
}

TEST_CASE("Binder byte Parcel preserves API19 positions append and pure marshalling") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto source = f.Static(
            "Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto target = f.Static(
            "Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto roots = f.vm.ProtectReferences(std::array{source, target});
        const auto text = f.model.NewString(u"Parcel \U0001F331");
        f.On(source, "writeInt", "(I)V", {VmValue::Int(0x12345678)});
        f.On(source, "writeString", "(Ljava/lang/String;)V", {VmValue::Ref(text)});
        const auto size = f.On(source, "dataSize", "()I").AsInt();
        CHECK(size > 8);
        CHECK((size & 3) == 0);
        f.On(target, "appendFrom", "(Landroid/os/Parcel;II)V",
             {VmValue::Ref(source), VmValue::Int(0), VmValue::Int(size)});
        f.On(source, "recycle", "()V");
        f.On(target, "setDataPosition", "(I)V", {VmValue::Int(0)});
        CHECK(f.On(target, "readInt", "()I").AsInt() == 0x12345678);
        CHECK(f.model.StringValue(
                  f.On(target, "readString", "()Ljava/lang/String;").ref) ==
              u"Parcel \U0001F331");
        f.On(target, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto wire = f.On(target, "marshall", "()[B").ref;
        CHECK(f.model.ArrayLength(wire) == size);

        const auto token = f.vm.NewStringUtf8("example.echo");
        const auto wrong = f.vm.NewStringUtf8("example.other");
        f.On(target, "setDataSize", "(I)V", {VmValue::Int(0)});
        f.On(target, "setDataPosition", "(I)V", {VmValue::Int(0)});
        f.On(target, "writeInterfaceToken", "(Ljava/lang/String;)V",
             {VmValue::Ref(token)});
        f.On(target, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto mismatch = f.OnOutcome(
            target, "enforceInterface", "(Ljava/lang/String;)V",
            {VmValue::Ref(wrong)});
        REQUIRE(mismatch.exception.IsValid());
        CHECK(f.linker.Class(mismatch.exception_class).descriptor ==
              "Ljava/lang/SecurityException;");
    }
}

TEST_CASE("DVM-86 Parcel Bundle and bounded services share session state") {
    AndroidValueVm fixture;
    const auto parcel = fixture.Static(
        "Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
    fixture.On(parcel, "writeInt", "(I)V", {VmValue::Int(37)});
    fixture.On(parcel, "writeString", "(Ljava/lang/String;)V",
               {VmValue::Ref(fixture.vm.NewStringUtf8("value"))});
    CHECK(fixture.On(parcel, "dataSize", "()I").AsInt() > 4);
    fixture.On(parcel, "setDataPosition", "(I)V", {VmValue::Int(0)});
    CHECK(fixture.On(parcel, "readInt", "()I").AsInt() == 37);
    CHECK(fixture.vm.StringUtf8(
              fixture.On(parcel, "readString", "()Ljava/lang/String;").ref) ==
          "value");

    const auto bundle = fixture.New("Landroid/os/Bundle;");
    fixture.On(bundle, "putString", "(Ljava/lang/String;Ljava/lang/String;)V",
        {VmValue::Ref(fixture.vm.NewStringUtf8("key")),
         VmValue::Ref(fixture.vm.NewStringUtf8("stored"))});
    const auto parcel2 = fixture.Static(
        "Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
    fixture.On(parcel2, "writeBundle", "(Landroid/os/Bundle;)V", {VmValue::Ref(bundle)});
    fixture.On(bundle, "putString", "(Ljava/lang/String;Ljava/lang/String;)V",
        {VmValue::Ref(fixture.vm.NewStringUtf8("key")),
         VmValue::Ref(fixture.vm.NewStringUtf8("mutated"))});
    fixture.On(parcel2, "setDataPosition", "(I)V", {VmValue::Int(0)});
    const auto copy = fixture.On(parcel2, "readBundle", "()Landroid/os/Bundle;").ref;
    CHECK(copy != bundle);
    CHECK(fixture.vm.StringUtf8(fixture.On(
              copy, "getString", "(Ljava/lang/String;)Ljava/lang/String;",
              {VmValue::Ref(fixture.vm.NewStringUtf8("key"))}).ref) == "stored");

    const auto power = fixture.New("Landroid/os/PowerManager;");
    const auto lock = fixture.On(power, "newWakeLock",
        "(ILjava/lang/String;)Landroid/os/PowerManager$WakeLock;",
        {VmValue::Int(1), VmValue::Ref(fixture.vm.NewStringUtf8("test"))}).ref;
    fixture.On(lock, "acquire", "()V");
    CHECK(fixture.On(lock, "isHeld", "()Z").AsInt() == 1);
    fixture.On(lock, "release", "()V");
    CHECK(fixture.On(lock, "isHeld", "()Z").AsInt() == 0);

    const auto vibrator = fixture.New("Landroid/os/Vibrator;");
    fixture.On(vibrator, "vibrate", "(J)V", {VmValue::Long(250)});
    CHECK(fixture.context->last_vibration_millis == 250);
    fixture.On(vibrator, "cancel", "()V");
    CHECK(fixture.context->last_vibration_millis == 0);

    const auto android_context = fixture.New("Landroid/content/Context;");
    const auto service_name = fixture.vm.NewStringUtf8("power");
    const auto service1 = fixture.On(android_context, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;", {VmValue::Ref(service_name)}).ref;
    const auto service2 = fixture.On(android_context, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;", {VmValue::Ref(service_name)}).ref;
    CHECK(service1.IsValid());
    CHECK(service1 == service2);
}

TEST_CASE("DVM-86 value side tables sweep with their guest owners") {
    AndroidValueVm fixture;
    static_cast<void>(fixture.New("Landroid/util/SparseArray;"));
    static_cast<void>(fixture.New("Landroid/graphics/Path;"));
    static_cast<void>(fixture.Static(
        "Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;"));
    const auto power = fixture.New("Landroid/os/PowerManager;");
    static_cast<void>(fixture.On(power, "newWakeLock",
        "(ILjava/lang/String;)Landroid/os/PowerManager$WakeLock;",
        {VmValue::Int(1), VmValue::Ref(fixture.vm.NewStringUtf8("sweep"))}));
    REQUIRE_FALSE(fixture.context->paths.empty());
    REQUIRE_FALSE(fixture.context->parcel_backings.empty());
    REQUIRE_FALSE(fixture.context->wake_locks.empty());
    const auto result = fixture.vm.CollectGarbage("dvm86-value-state");
    CHECK(result.freed_objects >= 4U);
    CHECK(fixture.context->paths.empty());
    // Parcel.obtain() may retain the recycled owner in the API 19 Java pool;
    // its backing is released by recycle/nativeFreeBuffer and is safe to reuse.
    CHECK(fixture.context->parcel_backings.size() <= 1U);
    CHECK(fixture.context->wake_locks.empty());
}

TEST_CASE("DVM-86 rooted Bundle traces byte arrays and Parcelable identities") {
    AndroidValueVm fixture;
    const auto bundle = fixture.New("Landroid/os/Bundle;");
    const auto bytes = fixture.Bytes("kept");
    const auto key = fixture.vm.NewStringUtf8("payload");
    fixture.On(bundle, "putByteArray", "(Ljava/lang/String;[B)V",
               {VmValue::Ref(key), VmValue::Ref(bytes)});
    fixture.vm.SetGcIntegration(
        {{}, {}, [bundle](const VmRootVisitor& visit) { visit(bundle); }});

    const auto marked = fixture.vm.MarkReachable();
    CHECK(marked.IsMarked(bundle));
    CHECK(marked.IsMarked(bytes));
    static_cast<void>(fixture.vm.CollectGarbage("dvm86-bundle-edge"));
    CHECK(fixture.On(bundle, "getByteArray", "(Ljava/lang/String;)[B",
                     {VmValue::Ref(fixture.vm.NewStringUtf8("payload"))}).ref ==
          bytes);
    CHECK(fixture.BytesOf(bytes) == "kept");
}

TEST_CASE("DVM-144 Parcel exception headers preserve Java protocol and reject violations") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto p = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto roots = f.vm.ProtectReferences(std::array{p});
        f.On(p, "writeNoException", "()V");
        CHECK(f.On(p, "dataPosition", "()I").AsInt() == 4);
        f.On(p, "setDataPosition", "(I)V", {VmValue::Int(0)});
        f.On(p, "readException", "()V");
        for (const auto type : {"Ljava/lang/SecurityException;", "Ljava/lang/IllegalArgumentException;",
                                "Ljava/lang/NullPointerException;", "Ljava/lang/IllegalStateException;",
                                "Landroid/os/BadParcelableException;"}) {
            f.On(p, "setDataSize", "(I)V", {VmValue::Int(0)});
            const auto error = f.New(type, "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("wire error"))});
            f.On(p, "writeException", "(Ljava/lang/Exception;)V", {VmValue::Ref(error)});
            f.On(p, "setDataPosition", "(I)V", {VmValue::Int(0)});
            const auto result = f.OnOutcome(p, "readException", "()V");
            REQUIRE(result.exception.IsValid());
            CHECK(f.linker.Class(result.exception_class).descriptor == type);
            CHECK(f.vm.StringUtf8(f.On(result.exception, "getMessage", "()Ljava/lang/String;").ref) == "wire error");
        }
        f.On(p, "setDataSize", "(I)V", {VmValue::Int(0)});
        f.On(p, "writeInt", "(I)V", {VmValue::Int(-128)});
        f.On(p, "writeInt", "(I)V", {VmValue::Int(4)});
        f.On(p, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto violation = f.OnOutcome(p, "readException", "()V");
        REQUIRE(violation.exception.IsValid());
        CHECK(f.linker.Class(violation.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
    }
}

TEST_CASE("DVM-144 Parcel interface policy and malformed lengths are bounded") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto p = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto descriptor = f.vm.NewStringUtf8("echo");
        const auto roots = f.vm.ProtectReferences(std::array{p, descriptor});
        f.Static("Landroid/os/Binder;", "setThreadStrictModePolicy", "(I)V", {VmValue::Int(7)});
        f.On(p, "writeInterfaceToken", "(Ljava/lang/String;)V", {VmValue::Ref(descriptor)});
        CHECK(f.On(p, "dataSize", "()I").AsInt() == 20);
        f.On(p, "setDataPosition", "(I)V", {VmValue::Int(0)});
        CHECK(f.On(p, "readInt", "()I").AsInt() == 0x107);
        f.Static("Landroid/os/Binder;", "setThreadStrictModePolicy", "(I)V", {VmValue::Int(0)});
        f.On(p, "setDataPosition", "(I)V", {VmValue::Int(0)});
        f.On(p, "enforceInterface", "(Ljava/lang/String;)V", {VmValue::Ref(descriptor)});
        CHECK(f.Static("Landroid/os/Binder;", "getThreadStrictModePolicy", "()I").AsInt() == 0x107);
        for (const auto length : {-1, 0x7fffffff, 20}) {
            f.On(p, "setDataSize", "(I)V", {VmValue::Int(0)});
            f.On(p, "writeInt", "(I)V", {VmValue::Int(0)});
            f.On(p, "writeInt", "(I)V", {VmValue::Int(length)});
            f.On(p, "setDataPosition", "(I)V", {VmValue::Int(0)});
            const auto result = f.OnOutcome(p, "enforceInterface", "(Ljava/lang/String;)V", {VmValue::Ref(descriptor)});
            REQUIRE(result.exception.IsValid());
            CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/lang/SecurityException;");
        }
    }
}

TEST_CASE("DVM-144 Binder Parcel references survive append and release on overwrite") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto source = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto target = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto roots = f.vm.ProtectReferences(std::array{source, target});
        const auto binder = f.New("Landroid/os/Binder;");
        f.On(source, "writeStrongBinder", "(Landroid/os/IBinder;)V", {VmValue::Ref(binder)});
        CHECK(f.On(source, "dataSize", "()I").AsInt() == 16);
        const auto partial = f.OnOutcome(target, "appendFrom", "(Landroid/os/Parcel;II)V",
                                        {VmValue::Ref(source), VmValue::Int(0), VmValue::Int(4)});
        REQUIRE(partial.exception.IsValid());
        f.On(target, "appendFrom", "(Landroid/os/Parcel;II)V",
             {VmValue::Ref(source), VmValue::Int(0), VmValue::Int(16)});
        f.On(source, "recycle", "()V");
        static_cast<void>(f.vm.CollectGarbage("binder-append"));
        CHECK(f.vm.MarkReachable().IsMarked(binder));
        f.On(target, "setDataPosition", "(I)V", {VmValue::Int(0)});
        CHECK(f.On(target, "readStrongBinder", "()Landroid/os/IBinder;").ref == binder);
        CHECK(f.On(binder, "pingBinder", "()Z").AsInt() == 1);
        REQUIRE(f.OnOutcome(target, "marshall", "()[B").exception.IsValid());
        f.On(target, "setDataPosition", "(I)V", {VmValue::Int(0)});
        f.On(target, "writeInt", "(I)V", {VmValue::Int(0)});
        CHECK_FALSE(f.vm.MarkReachable().IsMarked(binder));
        f.On(target, "recycle", "()V");
    }
}

TEST_CASE("DVM-144 Binder identity and policy are isolated by execution context") {
    AndroidValueVm f;
    const auto worker = f.vm.CreateExecutionContext();
    const auto type = f.linker.ResolveDescriptor("Landroid/os/Binder;");
    const auto set = f.linker.FindDirectMethod(type, "setThreadStrictModePolicy", "(I)V");
    const auto get = f.linker.FindDirectMethod(type, "getThreadStrictModePolicy", "()I");
    REQUIRE(set);
    REQUIRE(get);
    f.Static("Landroid/os/Binder;", "setThreadStrictModePolicy", "(I)V", {VmValue::Int(7)});
    CHECK(f.vm.Call(worker, *get, {}).value.AsInt() == 0);
    const std::array args{VmValue::Int(9)};
    REQUIRE_FALSE(f.vm.Call(worker, *set, args).exception.IsValid());
    CHECK(f.Static("Landroid/os/Binder;", "getThreadStrictModePolicy", "()I").AsInt() == 7);
    CHECK(f.vm.Call(worker, *get, {}).value.AsInt() == 9);
    const auto identity = f.Static("Landroid/os/Binder;", "clearCallingIdentity", "()J").AsLong();
    f.Static("Landroid/os/Binder;", "restoreCallingIdentity", "(J)V", {VmValue::Long(identity)});
    CHECK(f.Static("Landroid/os/Binder;", "clearCallingIdentity", "()J").AsLong() == identity);
    REQUIRE(f.StaticOutcome("Landroid/os/Binder;", "joinThreadPool", "()V").exception.IsValid());
    f.vm.DiscardExecutionContext(worker);
}
