#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "runtime/boundary/core/boundary_symbols.h"
#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include <set>
#include "ogplay/runtime/integration/native_activity_runtime.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"
#include "runtime/integration/dexvm_android/shared.h"
#include "../fixtures/bitmap_decode_data.h"
#include <chrono>
#include <atomic>
#include "ogplay/runtime/vfs/sandbox_store.h"
#include "../dexvm/boot_dex.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <future>
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include <thread>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include "ogplay/runtime/dexvm/reflection.h"

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/encoding.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/elf.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_bridge.h"
#include "ogplay/runtime/integration/native_library_loader.h"
#include "ogplay/runtime/integration/apk_assets.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/jni_java_vm.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_native_registry.h"
#include "ogplay/runtime/jni/jni_object_array.h"
#include "ogplay/runtime/jni_guest/jni_guest_abi.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"
#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/bus.h"
#include "ogplay/session/android_app_process.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/video/fake_video_player.h"

#include "native_library_fixture.h"

using namespace ogplay::test::native_library;

TEST_CASE("DexVM bridge canonicalizes JNI jclass as the real Class object") {
    using namespace ogplay;
    ApplicationProcess fixture;
    const auto represented = fixture.bridge->Linker().FindClass("Lfixture/LauncherActivity;");
    const auto class_class = fixture.bridge->Linker().FindClass("Ljava/lang/Class;");
    REQUIRE(represented.has_value());
    REQUIRE(class_class.has_value());
    const auto class_object = fixture.bridge->Model().ClassObject(*represented);

    const auto reference = fixture.bridge->PublishLocal(class_object);
    REQUIRE_FALSE(reference.IsNull());
    const auto identity = fixture.session->Environment().ResolveObjectForHle(1U, reference);
    REQUIRE(identity.has_value());
    CHECK(identity == fixture.bridge->RegisteredClassIdentity(*represented));
    CHECK(fixture.session->Objects().ClassOf(*identity) == fixture.bridge->RegisteredClassIdentity(*class_class));

    const auto round_trip = fixture.bridge->FromReference(reference);
    CHECK(round_trip == class_object);
    CHECK(fixture.bridge->Model().ClassOfClassObject(round_trip) == *represented);
}

TEST_CASE("DVM-200 JNI reflection conversions share VM members and survive GC") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess f(backend);
        auto& vm = f.bridge->Vm();
        auto& model = f.bridge->Model();
        auto& linker = f.bridge->Linker();
        auto& session = *f.session;
        auto& env = session.Environment();
        memory::AddressSpace memory;
        memory::CheckedMemoryBus bus(memory);
        cpu::InterpreterCpu cpu(bus);
        GuestJniAbi abi(memory);
        JniGuestCallDispatcher dispatcher(f.ledger);
        JniJavaVm java_vm(env);
        JniGuestBindingContext context{env,
                                       session.Classes(),
                                       session.Invocations(),
                                       session.Fields(),
                                       session.Strings(),
                                       session.Arrays(),
                                       java_vm,
                                       session.Objects(),
                                       memory};
        BindJniGuestSlots(dispatcher, context);
        dispatcher.Seal();
        const auto call = [&](const char* name, std::uint32_t r1, std::uint32_t r2 = 0, std::uint32_t r3 = 0) {
            const auto slot = FindJniSlot(name);
            REQUIRE(slot.has_value());
            const auto target = bus.Read32(kJniGuestEnvironmentTable.Add(slot->Value() * 4U));
            cpu::A32State state;
            state.SetThreadId(1);
            state.SetState(cpu::ExecutionState::thumb);
            state.SetRegister(cpu::CoreRegister::pc, target & ~1U);
            state.SetRegister(cpu::CoreRegister::r0, abi.Environment().Value());
            state.SetRegister(cpu::CoreRegister::r1, r1);
            state.SetRegister(cpu::CoreRegister::r2, r2);
            state.SetRegister(cpu::CoreRegister::r3, r3);
            cpu.SetState(state);
            const auto handled = dispatcher.Handle(cpu, cpu.Run(1));
            REQUIRE(handled);
            return cpu.GetState().Register(cpu::CoreRegister::r0);
        };
        const auto owner = linker.ResolveDescriptor("Lfixture/LauncherActivity;");
        const auto owner_ref = f.bridge->PublishLocal(model.ClassObject(owner));
        const auto identity = *f.bridge->RegisteredClassIdentity(owner);
        const auto object = vm.NewIntrinsicInstance("Lfixture/LauncherActivity;");
        const auto object_ref = f.bridge->PublishLocal(object);
        const auto instance_meta = vm.Reflection().FindDeclaredField(owner, "imported");
        REQUIRE(instance_meta.has_value());
        auto& classes = session.Classes();
        for (const char* name : {"imported", "stage"}) {
            const bool is_static = std::string_view(name) == "stage";
            const auto meta = vm.Reflection().FindDeclaredField(owner, name);
            REQUIRE(meta.has_value());
            env.PushLocalFrame(1, 8);
            const auto wrapper = f.bridge->PublishLocal(vm.Reflection().MaterializeField(*meta));
            const auto id = call("FromReflectedField", wrapper.Value());
            CHECK(id == classes.GetFieldId(identity, name, "I", is_static)->Value());
            const auto round_trip = JniReference{call("ToReflectedField", owner_ref.Value(), id, is_static)};
            CHECK(vm.Reflection().FieldMetadata(f.bridge->FromReference(round_trip)).field == meta->field);
            CHECK(call("FromReflectedField", round_trip.Value()) == id);
            if (is_static) {
                static_cast<void>(call("SetStaticIntField", owner_ref.Value(), id, 42));
                CHECK(call("GetStaticIntField", owner_ref.Value(), id) == 42);
            } else {
                static_cast<void>(call("SetIntField", object_ref.Value(), id, 37));
                CHECK(call("GetIntField", object_ref.Value(), id) == 37);
            }
            const auto global = env.NewGlobalRef(1, wrapper);
            static_cast<void>(env.PopLocalFrame(1));
            CHECK_THROWS(static_cast<void>(call("FromReflectedField", wrapper.Value())));
            static_cast<void>(vm.CollectGarbage("jni-reflection"));
            CHECK(call("FromReflectedField", global.Value()) == id);
            env.DeleteGlobalRef(1, global);
            static_cast<void>(vm.CollectGarbage("jni-reflection-id"));
            const auto renewed = call("ToReflectedField", owner_ref.Value(), id, is_static);
            CHECK(call("FromReflectedField", renewed) == id);
            CHECK_THROWS(static_cast<void>(call("ToReflectedField", owner_ref.Value(), id, !is_static)));
        }
        for (const char* name : {"getImported", "getInstances", "<init>"}) {
            const bool constructor = std::string_view(name) == "<init>";
            const bool is_static = std::string_view(name) == "getInstances";
            const auto id = classes.GetMethodId(identity, name, constructor ? "()V" : "()I", is_static);
            REQUIRE(id.has_value());
            const auto ref = JniReference{call("ToReflectedMethod", owner_ref.Value(), id->Value(), is_static)};
            CHECK(linker.Class(model.ObjectClass(f.bridge->FromReference(ref))).descriptor ==
                  (constructor ? "Ljava/lang/reflect/Constructor;" : "Ljava/lang/reflect/Method;"));
            CHECK(call("FromReflectedMethod", ref.Value()) == id->Value());
            CHECK_THROWS(static_cast<void>(call("FromReflectedField", ref.Value())));
            if (!constructor && !is_static) CHECK(call("CallIntMethod", object_ref.Value(), id->Value()) == 37);
            if (constructor) {
                const std::array<JniValue, 0> args{};
                static_cast<void>(session.Invocations().InvokeNonvirtual(1, object_ref, identity, identity, *id, args,
                                                                         JniArgumentSource::value_array));
                CHECK(call("GetIntField", object_ref.Value(),
                           classes.GetFieldId(identity, "imported", "I", false)->Value()) == 7);
            }
        }
        const auto child_ref =
            f.bridge->PublishLocal(model.ClassObject(linker.ResolveDescriptor("Lfixture/LauncherChildActivity;")));
        const auto inherited_id = classes.GetFieldId(identity, "imported", "I", false)->Value();
        const auto inherited = JniReference{call("ToReflectedField", child_ref.Value(), inherited_id)};
        CHECK(vm.Reflection().FieldMetadata(f.bridge->FromReference(inherited)).declaring_class == owner);
        CHECK(call("FromReflectedField", inherited.Value()) == inherited_id);
        const auto inherited_method = classes.GetMethodId(identity, "getImported", "()I", false)->Value();
        CHECK(call("FromReflectedMethod", call("ToReflectedMethod", child_ref.Value(), inherited_method)) ==
              inherited_method);
        CHECK_THROWS(static_cast<void>(call("FromReflectedField", 0)));
        CHECK_THROWS(static_cast<void>(call("FromReflectedMethod", object_ref.Value())));
        CHECK_THROWS(static_cast<void>(call("ToReflectedField", owner_ref.Value(), 0xffffffffU)));
        CHECK_THROWS(static_cast<void>(call("ToReflectedMethod", owner_ref.Value(), 0xffffffffU)));
        const auto other = f.bridge->PublishLocal(model.ClassObject(linker.ResolveDescriptor("Ljava/lang/String;")));
        CHECK_THROWS(static_cast<void>(
            call("ToReflectedField", other.Value(), classes.GetFieldId(identity, "imported", "I", false)->Value())));
        f.bridge.reset();
        CHECK_THROWS_WITH(static_cast<void>(env.FromReflectedField(1, object_ref)),
                          "JNI reflection backend is unavailable");
    }
}

TEST_CASE("DVM-141 Java Map array elements become callable JNI receivers and GC roots") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess f(backend);
        auto& vm = f.bridge->Vm();
        auto& model = f.bridge->Model();
        auto& linker = f.bridge->Linker();
        auto& session = *f.session;
        memory::AddressSpace memory;
        memory::CheckedMemoryBus bus(memory);
        cpu::InterpreterCpu cpu(bus);
        runtime::GuestJniAbi abi(memory);
        runtime::JniGuestCallDispatcher dispatcher(f.ledger);
        runtime::JniJavaVm java_vm(session.Environment());
        runtime::JniGuestBindingContext context{session.Environment(),
                                                session.Classes(),
                                                session.Invocations(),
                                                session.Fields(),
                                                session.Strings(),
                                                session.Arrays(),
                                                java_vm,
                                                session.Objects(),
                                                memory};
        runtime::BindJniGuestSlots(dispatcher, context);
        dispatcher.Seal();
        const auto call = [&](const char* name, std::uint32_t r1, std::uint32_t r2 = 0) {
            const auto slot = runtime::FindJniSlot(name);
            REQUIRE(slot.has_value());
            const auto target = bus.Read32(runtime::kJniGuestEnvironmentTable.Add(slot->Value() * 4U));
            cpu::A32State state;
            state.SetThreadId(1);
            state.SetState(cpu::ExecutionState::thumb);
            state.SetRegister(cpu::CoreRegister::pc, target & ~1U);
            state.SetRegister(cpu::CoreRegister::r0, abi.Environment().Value());
            state.SetRegister(cpu::CoreRegister::r1, r1);
            state.SetRegister(cpu::CoreRegister::r2, r2);
            cpu.SetState(state);
            const auto stopped = cpu.Run(1);
            REQUIRE(dispatcher.Handle(cpu, stopped));
            return cpu.GetState().Register(cpu::CoreRegister::r0);
        };
        const auto map_class = linker.ResolveDescriptor("Ljava/util/HashMap;");
        REQUIRE_FALSE(vm.EnsureClassInitialized(map_class).exception.IsValid());
        const auto map = vm.NewIntrinsicInstance("Ljava/util/HashMap;");
        const auto ctor = linker.FindDirectMethod(map_class, "<init>", "()V");
        REQUIRE(ctor.has_value());
        REQUIRE_FALSE(vm.Call(*ctor, std::array{VmValue::Ref(map)}).exception.IsValid());
        const auto array = model.NewObjectArray(linker.ResolveDescriptor("[Ljava/lang/Object;"),
                                                linker.ResolveDescriptor("Ljava/lang/Object;"), 1);
        model.SetObjectElement(array, 0, map);
        const auto array_ref = f.bridge->PublishLocal(array);
        const auto identity = model.ToIdentity(map);
        CHECK_THROWS_AS(static_cast<void>(session.Objects().ClassOf(identity)), runtime::JniGuestBindingError);
        const auto element = call("GetObjectArrayElement", array_ref.Value());
        const auto second = call("GetObjectArrayElement", array_ref.Value());
        model.SetObjectElement(array, 0, VmObjectRef{});
        static_cast<void>(vm.CollectGarbage("dvm141-element-local-root"));
        const auto klass = session.Objects().ClassOf(identity);
        CHECK(klass == *f.bridge->RegisteredClassIdentity(map_class));
        const auto method = session.Classes().GetMethodId(klass, "entrySet", "()Ljava/util/Set;", false);
        REQUIRE(method.has_value());
        const auto entries = call("CallObjectMethodV", element, method->Value());
        CHECK(entries != 0);
        CHECK(call("GetObjectClass", element) != 0);
        static_cast<void>(call("DeleteLocalRef", element));
        static_cast<void>(call("DeleteLocalRef", second));
        static_cast<void>(call("DeleteLocalRef", entries));
        // Replace the interpreter's retained last object return with a scalar.
        const auto size_slot = linker.FindVtableIndex(map_class, "size", "()I");
        REQUIRE(size_slot.has_value());
        REQUIRE_FALSE(
            vm.Call(linker.Class(map_class).vtable[*size_slot], std::array{VmValue::Ref(map)}).exception.IsValid());
        static_cast<void>(vm.CollectGarbage("dvm141-released-element"));
        CHECK_THROWS_AS(static_cast<void>(session.Objects().ClassOf(identity)), runtime::JniGuestBindingError);
    }
}

TEST_CASE("DexVM JNI classes retain intrinsic superclasses for object arrays") {
    using namespace ogplay;
    ApplicationProcess fixture;
    const auto activity = fixture.bridge->Linker().FindClass("Landroid/app/Activity;");
    const auto launcher = fixture.bridge->Linker().FindClass("Lfixture/LauncherActivity;");
    const auto object = fixture.bridge->Linker().FindClass("Ljava/lang/Object;");
    REQUIRE(activity.has_value());
    REQUIRE(launcher.has_value());
    REQUIRE(object.has_value());

    const auto activity_identity = fixture.bridge->RegisteredClassIdentity(*activity);
    const auto launcher_identity = fixture.bridge->RegisteredClassIdentity(*launcher);
    const auto object_identity = fixture.bridge->RegisteredClassIdentity(*object);
    REQUIRE(activity_identity.has_value());
    REQUIRE(launcher_identity.has_value());
    REQUIRE(object_identity.has_value());
    CHECK(fixture.session->Classes().IsAssignableFrom(*activity_identity, *launcher_identity));

    const auto& launcher_class = fixture.bridge->Linker().Class(*launcher);
    const auto launcher_object = fixture.bridge->Model().NewInstance(*launcher, launcher_class.instance_slots);
    const runtime::JniObjectValue launcher_value{fixture.bridge->Model().ToIdentity(launcher_object),
                                                 *launcher_identity};
    auto& arrays = fixture.session->Objects().ObjectArrays();
    const auto array = arrays.New(*activity_identity, 1, launcher_value);
    CHECK(arrays.Get(array, 0) == launcher_value);
    CHECK_THROWS_AS(
        arrays.Set(array, 0,
                   runtime::JniObjectValue{{runtime::JniObjectDomain::dex_vm, UINT64_C(0x1234)}, *object_identity}),
        runtime::JniObjectArrayError);
}

TEST_CASE("DVM-231 JNI-created strings and arrays expose their actual runtime classes") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess f(backend);
        auto& env = f.session->Environment();
        auto& objects = f.session->Objects();
        auto& model = f.bridge->Model();
        auto& vm = f.bridge->Vm();
        auto& linker = f.bridge->Linker();
        memory::AddressSpace memory;
        memory::CheckedMemoryBus bus(memory);
        cpu::InterpreterCpu cpu(bus);
        GuestJniAbi abi(memory);
        JniGuestCallDispatcher dispatcher(f.ledger);
        JniJavaVm java_vm(env);
        constexpr memory::GuestAddress data{0x72000000U};
        memory.Map({data, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::write);
        JniGuestBindingContext context{env,
                                       f.session->Classes(),
                                       f.session->Invocations(),
                                       f.session->Fields(),
                                       f.session->Strings(),
                                       f.session->Arrays(),
                                       java_vm,
                                       objects,
                                       memory};
        BindJniGuestSlots(dispatcher, context);
        dispatcher.Seal();
        const auto call = [&](const char* name, std::uint32_t r1, std::uint32_t r2 = 0, std::uint32_t r3 = 0,
                              std::uint64_t thread = 1) {
            const auto slot = FindJniSlot(name);
            REQUIRE(slot.has_value());
            const auto target = bus.Read32(kJniGuestEnvironmentTable.Add(slot->Value() * 4U));
            cpu::A32State state;
            state.SetThreadId(thread);
            state.SetState(cpu::ExecutionState::thumb);
            state.SetRegister(cpu::CoreRegister::pc, target & ~1U);
            state.SetRegister(cpu::CoreRegister::r0, abi.Environment().Value());
            state.SetRegister(cpu::CoreRegister::r1, r1);
            state.SetRegister(cpu::CoreRegister::r2, r2);
            state.SetRegister(cpu::CoreRegister::r3, r3);
            state.SetRegister(cpu::CoreRegister::sp, data.Add(0x800U).Value());
            cpu.SetState(state);
            const auto handled = dispatcher.Handle(cpu, cpu.Run(1));
            REQUIRE(handled);
            return cpu.GetState().Register(cpu::CoreRegister::r0);
        };
        const auto write32 = [&](std::uint32_t offset, std::uint32_t value) {
            std::vector<std::byte> bytes(4);
            Put32(bytes, 0, value);
            memory.Write(data.Add(offset), bytes);
        };
        const std::array text{std::byte{'h'}, std::byte{0xc3}, std::byte{0xa9}, std::byte{}};
        const std::array utf16{std::byte{'h'}, std::byte{}, std::byte{0xe9}, std::byte{}};
        const std::array charset{std::byte{'U'}, std::byte{'T'}, std::byte{'F'},
                                 std::byte{'-'}, std::byte{'8'}, std::byte{}};
        memory.Write(data, text);
        memory.Write(data.Add(0x20U), utf16);
        memory.Write(data.Add(0x40U), charset);
        const auto string_class = *f.session->Classes().FindClass("java/lang/String");
        const auto object_class = *f.session->Classes().FindClass("java/lang/Object");
        const auto class_ref = env.PublishLocalObject(1, string_class);
        const auto object_ref = env.PublishLocalObject(1, object_class);
        const auto get_bytes =
            *f.session->Classes().GetMethodId(string_class, "getBytes", "(Ljava/lang/String;)[B", false);
        env.PushLocalFrame(1, 64);
        const auto encoding = call("NewStringUTF", data.Add(0x40U).Value());
        const auto utf = call("NewStringUTF", data.Value());
        const auto unicode = call("NewString", data.Add(0x20U).Value(), 2);
        // First operation on either string is a Java call, with no VM publication.
        for (const auto receiver : {utf, unicode}) {
            for (const auto name : {"CallObjectMethod", "CallObjectMethodV", "CallObjectMethodA"}) {
                write32(0x100U, encoding);
                write32(0x104U, 0);
                const auto args = std::string_view(name) == "CallObjectMethod" ? encoding : data.Add(0x100U).Value();
                const auto bytes = JniReference{call(name, receiver, get_bytes.Value(), args)};
                const auto array = f.bridge->FromReference(bytes);
                CHECK(model.ReadByteRegion(array, 0, model.ArrayLength(array)) ==
                      std::vector<std::byte>{std::byte{0x68}, std::byte{0xc3}, std::byte{0xa9}});
                CHECK(linker.Class(model.ObjectClass(array)).descriptor == "[B");
            }
            CHECK(*env.ResolveObjectForHle(1, JniReference{call("GetObjectClass", receiver)}) == string_class);
            CHECK(call("IsInstanceOf", receiver, class_ref.Value()) == 1);
            CHECK(call("IsInstanceOf", receiver, object_ref.Value()) == 1);
        }
        // Querying a newly created string must also work before any Java call.
        const auto fresh = call("NewStringUTF", data.Value());
        CHECK(*env.ResolveObjectForHle(1, JniReference{call("GetObjectClass", fresh)}) == string_class);
        const auto fresh_identity = *env.ResolveObjectForHle(1, JniReference{fresh});
        CHECK_THROWS_AS(objects.EnsureRegistered(fresh_identity, object_class), JniGuestBindingError);
        constexpr std::array names{"NewBooleanArray", "NewByteArray", "NewCharArray",  "NewShortArray",
                                   "NewIntArray",     "NewLongArray", "NewFloatArray", "NewDoubleArray"};
        constexpr std::array descriptors{"[Z", "[B", "[C", "[S", "[I", "[J", "[F", "[D"};
        for (std::size_t index = 0; index < names.size(); ++index) {
            const auto array = JniReference{call(names[index], 2)};
            const auto native_class = *env.ResolveObjectForHle(1, JniReference{call("GetObjectClass", array.Value())});
            CHECK(native_class ==
                  JniObjectIdentity{JniObjectDomain::dex_vm, linker.ResolveDescriptor(descriptors[index]).Value()});
            const auto imported = f.bridge->FromReference(array);
            CHECK(linker.Class(model.ObjectClass(imported)).descriptor == descriptors[index]);
            CHECK(model.ArrayLength(imported) == 2);
        }
        const auto array = JniReference{call("NewObjectArray", 1, class_ref.Value(), utf)};
        const auto array_class = *env.ResolveObjectForHle(1, JniReference{call("GetObjectClass", array.Value())});
        CHECK(array_class ==
              JniObjectIdentity{JniObjectDomain::dex_vm, linker.ResolveDescriptor("[Ljava/lang/String;").Value()});
        CHECK(call("GetObjectArrayElement", array.Value(), 0) != 0);
        const auto global = env.NewGlobalRef(1, JniReference{fresh});
        static_cast<void>(env.PopLocalFrame(1));
        static_cast<void>(vm.CollectGarbage("jni-native-types"));
        CHECK_THROWS(static_cast<void>(call("GetObjectClass", fresh)));
        CHECK(objects.ClassOf(fresh_identity) == string_class);
        env.AttachThread(777);
        CHECK(*env.ResolveObjectForHle(777, JniReference{call("GetObjectClass", global.Value(), 0, 0, 777)}) ==
              string_class);
        static_cast<void>(env.DetachThread(777));
        env.DeleteGlobalRef(1, global);
        static_cast<void>(vm.CollectGarbage("jni-native-types-dead"));
        CHECK_FALSE(f.session->Strings().Contains(fresh_identity));
        CHECK_THROWS_AS(static_cast<void>(objects.ClassOf(fresh_identity)), JniGuestBindingError);
        const auto forged = env.PublishLocalObject(1, AllocateJniHostObjectIdentity());
        CHECK_THROWS_AS(static_cast<void>(call("GetObjectClass", forged.Value())), JniGuestBindingError);
        f.bridge.reset();
        CHECK_THROWS_AS(static_cast<void>(objects.ClassOf(f.session->Strings().Create({}))), JniGuestBindingError);
    }
}

TEST_CASE("DexVM imports JNI-created application objects with instance slots") {
    using namespace ogplay;
    ApplicationProcess fixture;
    const auto launcher = fixture.bridge->Linker().FindClass("Lfixture/LauncherActivity;");
    const auto activity = fixture.bridge->Linker().FindClass("Landroid/app/Activity;");
    REQUIRE(launcher.has_value());
    REQUIRE(activity.has_value());
    const auto launcher_identity = fixture.bridge->RegisteredClassIdentity(*launcher);
    const auto activity_identity = fixture.bridge->RegisteredClassIdentity(*activity);
    REQUIRE(launcher_identity.has_value());
    REQUIRE(activity_identity.has_value());

    const auto imported_identity = fixture.session->Objects().Allocate(*launcher_identity);
    const auto imported_reference = fixture.session->Environment().PublishLocalObject(1U, imported_identity);
    const auto imported = fixture.bridge->FromReference(imported_reference);
    CHECK(fixture.bridge->Model().Kind(imported) == runtime::dexvm::VmObjectKind::vm_instance);
    CHECK(fixture.bridge->Model().ToIdentity(imported) == imported_identity);
    const auto init = fixture.bridge->Linker().FindDirectMethod(*launcher, "<init>", "()V");
    REQUIRE(init.has_value());
    const std::array receiver{runtime::dexvm::VmValue::Ref(imported)};
    auto outcome = fixture.bridge->Vm().Call(*init, receiver);
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    const auto getter_index = fixture.bridge->Linker().FindVtableIndex(*launcher, "getImported", "()I");
    REQUIRE(getter_index.has_value());
    outcome = fixture.bridge->Vm().Call(fixture.bridge->Linker().Class(*launcher).vtable[*getter_index], receiver);
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    CHECK(outcome.value.AsInt() == 7);

    // Activity is an API19 BootDex class; its JNI allocation imports VM fields.
    const auto intrinsic_identity = fixture.session->Objects().Allocate(*activity_identity);
    const auto intrinsic_reference = fixture.session->Environment().PublishLocalObject(1U, intrinsic_identity);
    const auto intrinsic = fixture.bridge->FromReference(intrinsic_reference);
    CHECK(fixture.bridge->Model().Kind(intrinsic) == runtime::dexvm::VmObjectKind::vm_instance);
}

TEST_CASE("DexVM Intent ArrayList extras trace children and sweep with owner") {
    using namespace ogplay;
    ApplicationProcess fixture;
    auto& vm = fixture.bridge->Vm();
    auto& linker = fixture.bridge->Linker();
    const auto intent = vm.NewIntrinsicInstance("Landroid/content/Intent;");
    const auto list = vm.NewIntrinsicInstance("Ljava/util/ArrayList;");
    const auto key = vm.NewStringUtf8("levels");
    const auto intent_class = linker.ResolveDescriptor("Landroid/content/Intent;");
    const auto put_index = linker.FindVtableIndex(intent_class, "putIntegerArrayListExtra",
                                                  "(Ljava/lang/String;Ljava/util/ArrayList;)Landroid/content/Intent;");
    REQUIRE(put_index.has_value());
    auto outcome = vm.Call(linker.Class(intent_class).vtable[*put_index],
                           std::vector{runtime::dexvm::VmValue::Ref(intent), runtime::dexvm::VmValue::Ref(key),
                                       runtime::dexvm::VmValue::Ref(list)});
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);

    vm.SetGcIntegration({{}, {}, [intent](const runtime::dexvm::VmRootVisitor& visit) { visit(intent); }});
    const auto marked = vm.MarkReachable();
    CHECK(marked.IsMarked(intent));
    CHECK(marked.IsMarked(list));
    static_cast<void>(vm.CollectGarbage("intent-list-extra-edge"));
    CHECK(vm.MarkReachable().IsMarked(list));

    vm.SetGcIntegration({});
    const auto flags_index = linker.FindVtableIndex(intent_class, "getFlags", "()I");
    REQUIRE(flags_index.has_value());
    outcome =
        vm.Call(linker.Class(intent_class).vtable[*flags_index], std::vector{runtime::dexvm::VmValue::Ref(intent)});
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    static_cast<void>(vm.CollectGarbage("intent-list-extra-owner-sweep"));
    CHECK_FALSE(vm.MarkReachable().IsMarked(intent));
    CHECK_FALSE(vm.MarkReachable().IsMarked(list));
}

TEST_CASE("DexVM JNI fields share interpreter storage and reference identity") {
    using namespace ogplay;
    ApplicationProcess fixture;
    const auto owner = fixture.bridge->Linker().FindClass("Lfixture/LauncherActivity;");
    const auto child = fixture.bridge->Linker().FindClass("Lfixture/LauncherChildActivity;");
    REQUIRE(owner.has_value());
    REQUIRE(child.has_value());
    const auto owner_identity = fixture.bridge->RegisteredClassIdentity(*owner);
    const auto child_identity = fixture.bridge->RegisteredClassIdentity(*child);
    REQUIRE(owner_identity.has_value());
    REQUIRE(child_identity.has_value());

    auto& classes = fixture.session->Classes();
    auto& fields = fixture.session->Fields();
    const auto imported_id = classes.GetFieldId(*owner_identity, "imported", "I", false);
    const auto inherited_id = classes.GetFieldId(*child_identity, "imported", "I", false);
    const auto peer_id = classes.GetFieldId(*owner_identity, "peer", "Ljava/lang/Object;", false);
    const auto wide_id = classes.GetFieldId(*owner_identity, "wide", "J", true);
    const auto stage_id = classes.GetFieldId(*owner_identity, "stage", "I", true);
    REQUIRE(imported_id.has_value());
    REQUIRE(inherited_id == imported_id);
    REQUIRE(peer_id.has_value());
    REQUIRE(wide_id.has_value());
    REQUIRE(stage_id.has_value());
    REQUIRE(fields.EnsureClassInitialized(*owner_identity, 1U));

    const auto instance =
        fixture.bridge->Model().NewInstance(*owner, fixture.bridge->Linker().Class(*owner).instance_slots);
    const auto instance_ref = fixture.bridge->PublishLocal(instance);
    const auto instance_identity = fixture.session->Environment().ResolveObjectForHle(1U, instance_ref);
    REQUIRE(instance_identity.has_value());
    fields.SetInstance(*instance_identity, *owner_identity, *imported_id, runtime::JniInt{42}, 1U);

    const auto getter_index = fixture.bridge->Linker().FindVtableIndex(*owner, "getImported", "()I");
    REQUIRE(getter_index.has_value());
    const std::array receiver{runtime::dexvm::VmValue::Ref(instance)};
    auto outcome = fixture.bridge->Vm().Call(fixture.bridge->Linker().Class(*owner).vtable[*getter_index], receiver);
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    CHECK(outcome.value.AsInt() == 42);

    const auto round_trip_primitive = [&](const char* name, const char* descriptor, const runtime::JniValue& value) {
        const auto id = classes.GetFieldId(*owner_identity, name, descriptor, false);
        REQUIRE(id.has_value());
        fields.SetInstance(*instance_identity, *owner_identity, *id, value, 1U);
        CHECK(fields.GetInstance(*instance_identity, *owner_identity, *id, 1U) == value);
    };
    round_trip_primitive("flag", "Z", runtime::JniBoolean{1});
    round_trip_primitive("small", "B", runtime::JniByte{-7});
    round_trip_primitive("letter", "C", runtime::JniChar{0x4e2d});
    round_trip_primitive("shortValue", "S", runtime::JniShort{-1234});
    round_trip_primitive("ratio", "F", runtime::JniFloat{1.25F});
    round_trip_primitive("precise", "D", runtime::JniDouble{-3.5});

    const auto peer =
        fixture.bridge->Model().NewInstance(*owner, fixture.bridge->Linker().Class(*owner).instance_slots);
    const auto peer_ref = fixture.bridge->PublishLocal(peer);
    fields.SetInstance(*instance_identity, *owner_identity, *peer_id, peer_ref, 1U);
    const auto round_trip =
        std::get<runtime::JniReference>(fields.GetInstance(*instance_identity, *owner_identity, *peer_id, 1U));
    CHECK(fixture.bridge->FromReference(round_trip) == peer);

    fields.SetStatic(*owner_identity, *wide_id, runtime::JniLong{INT64_C(0x1122334455667788)}, 1U);
    CHECK(std::get<runtime::JniLong>(fields.GetStatic(*owner_identity, *wide_id, 1U)) == INT64_C(0x1122334455667788));
    fields.SetStatic(*owner_identity, *stage_id, runtime::JniInt{9}, 1U);
    CHECK(fixture.CallStaticInt("Lfixture/LauncherActivity;", "getStage") == 9);
}

TEST_CASE("DVM-242 ARM JNI AllocObject shares zeroed inherited fields without constructors") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess f(backend);
        auto& bridge = *f.bridge;
        auto& vm = bridge.Vm();
        auto& linker = vm.Linker();
        auto& process = f.session->Process();
        auto& env = f.session->Environment();
        const auto memory = process.GuestMemoryAccess();
        const auto scratch = memory.allocate(256);
        const auto invoke = [&](const char* name, std::array<std::uint32_t, 4> args) {
            return process.Invoke({memory::GuestAddress{EnvironmentThunk(name)}, args, {}}).return_value;
        };
        const auto clazz = [&](const char* descriptor) {
            return bridge.PublishLocal(vm.Model().ClassObject(linker.ResolveDescriptor(descriptor)));
        };
        const auto allocate = [&](JniReference type) {
            return JniReference{invoke("AllocObject", {process.GuestEnvironment().Value(), type.Value(), 0, 0})};
        };
        const auto child = linker.ResolveDescriptor("Lfixture/AllocChild;");
        const auto child_class = clazz("Lfixture/AllocChild;");
        CHECK(linker.Class(child).clinit_state == ClinitState::uninitialized);
        const auto reference = allocate(child_class);
        REQUIRE_FALSE(env.ExceptionCheck(1));
        REQUIRE_FALSE(reference.IsNull());
        const auto object = bridge.FromReference(reference);
        CHECK(vm.Model().ObjectClass(object) == child);
        for (const auto slot : vm.Model().InstanceSlots(object))
            CHECK(slot.bits == 0);
        const auto static_value = [&](const char* name) {
            const auto field = linker.FindFieldRecursive(child, name, "I");
            REQUIRE(field.has_value());
            return linker.Class(child).static_storage[linker.Field(*field).slot];
        };
        CHECK(static_value("initialized") == 1);
        CHECK(static_value("constructed") == 0);
        const auto field_id = [&](const char* name, const char* signature) {
            std::array<std::byte, 256> data{};
            std::memcpy(data.data(), name, std::strlen(name) + 1);
            std::memcpy(data.data() + 64, signature, std::strlen(signature) + 1);
            memory.write(scratch, data);
            return invoke("GetFieldID", {process.GuestEnvironment().Value(), child_class.Value(), scratch.Value(),
                                         scratch.Add(64).Value()});
        };
        const auto number = field_id("number", "I");
        REQUIRE(number != 0);
        invoke("SetIntField", {process.GuestEnvironment().Value(), reference.Value(), number, 17});
        const auto method = linker.FindVtableIndex(child, "numberValue", "()I");
        REQUIRE(method.has_value());
        const auto read = vm.Call(linker.Class(child).vtable[*method], std::array{VmValue::Ref(object)});
        REQUIRE_FALSE(read.exception.IsValid());
        CHECK(read.value.AsInt() == 17);
        const auto inherited = field_id("inherited", "J");
        CHECK(invoke("GetLongField", {process.GuestEnvironment().Value(), reference.Value(), inherited, 0}) == 0);
        const auto object_class =
            JniReference{invoke("GetObjectClass", {process.GuestEnvironment().Value(), reference.Value(), 0, 0})};
        CHECK(env.IsSameObject(1, child_class, object_class));
        env.DeleteLocalRef(1, object_class);
        const auto global = env.NewGlobalRef(1, reference);
        const auto weak = env.NewWeakGlobalRef(1, reference);
        env.DeleteLocalRef(1, reference);
        static_cast<void>(vm.CollectGarbage("alloc-object-global"));
        CHECK(vm.MarkReachable().IsMarked(object));
        env.DeleteGlobalRef(1, global);
        static_cast<void>(vm.CollectGarbage("alloc-object-weak"));
        CHECK_FALSE(env.ResolveObjectForHle(1, weak).has_value());
        env.DeleteWeakGlobalRef(1, weak);
        const auto no_constructor = clazz("Lfixture/AllocNoConstructor;");
        const auto no_init = allocate(no_constructor);
        REQUIRE_FALSE(no_init.IsNull());
        REQUIRE_FALSE(env.ExceptionCheck(1));
        env.DeleteLocalRef(1, no_init);
        const auto constructor =
            f.session->Classes().GetMethodId(*env.ResolveObjectForHle(1, child_class), "<init>", "()V", false);
        REQUIRE(constructor.has_value());
        const auto constructed = JniReference{
            invoke("NewObjectA", {process.GuestEnvironment().Value(), child_class.Value(), constructor->Value(), 0})};
        REQUIRE_FALSE(env.ExceptionCheck(1));
        REQUIRE_FALSE(constructed.IsNull());
        CHECK(invoke("GetIntField", {process.GuestEnvironment().Value(), constructed.Value(), number, 0}) == 7);
        CHECK(invoke("GetLongField", {process.GuestEnvironment().Value(), constructed.Value(), inherited, 0}) == 13);
        env.DeleteLocalRef(1, constructed);
        CHECK(static_value("initialized") == 1);
        CHECK(static_value("constructed") == 7);
        const auto expect_exception = [&](JniReference type, const char* descriptor) {
            CHECK(allocate(type).IsNull());
            REQUIRE(env.ExceptionCheck(1));
            const auto exception = env.ExceptionOccurred(1);
            env.ExceptionClear(1);
            CHECK(linker.Class(vm.Model().ObjectClass(bridge.FromReference(exception))).descriptor == descriptor);
            env.DeleteLocalRef(1, exception);
        };
        for (const char* type : {"Lfixture/AllocAbstract;", "Ljava/lang/Runnable;", "[I", "Ljava/lang/Class;"}) {
            const auto klass = clazz(type);
            expect_exception(klass, "Ljava/lang/InstantiationException;");
            env.DeleteLocalRef(1, klass);
        }
        const auto failed = clazz("Lfixture/AllocFailInit;");
        expect_exception(failed, "Ljava/lang/ExceptionInInitializerError;");
        expect_exception(failed, "Ljava/lang/NoClassDefFoundError;");
        env.DeleteLocalRef(1, failed);
        // Exhaust local reference publication; allocation must not leak a
        // registry entry or a permanent heap root when publication fails.
        std::vector<JniReference> aliases;
        for (int i = 0; i < 512; ++i) {
            try {
                aliases.push_back(env.NewLocalRef(1, child_class));
            } catch (const JniReferenceError&) {
                break;
            }
        }
        REQUIRE(aliases.size() > 100);
        CHECK_THROWS_AS(static_cast<void>(env.AllocObject(1, child_class)), JniReferenceError);
        for (auto alias : aliases)
            env.DeleteLocalRef(1, alias);
        static_cast<void>(vm.CollectGarbage("alloc-object-publication-failure"));
        const auto recovered = allocate(child_class);
        REQUIRE_FALSE(recovered.IsNull());
        env.DeleteLocalRef(1, recovered);
        CHECK_THROWS_AS(static_cast<void>(env.AllocObject(1, JniReference{})), std::invalid_argument);
        env.DeleteLocalRef(1, no_constructor);
        memory.release(scratch, 256);
        f.bridge.reset();
        CHECK_THROWS_AS(static_cast<void>(env.AllocObject(1, child_class)), std::runtime_error);
        env.DeleteLocalRef(1, child_class);
    }
}

TEST_CASE("DVM-333 JNI NewObject variants allocate VM slots and preserve Java failures") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const char* variant : {"NewObject", "NewObjectV", "NewObjectA"}) {
            CAPTURE(backend);
            CAPTURE(variant);
            ApplicationProcess f(backend);
            auto& bridge = *f.bridge;
            auto& vm = bridge.Vm();
            auto& linker = bridge.Linker();
            auto& env = f.session->Environment();
            auto& classes = f.session->Classes();
            auto& process = f.session->Process();
            const auto memory = process.GuestMemoryAccess();
            const auto scratch = memory.allocate(8);
            const auto clazz = [&](const char* descriptor) {
                return bridge.PublishLocal(vm.Model().ClassObject(linker.ResolveDescriptor(descriptor)));
            };
            // Passive registry lookup deliberately avoids GetMethodID's clinit.
            const auto constructor = [&](JniReference type, const char* descriptor) {
                const auto id = classes.GetMethodId(*env.ResolveObjectForHle(1, type), "<init>", descriptor, false);
                REQUIRE(id.has_value());
                return id->Value();
            };
            const auto create = [&](JniReference type, std::uint32_t method, JniReference argument = JniReference{}) {
                const auto word = argument.Value();
                std::array<std::byte, 8> data{};
                for (unsigned i = 0; i < 4; ++i)
                    data[i] = static_cast<std::byte>(word >> (8U * i));
                memory.write(scratch, data);
                const auto r3 = std::string_view(variant) == "NewObject" ? word : scratch.Value();
                return JniReference{process
                                        .Invoke({memory::GuestAddress{EnvironmentThunk(variant)},
                                                 {process.GuestEnvironment().Value(), type.Value(), method, r3},
                                                 {}})
                                        .return_value};
            };
            const auto pending = [&](const char* descriptor) {
                REQUIRE(env.ExceptionCheck(1));
                const auto error = env.ExceptionOccurred(1);
                env.ExceptionClear(1);
                CHECK(linker.Class(vm.Model().ObjectClass(bridge.FromReference(error))).descriptor == descriptor);
                env.DeleteLocalRef(1, error);
            };
            const auto url_class = clazz("Ljava/net/URL;");
            const auto url_ctor = constructor(url_class, "(Ljava/lang/String;)V");
            const auto text = bridge.PublishLocal(vm.NewStringUtf8("http://example.com/path"));
            const auto url = create(url_class, url_ctor, text);
            REQUIRE_FALSE(env.ExceptionCheck(1));
            REQUIRE_FALSE(url.IsNull());
            const auto object = bridge.FromReference(url);
            CHECK(vm.Model().Kind(object) == VmObjectKind::vm_instance);
            const auto protocol = linker.FindVtableIndex(linker.ResolveDescriptor("Ljava/net/URL;"), "getProtocol",
                                                         "()Ljava/lang/String;");
            REQUIRE(protocol.has_value());
            const auto result = vm.Call(linker.Class(vm.Model().ObjectClass(object)).vtable[*protocol],
                                        std::array{VmValue::Ref(object)});
            REQUIRE_FALSE(result.exception.IsValid());
            CHECK(vm.StringUtf8(result.value.ref) == "http");
            CHECK_THROWS_WITH_AS(static_cast<void>(vm.Model().InstanceSlots(bridge.FromReference(text))),
                                 ("object has no field slot board: class=Ljava/lang/String; kind=2 class_id=" +
                                  std::to_string(linker.ResolveDescriptor("Ljava/lang/String;").Value()))
                                     .c_str(),
                                 DexVmError);

            const auto child = linker.ResolveDescriptor("Lfixture/AllocChild;");
            const auto child_class = clazz("Lfixture/AllocChild;");
            const auto child_ctor = constructor(child_class, "()V");
            CHECK(linker.Class(child).clinit_state == ClinitState::uninitialized);
            const auto instance = create(child_class, child_ctor);
            REQUIRE_FALSE(env.ExceptionCheck(1));
            REQUIRE_FALSE(instance.IsNull());
            const auto static_value = [&](const char* name) {
                const auto field = linker.FindFieldRecursive(child, name, "I");
                REQUIRE(field.has_value());
                return linker.Class(child).static_storage[linker.Field(*field).slot];
            };
            CHECK(static_value("initialized") == 1);
            CHECK(static_value("constructed") == 7);
            const auto constructed_field = *linker.FindFieldRecursive(child, "constructed", "I");
            linker.MutableClass(child).static_storage[linker.Field(constructed_field).slot] = 0;
            for (const char* type : {"Lfixture/AllocAbstract;", "Ljava/lang/Runnable;"}) {
                const auto rejected = clazz(type);
                // A valid constructor ID with an observable side effect must
                // never be dispatched for an unallocatable class.
                CHECK(create(rejected, child_ctor).IsNull());
                pending("Ljava/lang/InstantiationException;");
                CHECK(static_value("constructed") == 0);
                env.DeleteLocalRef(1, rejected);
            }
            const auto failed = clazz("Lfixture/AllocFailInit;");
            CHECK(create(failed, child_ctor).IsNull());
            pending("Ljava/lang/ExceptionInInitializerError;");
            CHECK(static_value("constructed") == 0);
            const auto throwing = clazz("Lfixture/AllocThrowConstructor;");
            const auto thrown = create(throwing, constructor(throwing, "()V"));
            REQUIRE_FALSE(thrown.IsNull());
            pending("Ljava/lang/IllegalArgumentException;");
            CHECK(vm.Model().Kind(bridge.FromReference(thrown)) == VmObjectKind::vm_instance);
            const auto global = env.NewGlobalRef(1, thrown);
            env.DeleteLocalRef(1, thrown);
            static_cast<void>(vm.CollectGarbage("new-object-throwing-constructor"));
            CHECK(vm.Model().Kind(bridge.FromReference(global)) == VmObjectKind::vm_instance);
            env.DeleteGlobalRef(1, global);
            for (const auto ref : {url_class, text, url, child_class, instance, failed, throwing})
                env.DeleteLocalRef(1, ref);
            memory.release(scratch, 8);
        }
    }
}

TEST_CASE("DVM-242 JNI allocation translates exhausted VM heap to a pending Java error") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        constexpr std::uint64_t limit = 64 * 1024;
        ApplicationProcess f(backend, std::nullopt, "application.dex",
                             {.initial_heap_target_bytes = limit,
                              .heap_growth_limit_bytes = limit,
                              .maximum_heap_bytes = limit,
                              .min_free_bytes = 0,
                              .max_free_bytes = 0});
        auto& vm = f.bridge->Vm();
        auto& linker = vm.Linker();
        auto& env = f.session->Environment();
        const auto type = linker.ResolveDescriptor("Lfixture/AllocNoConstructor;");
        const auto klass = f.bridge->PublishLocal(vm.Model().ClassObject(type));
        REQUIRE_FALSE(vm.EnsureClassInitialized(type).exception.IsValid());
        const auto array_type = linker.ResolveDescriptor("[B");
        const auto occupied = vm.Model().AllocatedBytes();
        REQUIRE(occupied + 32 < limit);
        const auto padding = vm.Model().NewPrimitiveArray(array_type, JniPrimitiveKind::byte,
                                                          static_cast<JniSize>(limit - occupied - 32));
        const auto roots = vm.ProtectReferences(std::array{padding});
        CHECK(env.AllocObject(1, klass).IsNull());
        REQUIRE(env.ExceptionCheck(1));
        const auto error = env.ExceptionOccurred(1);
        env.ExceptionClear(1);
        CHECK(linker.Class(vm.Model().ObjectClass(f.bridge->FromReference(error))).descriptor ==
              "Ljava/lang/OutOfMemoryError;");
        env.DeleteLocalRef(1, error);
        env.DeleteLocalRef(1, klass);
    }
}

TEST_CASE("DVM-254 JNI lazily publishes skeleton members and rejects unsupported fields") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const bool enabled : {false, true}) {
        std::vector<std::uint8_t> skeleton;
        if (enabled) {
            std::ifstream input(std::string(OGPLAY_SOURCE_DIR) + "/data/dexvm/api19-skeleton.bin", std::ios::binary);
            REQUIRE(input.good());
            skeleton.assign(std::istreambuf_iterator<char>(input), {});
        }
        ApplicationProcess f(InterpreterBackend::switch_dispatch, std::nullopt, "application.dex", {},
                             std::move(skeleton));
        auto& session = *f.session;
        auto& env = session.Environment();
        auto& classes = session.Classes();
        auto& linker = f.bridge->Linker();
        memory::AddressSpace memory;
        memory::CheckedMemoryBus bus(memory);
        cpu::InterpreterCpu cpu(bus);
        GuestJniAbi abi(memory);
        const memory::GuestAddress data{0x70000000};
        memory.Map({data, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::write);
        JniGuestCallDispatcher dispatcher(f.ledger);
        JniJavaVm java_vm(env);
        JniGuestBindingContext context{env,
                                       classes,
                                       session.Invocations(),
                                       session.Fields(),
                                       session.Strings(),
                                       session.Arrays(),
                                       java_vm,
                                       session.Objects(),
                                       memory};
        BindJniGuestSlots(dispatcher, context);
        dispatcher.Seal();
        const auto text = [&](std::uint32_t offset, const std::string& value) {
            std::vector<std::byte> bytes;
            for (const auto c : value)
                bytes.push_back(static_cast<std::byte>(c));
            bytes.push_back(std::byte{});
            memory.Write(data.Add(offset), bytes);
            return data.Add(offset).Value();
        };
        const auto call = [&](const char* name, std::uint32_t a = 0, std::uint32_t b = 0, std::uint32_t c = 0) {
            const auto slot = FindJniSlot(name);
            REQUIRE(slot);
            const auto target = bus.Read32(kJniGuestEnvironmentTable.Add(slot->Value() * 4U));
            cpu::A32State state;
            state.SetThreadId(1);
            state.SetState(cpu::ExecutionState::thumb);
            state.SetRegister(cpu::CoreRegister::pc, target & ~1U);
            state.SetRegister(cpu::CoreRegister::r0, abi.Environment().Value());
            state.SetRegister(cpu::CoreRegister::r1, a);
            state.SetRegister(cpu::CoreRegister::r2, b);
            state.SetRegister(cpu::CoreRegister::r3, c);
            state.SetRegister(cpu::CoreRegister::sp, data.Add(0x800).Value());
            cpu.SetState(state);
            const auto handled = dispatcher.Handle(cpu, cpu.Run(1));
            REQUIRE(handled);
            return cpu.GetState().Register(cpu::CoreRegister::r0);
        };
        const auto lookup = [&](const char* slot, std::uint32_t owner, const char* name, const char* descriptor) {
            return call(slot, owner, text(0x100, name), text(0x200, descriptor));
        };
        const auto pending = [&](const char* expected) {
            REQUIRE(env.ExceptionCheck(1));
            const auto meta = env.PendingExceptionMetadata(1);
            if (meta && meta->exception_class.value != 0U)
                CHECK(classes.ClassName(meta->exception_class) == expected);
            else {
                const auto ref = env.ExceptionOccurred(1);
                env.ExceptionClear(1);
                const auto object = f.bridge->FromReference(ref);
                CHECK(linker.Class(f.bridge->Model().ObjectClass(object)).descriptor ==
                      "L" + std::string(expected) + ";");
            }
            env.ExceptionClear(1);
        };
        CHECK(linker.FindClass("Landroid/view/MotionEvent$PointerCoords;"));
        CHECK_FALSE(linker.FindClass("Landroid/os/NullVibrator;"));
        const auto lazy = call("FindClass", text(0, "android/os/NullVibrator"));
        if (!enabled) {
            CHECK(lazy == 0U);
            pending("java/lang/NoClassDefFoundError");
        } else {
            REQUIRE(lazy != 0U);
            const auto klass = linker.ResolveDescriptor("Landroid/os/NullVibrator;");
            const auto receiver = f.bridge->Model().NewInstance(klass, linker.Class(klass).instance_slots);
            const auto reference = f.bridge->PublishLocal(receiver);
            const auto ctor = lookup("GetMethodID", lazy, "<init>", "()V");
            REQUIRE(ctor != 0U);
            static_cast<void>(call("CallVoidMethod", reference.Value(), ctor));
            pending("java/lang/UnsatisfiedLinkError");
        }
        const auto owner = call("FindClass", text(0, "android/view/MotionEvent$PointerCoords"));
        REQUIRE(owner != 0U);
        CHECK(call("FindClass", text(0, "android/view/MotionEvent$PointerCoords")) != 0U);
        const auto type = linker.ResolveDescriptor("Landroid/view/MotionEvent$PointerCoords;");
        const auto object = f.bridge->Model().NewInstance(type, linker.Class(type).instance_slots);
        const auto object_ref = f.bridge->PublishLocal(object);
        for (const auto* name : {"<init>", "clear"}) {
            const auto method = lookup("GetMethodID", owner, name, "()V");
            REQUIRE(method != 0U);
            static_cast<void>(call("CallVoidMethod", object_ref.Value(), method));
            CHECK_FALSE(env.ExceptionCheck(1));
        }
        CHECK(lookup("GetStaticMethodID", owner, "createArray", "(I)[Landroid/view/MotionEvent$PointerCoords;") != 0U);
        const auto constant = lookup("GetStaticFieldID", owner, "INITIAL_PACKED_AXIS_VALUES", "I");
        REQUIRE(constant != 0U);
        CHECK(call("GetStaticIntField", owner, constant) == 8U);
        const auto field = lookup("GetFieldID", owner, "x", "F");
        REQUIRE(field != 0U);
        for (const auto* slot : {"GetFloatField", "SetFloatField"}) {
            static_cast<void>(call(slot, object_ref.Value(), field));
            CHECK_FALSE(env.ExceptionCheck(1));
        }
        static_cast<void>(call("SetFloatField", object_ref.Value(), field, 0x40200000U));
        CHECK_FALSE(env.ExceptionCheck(1));
        CHECK(call("GetFloatField", object_ref.Value(), field) == 0x40200000U);
        if (!enabled) continue;
        const auto point = call("FindClass", text(0, "android/graphics/PointF"));
        const auto creator = lookup("GetStaticFieldID", point, "CREATOR", "Landroid/os/Parcelable$Creator;");
        REQUIRE(creator != 0U);
        for (const auto* slot : {"GetStaticObjectField", "SetStaticObjectField"}) {
            static_cast<void>(call(slot, point, creator));
            pending("java/lang/UnsatisfiedLinkError");
        }
        CHECK(lookup("GetMethodID", owner, "absent", "()V") == 0U);
        const auto missing = JniReference{call("ExceptionOccurred")};
        const auto pending_identity = env.PendingExceptionMetadata(1)->throwable;
        CHECK(call("GetObjectClass", missing.Value()) != 0U);
        CHECK(env.PendingExceptionMetadata(1)->throwable == pending_identity);
        CHECK_FALSE(env.PendingCallDiagnostics().empty());
        env.ExceptionClear(1);
        const auto error_type = JniReference{call("GetObjectClass", missing.Value())};
        CHECK(linker.Class(f.bridge->Model().ClassOfClassObject(f.bridge->FromReference(error_type))).descriptor ==
              "Ljava/lang/NoSuchMethodError;");
        const auto wide = lookup("GetFieldID", owner, "mPackedAxisBits", "J");
        REQUIRE(wide != 0U);
        for (const auto* slot : {"GetLongField", "SetLongField"}) {
            static_cast<void>(call(slot, object_ref.Value(), wide));
            CHECK_FALSE(env.ExceptionCheck(1));
        }
        const auto hits = f.ledger.Unimplemented();
        CHECK(std::none_of(hits.begin(), hits.end(), [](const auto& hit) {
            return hit.id.find("MotionEvent$PointerCoords;") != std::string::npos;
        }));
        CHECK(std::any_of(hits.begin(), hits.end(), [](const auto& hit) {
            return hit.id ==
                       "dexvm.skeleton.field.Landroid/graphics/PointF;->CREATOR:Landroid/os/Parcelable$Creator;" &&
                   hit.count == 2;
        }));
    }
}

TEST_CASE("gap survey records JNI-channel hits on skeleton members and answers neutrally") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const bool survey : {false, true}) {
        std::vector<std::uint8_t> skeleton;
        {
            std::ifstream input(std::string(OGPLAY_SOURCE_DIR) + "/data/dexvm/api19-skeleton.bin", std::ios::binary);
            REQUIRE(input.good());
            skeleton.assign(std::istreambuf_iterator<char>(input), {});
        }
        ApplicationProcess f(InterpreterBackend::switch_dispatch, std::nullopt, "application.dex", {},
                             std::move(skeleton));
        auto& session = *f.session;
        auto& env = session.Environment();
        auto& classes = session.Classes();
        auto& linker = f.bridge->Linker();
        if (survey) linker.EnableGapSurvey();
        memory::AddressSpace memory;
        memory::CheckedMemoryBus bus(memory);
        cpu::InterpreterCpu cpu(bus);
        GuestJniAbi abi(memory);
        const memory::GuestAddress data{0x70000000};
        memory.Map({data, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::write);
        JniGuestCallDispatcher dispatcher(f.ledger);
        JniJavaVm java_vm(env);
        JniGuestBindingContext context{env,
                                       classes,
                                       session.Invocations(),
                                       session.Fields(),
                                       session.Strings(),
                                       session.Arrays(),
                                       java_vm,
                                       session.Objects(),
                                       memory};
        BindJniGuestSlots(dispatcher, context);
        dispatcher.Seal();
        const auto text = [&](std::uint32_t offset, const std::string& value) {
            std::vector<std::byte> bytes;
            for (const auto c : value)
                bytes.push_back(static_cast<std::byte>(c));
            bytes.push_back(std::byte{});
            memory.Write(data.Add(offset), bytes);
            return data.Add(offset).Value();
        };
        const auto call = [&](const char* name, std::uint32_t a = 0, std::uint32_t b = 0, std::uint32_t c = 0) {
            const auto slot = FindJniSlot(name);
            REQUIRE(slot);
            const auto target = bus.Read32(kJniGuestEnvironmentTable.Add(slot->Value() * 4U));
            cpu::A32State state;
            state.SetThreadId(1);
            state.SetState(cpu::ExecutionState::thumb);
            state.SetRegister(cpu::CoreRegister::pc, target & ~1U);
            state.SetRegister(cpu::CoreRegister::r0, abi.Environment().Value());
            state.SetRegister(cpu::CoreRegister::r1, a);
            state.SetRegister(cpu::CoreRegister::r2, b);
            state.SetRegister(cpu::CoreRegister::r3, c);
            state.SetRegister(cpu::CoreRegister::sp, data.Add(0x800).Value());
            cpu.SetState(state);
            const auto handled = dispatcher.Handle(cpu, cpu.Run(1));
            REQUIRE(handled);
            return cpu.GetState().Register(cpu::CoreRegister::r0);
        };
        const auto lookup = [&](const char* slot, std::uint32_t owner, const char* name, const char* descriptor) {
            return call(slot, owner, text(0x100, name), text(0x200, descriptor));
        };
        const auto pending = [&](const char* expected) {
            REQUIRE(env.ExceptionCheck(1));
            const auto meta = env.PendingExceptionMetadata(1);
            if (meta && meta->exception_class.value != 0U)
                CHECK(classes.ClassName(meta->exception_class) == expected);
            else {
                const auto ref = env.ExceptionOccurred(1);
                env.ExceptionClear(1);
                const auto object = f.bridge->FromReference(ref);
                CHECK(linker.Class(f.bridge->Model().ObjectClass(object)).descriptor ==
                      "L" + std::string(expected) + ";");
            }
            env.ExceptionClear(1);
        };
        const auto owner = call("FindClass", text(0, "android/graphics/PointF"));
        REQUIRE(owner != 0U);
        const auto type = linker.ResolveDescriptor("Landroid/graphics/PointF;");
        const auto object = f.bridge->Model().NewInstance(type, linker.Class(type).instance_slots);
        const auto object_ref = f.bridge->PublishLocal(object);
        const auto negate = lookup("GetMethodID", owner, "negate", "()V");
        REQUIRE(negate != 0U);
        const auto field = lookup("GetStaticFieldID", owner, "CREATOR", "Landroid/os/Parcelable$Creator;");
        REQUIRE(field != 0U);
        static_cast<void>(call("CallVoidMethod", object_ref.Value(), negate));
        if (!survey) {
            // Quirk control: without survey both accesses still fail closed.
            CHECK(linker.GapSurveyHits().empty());
            pending("java/lang/UnsatisfiedLinkError");
            static_cast<void>(call("GetStaticObjectField", owner, field));
            pending("java/lang/UnsatisfiedLinkError");
            continue;
        }
        static_cast<void>(call("GetStaticObjectField", owner, field));
        CHECK_FALSE(env.ExceptionCheck(1));
        // A member API 19 does not declare is still a lookup miss, not a stub.
        CHECK(lookup("GetMethodID", owner, "absent", "()V") == 0U);
        env.ExceptionClear(1);
        const auto hits = linker.GapSurveyHits();
        const auto find = [&](const char* member) -> const dexvm::GapSurveyHit* {
            for (const auto& hit : hits) {
                if (hit.owner_descriptor == "Landroid/graphics/PointF;" && hit.member == member) return &hit;
            }
            return nullptr;
        };
        const auto* method = find("negate()V");
        REQUIRE(method != nullptr);
        CHECK(method->source == dexvm::GapSurveySource::skeleton);
        CHECK(method->first_channel == dexvm::GapSurveyChannel::jni);
        CHECK(method->channel_hits[static_cast<std::size_t>(dexvm::GapSurveyChannel::jni)] == 1U);
        const auto* member = find("CREATOR:Landroid/os/Parcelable$Creator;");
        REQUIRE(member != nullptr);
        CHECK(member->kind == dexvm::GapSurveyKind::field);
        CHECK(member->first_channel == dexvm::GapSurveyChannel::jni);
        CHECK(member->source == dexvm::GapSurveySource::skeleton);
        CHECK(find("absent()V") == nullptr);
    }
}
