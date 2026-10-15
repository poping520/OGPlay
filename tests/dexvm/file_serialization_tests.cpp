#include "boot_dex.h"
// java.io.File through the shared VFS (SBX-5, ADR-0020). Two things matter
// here: a Java save and a native read see one filesystem, and File.mkdirs
// reports the truth. The old handler returned true unconditionally without
// creating anything, so the mkdirs case below fails if that ever returns.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/apk.h"
#include "ogplay/audio/encoded_music.h"
#include "ogplay/audio/java_sound_pool_mixer.h"
#include "ogplay/audio/pcm_mix.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_io_vfs.h"
#include "ogplay/runtime/vfs/sandbox_store.h"
#include "ogplay/runtime/vfs/vfs.h"

#include "file_vfs_fixture.h"

using namespace ogplay::test::file_vfs;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

namespace {

std::vector<IntrinsicClassDecl> ObjectSerializationTestIntrinsics() {
    std::vector<IntrinsicClassDecl> result;
    auto mode = IntrinsicClassBuilder::Class(
        "Ltest/SerializationMode;", "Ljava/lang/Enum;", {},
        kAccPublic | kAccFinal | kAccEnum);
    mode.StaticField("ACTIVE", "Ltest/SerializationMode;",
                     kAccPublic | kAccStatic | kAccFinal | kAccEnum);
    result.push_back(std::move(mode).Build());

    auto node = IntrinsicClassBuilder::Class(
        "Ltest/SerializableNode;", "Ljava/lang/Object;",
        {"Ljava/io/Serializable;"});
    node.ConstantInt("serialVersionUID", "J", 42, kAccPrivate);
    node.InstanceField("id", "I", kAccPrivate);
    node.InstanceField("name", "Ljava/lang/String;", kAccPrivate);
    node.InstanceField("alias", "Ljava/lang/String;", kAccPrivate);
    node.InstanceField("next", "Ltest/SerializableNode;", kAccPrivate);
    node.InstanceField("mode", "Ltest/SerializationMode;", kAccPrivate);
    node.InstanceField("date", "Ljava/util/Date;", kAccPrivate);
    result.push_back(std::move(node).Build());
    return result;
}

} // namespace

TEST_CASE("Object streams round trip bounded API19 primitive and string data") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" :
                                                         "switch");
        InterpreterConfig config;
        config.backend = backend;
        FileVm vm(nullptr, false, config);
        VmThreadRuntime threads(vm.interpreter);

        const auto byte_output = vm.interpreter.NewIntrinsicInstance(
            "Ljava/io/ByteArrayOutputStream;");
        static_cast<void>(vm.CallOn(byte_output, "<init>", "()V"));
        const auto output = vm.interpreter.NewIntrinsicInstance(
            "Ljava/io/ObjectOutputStream;");
        static_cast<void>(vm.CallOn(
            output, "<init>", "(Ljava/io/OutputStream;)V",
            {VmValue::Ref(byte_output)}));

        static_cast<void>(vm.CallOn(output, "writeBoolean", "(Z)V",
                                    {VmValue::Int(1)}));
        static_cast<void>(vm.CallOn(output, "writeByte", "(I)V",
                                    {VmValue::Int(-2)}));
        static_cast<void>(vm.CallOn(output, "writeShort", "(I)V",
                                    {VmValue::Int(0x8123)}));
        static_cast<void>(vm.CallOn(output, "writeChar", "(I)V",
                                    {VmValue::Int(0x4e2d)}));
        static_cast<void>(vm.CallOn(output, "writeInt", "(I)V",
                                    {VmValue::Int(0x12345678)}));
        static_cast<void>(vm.CallOn(
            output, "writeLong", "(J)V",
            {VmValue::Long(static_cast<std::int64_t>(0x8123456789abcdefULL))}));
        static_cast<void>(vm.CallOn(output, "writeFloat", "(F)V",
                                    {VmValue::Float(1.25F)}));
        static_cast<void>(vm.CallOn(output, "writeDouble", "(D)V",
                                    {VmValue::Double(-2.5)}));
        static_cast<void>(vm.CallOn(
            output, "writeBytes", "(Ljava/lang/String;)V",
            {VmValue::Ref(vm.interpreter.NewStringUtf8("AZ"))}));
        static_cast<void>(vm.CallOn(
            output, "writeChars", "(Ljava/lang/String;)V",
            {VmValue::Ref(vm.interpreter.NewStringUtf8("中"))}));
        static_cast<void>(vm.CallOn(
            output, "writeUTF", "(Ljava/lang/String;)V",
            {VmValue::Ref(vm.interpreter.NewStringUtf8("save"))}));

        const auto byte_array_class = vm.linker.ResolveDescriptor("[B");
        const auto raw = vm.model.NewPrimitiveArray(
            byte_array_class, JniPrimitiveKind::byte, 7);
        vm.model.WriteByteRegion(
            raw, 0,
            std::array{std::byte{'a'}, std::byte{'b'}, std::byte{'c'},
                       std::byte{'d'}, std::byte{'e'}, std::byte{'f'},
                       std::byte{'g'}});
        static_cast<void>(vm.CallOn(output, "write", "([BII)V",
                                    {VmValue::Ref(raw), VmValue::Int(0),
                                     VmValue::Int(3)}));
        static_cast<void>(vm.CallOn(output, "write", "([BII)V",
                                    {VmValue::Ref(raw), VmValue::Int(3),
                                     VmValue::Int(4)}));
        static_cast<void>(vm.CallOn(
            output, "writeBytes", "(Ljava/lang/String;)V",
            {VmValue::Ref(vm.interpreter.NewStringUtf8("line\n"))}));
        static_cast<void>(vm.CallOn(output, "write", "([B)V",
                                    {VmValue::Ref(raw)}));
        const auto bulk = vm.model.NewPrimitiveArray(
            byte_array_class, JniPrimitiveKind::byte, 300);
        std::vector<std::byte> bulk_bytes(300);
        for (std::size_t index = 0; index < bulk_bytes.size(); ++index) {
            bulk_bytes[index] = static_cast<std::byte>(index & 0xffU);
        }
        vm.model.WriteByteRegion(bulk, 0, bulk_bytes);
        static_cast<void>(vm.CallOn(output, "write", "([B)V",
                                    {VmValue::Ref(bulk)}));
        static_cast<void>(vm.CallOn(
            output, "writeObject", "(Ljava/lang/Object;)V",
            {VmValue::Ref(VmObjectRef{})}));
        static_cast<void>(vm.CallOn(
            output, "writeObject", "(Ljava/lang/Object;)V",
            {VmValue::Ref(vm.interpreter.NewStringUtf8("slot"))}));
        static_cast<void>(vm.CallOn(output, "flush", "()V"));

        CHECK(vm.interpreter.IO().FindOutput(output) == nullptr);
        static_cast<void>(vm.CallOn(output, "flush", "()V"));
        const auto wire = vm.CallOn(byte_output, "toByteArray", "()[B").ref;
        const auto encoded = vm.model.ReadByteRegion(wire, 0, vm.model.ArrayLength(wire));
        REQUIRE(encoded.size() > 4U);
        CHECK(encoded[0] == std::byte{0xac});
        CHECK(encoded[1] == std::byte{0xed});
        CHECK(encoded[2] == std::byte{0x00});
        CHECK(encoded[3] == std::byte{0x05});

        const auto source_array = vm.model.NewPrimitiveArray(
            byte_array_class, JniPrimitiveKind::byte,
            static_cast<JniSize>(encoded.size()));
        vm.model.WriteByteRegion(source_array, 0, encoded);
        const auto byte_input = vm.interpreter.NewIntrinsicInstance(
            "Ljava/io/ByteArrayInputStream;");
        static_cast<void>(vm.CallOn(byte_input, "<init>", "([B)V",
                                    {VmValue::Ref(source_array)}));
        const auto input = vm.interpreter.NewIntrinsicInstance(
            "Ljava/io/ObjectInputStream;");
        static_cast<void>(vm.CallOn(
            input, "<init>", "(Ljava/io/InputStream;)V",
            {VmValue::Ref(byte_input)}));

        CHECK(vm.CallOn(input, "available", "()I").AsInt() == 359);
        CHECK(vm.CallOn(input, "readBoolean", "()Z").AsInt() == 1);
        CHECK(vm.CallOn(input, "readByte", "()B").AsInt() == -2);
        CHECK(vm.CallOn(input, "readShort", "()S").AsInt() == -32477);
        CHECK(vm.CallOn(input, "readChar", "()C").AsInt() == 0x4e2d);
        CHECK(vm.CallOn(input, "readInt", "()I").AsInt() == 0x12345678);
        CHECK(static_cast<std::uint64_t>(
                  vm.CallOn(input, "readLong", "()J").AsLong()) ==
              0x8123456789abcdefULL);
        CHECK(vm.CallOn(input, "readFloat", "()F").AsFloat() ==
              doctest::Approx(1.25F));
        CHECK(vm.CallOn(input, "readDouble", "()D").AsDouble() ==
              doctest::Approx(-2.5));
        CHECK(vm.CallOn(input, "readUnsignedByte", "()I").AsInt() == 'A');
        CHECK(vm.CallOn(input, "readUnsignedByte", "()I").AsInt() == 'Z');
        CHECK(vm.CallOn(input, "readUnsignedShort", "()I").AsInt() ==
              0x4e2d);
        CHECK(vm.interpreter.StringUtf8(vm.CallOn(
                  input, "readUTF", "()Ljava/lang/String;").ref) == "save");

        const auto first = vm.model.NewPrimitiveArray(
            byte_array_class, JniPrimitiveKind::byte, 3);
        static_cast<void>(vm.CallOn(input, "readFully", "([B)V",
                                    {VmValue::Ref(first)}));
        CHECK(vm.model.ReadByteRegion(first, 0, 3) ==
              std::vector<std::byte>{std::byte{'a'}, std::byte{'b'},
                                     std::byte{'c'}});
        const auto second = vm.model.NewPrimitiveArray(
            byte_array_class, JniPrimitiveKind::byte, 6);
        static_cast<void>(vm.CallOn(input, "readFully", "([BII)V",
                                    {VmValue::Ref(second), VmValue::Int(1),
                                     VmValue::Int(4)}));
        CHECK(vm.model.ReadByteRegion(second, 0, 6) ==
              std::vector<std::byte>{std::byte{0}, std::byte{'d'},
                                     std::byte{'e'}, std::byte{'f'},
                                     std::byte{'g'}, std::byte{0}});
        CHECK(vm.interpreter.StringUtf8(vm.CallOn(
                  input, "readLine", "()Ljava/lang/String;").ref) == "line");
        CHECK(vm.CallOn(input, "skip", "(J)J", {VmValue::Long(2)})
                  .AsLong() == 2);
        CHECK(vm.CallOn(input, "read", "()I").AsInt() == 'c');
        CHECK(vm.CallOn(input, "skipBytes", "(I)I", {VmValue::Int(2)})
                  .AsInt() == 2);
        const auto tail = vm.model.NewPrimitiveArray(
            byte_array_class, JniPrimitiveKind::byte, 2);
        CHECK(vm.CallOn(input, "read", "([B)I", {VmValue::Ref(tail)})
                  .AsInt() == 2);
        CHECK(vm.model.ReadByteRegion(tail, 0, 2) ==
              std::vector<std::byte>{std::byte{'f'}, std::byte{'g'}});
        const auto bulk_result = vm.model.NewPrimitiveArray(
            byte_array_class, JniPrimitiveKind::byte, 300);
        static_cast<void>(vm.CallOn(input, "readFully", "([B)V",
                                    {VmValue::Ref(bulk_result)}));
        CHECK(vm.model.ReadByteRegion(bulk_result, 0, 300) == bulk_bytes);
        CHECK_FALSE(vm.CallOn(input, "readObject", "()Ljava/lang/Object;")
                        .ref.IsValid());
        CHECK(vm.interpreter.StringUtf8(vm.CallOn(
                  input, "readObject", "()Ljava/lang/Object;").ref) ==
              "slot");
        CHECK(vm.CallOn(input, "available", "()I").AsInt() == 0);
        static_cast<void>(vm.CallOn(input, "close", "()V"));
        static_cast<void>(vm.CallOn(output, "close", "()V"));
    }
}

TEST_CASE("Object streams round trip default Serializable graphs") {
    const auto extra = ObjectSerializationTestIntrinsics();
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" :
                                                         "switch");
        InterpreterConfig config;
        config.backend = backend;
        FileVm vm(nullptr, false, config, extra, true);
        VmThreadRuntime threads(vm.interpreter);

        const auto field = [&](const std::string_view owner,
                               const std::string_view name,
                               const std::string_view descriptor) {
            const auto java_class = vm.linker.ResolveDescriptor(owner);
            const auto result = vm.linker.FindFieldRecursive(
                java_class, std::string(name), std::string(descriptor));
            REQUIRE(result.has_value());
            return *result;
        };
        const auto set_bits = [&](const VmObjectRef object,
                                  const VmFieldId field_id,
                                  const std::uint64_t bits) {
            const auto& linked = vm.linker.Field(field_id);
            auto slots = vm.model.InstanceSlots(object);
            slots[linked.slot] = {
                static_cast<std::uint32_t>(bits),
                linked.is_ref ? SlotTag::ref
                              : linked.is_wide ? SlotTag::wide_lo
                                               : SlotTag::cat1};
            if (linked.is_wide) {
                slots[linked.slot + 1U] = {
                    static_cast<std::uint32_t>(bits >> 32U),
                    SlotTag::wide_hi};
            }
        };
        const auto get_bits = [&](const VmObjectRef object,
                                  const VmFieldId field_id) {
            const auto& linked = vm.linker.Field(field_id);
            const auto slots = vm.model.InstanceSlots(object);
            auto bits = static_cast<std::uint64_t>(slots[linked.slot].bits);
            if (linked.is_wide) {
                bits |= static_cast<std::uint64_t>(
                            slots[linked.slot + 1U].bits)
                        << 32U;
            }
            return bits;
        };

        const auto mode_class =
            vm.linker.ResolveDescriptor("Ltest/SerializationMode;");
        const auto mode_initialized =
            vm.interpreter.EnsureClassInitialized(mode_class);
        REQUIRE_FALSE(mode_initialized.exception.IsValid());
        const auto mode = vm.interpreter.NewIntrinsicInstance(
            "Ltest/SerializationMode;");
        const auto active_name = vm.interpreter.NewStringUtf8("ACTIVE");
        set_bits(mode,
                 field("Ljava/lang/Enum;", "name", "Ljava/lang/String;"),
                 active_name.Value());
        set_bits(mode, field("Ljava/lang/Enum;", "ordinal", "I"), 0U);
        const auto mode_static = field(
            "Ltest/SerializationMode;", "ACTIVE",
            "Ltest/SerializationMode;");
        vm.linker.MutableClass(mode_class)
            .static_storage[vm.linker.Field(mode_static).slot] = mode.Value();

        const auto date =
            vm.interpreter.NewIntrinsicInstance("Ljava/util/Date;");
        set_bits(date, field("Ljava/util/Date;", "milliseconds", "J"),
                 0x0102030405060708ULL);

        const auto node = vm.interpreter.NewIntrinsicInstance(
            "Ltest/SerializableNode;");
        const auto shared_name = vm.interpreter.NewStringUtf8("shared");
        set_bits(node, field("Ltest/SerializableNode;", "id", "I"), 37U);
        set_bits(node,
                 field("Ltest/SerializableNode;", "name",
                       "Ljava/lang/String;"),
                 shared_name.Value());
        set_bits(node,
                 field("Ltest/SerializableNode;", "alias",
                       "Ljava/lang/String;"),
                 shared_name.Value());
        set_bits(node,
                 field("Ltest/SerializableNode;", "next",
                       "Ltest/SerializableNode;"),
                 node.Value());
        set_bits(node,
                 field("Ltest/SerializableNode;", "mode",
                       "Ltest/SerializationMode;"),
                 mode.Value());
        set_bits(node,
                 field("Ltest/SerializableNode;", "date",
                       "Ljava/util/Date;"),
                 date.Value());

        const auto byte_output = vm.interpreter.NewIntrinsicInstance(
            "Ljava/io/ByteArrayOutputStream;");
        static_cast<void>(vm.CallOn(byte_output, "<init>", "()V"));
        const auto output = vm.interpreter.NewIntrinsicInstance(
            "Ljava/io/ObjectOutputStream;");
        static_cast<void>(vm.CallOn(
            output, "<init>", "(Ljava/io/OutputStream;)V",
            {VmValue::Ref(byte_output)}));
        static_cast<void>(vm.CallOn(
            output, "writeObject", "(Ljava/lang/Object;)V",
            {VmValue::Ref(node)}));
        static_cast<void>(vm.CallOn(
            output, "writeObject", "(Ljava/lang/Object;)V",
            {VmValue::Ref(node)}));

        CHECK(vm.interpreter.IO().FindOutput(output) == nullptr);
        static_cast<void>(vm.CallOn(output, "flush", "()V"));
        const auto wire = vm.CallOn(byte_output, "toByteArray", "()[B").ref;
        const auto encoded = vm.model.ReadByteRegion(wire, 0, vm.model.ArrayLength(wire));
        const auto bytes = vm.model.NewPrimitiveArray(
            vm.linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte,
            static_cast<JniSize>(encoded.size()));
        vm.model.WriteByteRegion(bytes, 0, encoded);
        const auto byte_input = vm.interpreter.NewIntrinsicInstance(
            "Ljava/io/ByteArrayInputStream;");
        static_cast<void>(vm.CallOn(byte_input, "<init>", "([B)V",
                                    {VmValue::Ref(bytes)}));
        const auto input = vm.interpreter.NewIntrinsicInstance(
            "Ljava/io/ObjectInputStream;");
        static_cast<void>(vm.CallOn(
            input, "<init>", "(Ljava/io/InputStream;)V",
            {VmValue::Ref(byte_input)}));

        const auto restored =
            vm.CallOn(input, "readObject", "()Ljava/lang/Object;").ref;
        REQUIRE(restored.IsValid());
        CHECK(vm.model.ObjectClass(restored) ==
              vm.linker.ResolveDescriptor("Ltest/SerializableNode;"));
        CHECK(get_bits(restored,
                       field("Ltest/SerializableNode;", "id", "I")) == 37U);
        const auto restored_name = VmObjectRef(static_cast<std::uint32_t>(
            get_bits(restored,
                     field("Ltest/SerializableNode;", "name",
                           "Ljava/lang/String;"))));
        const auto restored_alias = VmObjectRef(static_cast<std::uint32_t>(
            get_bits(restored,
                     field("Ltest/SerializableNode;", "alias",
                           "Ljava/lang/String;"))));
        CHECK(vm.interpreter.StringUtf8(restored_name) == "shared");
        CHECK(restored_alias == restored_name);
        CHECK(VmObjectRef(static_cast<std::uint32_t>(
                  get_bits(restored,
                           field("Ltest/SerializableNode;", "next",
                                 "Ltest/SerializableNode;")))) == restored);
        const auto restored_mode = VmObjectRef(static_cast<std::uint32_t>(
            get_bits(restored,
                     field("Ltest/SerializableNode;", "mode",
                           "Ltest/SerializationMode;"))));
        CHECK(restored_mode == mode);
        const auto restored_date = VmObjectRef(static_cast<std::uint32_t>(
            get_bits(restored,
                     field("Ltest/SerializableNode;", "date",
                           "Ljava/util/Date;"))));
        REQUIRE(restored_date.IsValid());
        CHECK(get_bits(restored_date,
                       field("Ljava/util/Date;", "milliseconds", "J")) ==
              0x0102030405060708ULL);
        CHECK(vm.CallOn(input, "readObject", "()Ljava/lang/Object;").ref ==
              restored);
    }
}

TEST_CASE("Object streams reject nonserializable objects and invalid headers") {
    FileVm vm(nullptr, false);
    VmThreadRuntime threads(vm.interpreter);
    const auto byte_output = vm.interpreter.NewIntrinsicInstance(
        "Ljava/io/ByteArrayOutputStream;");
    static_cast<void>(vm.CallOn(byte_output, "<init>", "()V"));
    const auto output = vm.interpreter.NewIntrinsicInstance(
        "Ljava/io/ObjectOutputStream;");
    static_cast<void>(vm.CallOn(output, "<init>",
                                "(Ljava/io/OutputStream;)V",
                                {VmValue::Ref(byte_output)}));
    const auto unsupported = vm.CallOnOutcome(
        output, "writeObject", "(Ljava/lang/Object;)V",
        {VmValue::Ref(output)});
    REQUIRE(unsupported.exception.IsValid());
    CHECK(vm.linker.Class(unsupported.exception_class).descriptor ==
          "Ljava/io/NotSerializableException;");

    const auto invalid_bytes = vm.model.NewPrimitiveArray(
        vm.linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte, 4);
    const auto byte_input = vm.interpreter.NewIntrinsicInstance(
        "Ljava/io/ByteArrayInputStream;");
    static_cast<void>(vm.CallOn(byte_input, "<init>", "([B)V",
                                {VmValue::Ref(invalid_bytes)}));
    const auto input = vm.interpreter.NewIntrinsicInstance(
        "Ljava/io/ObjectInputStream;");
    const auto invalid = vm.CallOnOutcome(
        input, "<init>", "(Ljava/io/InputStream;)V",
        {VmValue::Ref(byte_input)});
    REQUIRE(invalid.exception.IsValid());
    CHECK(vm.linker.Class(invalid.exception_class).descriptor ==
          "Ljava/io/StreamCorruptedException;");
}
