#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/dexvm/class_name_codec.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"

namespace {

using ogplay::runtime::dexvm::ClassNameCodec;
using ogplay::runtime::dexvm::ClassNameCodecError;
using namespace ogplay::runtime::dexvm;

std::vector<std::uint8_t> ReadFixture(const std::string& name) {
    const std::string path =
        std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), "missing fixture: ", path);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                     std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("ClassNameCodec parses method descriptors without losing type identity") {
    const auto method = ClassNameCodec::ParseMethod(
        "(ILjava/lang/String;[[J)[Ljava/lang/Object;");
    REQUIRE(method.parameters.size() == 3);
    CHECK(method.parameters[0] == "I");
    CHECK(method.parameters[1] == "Ljava/lang/String;");
    CHECK(method.parameters[2] == "[[J");
    CHECK(method.return_type == "[Ljava/lang/Object;");

    const auto empty = ClassNameCodec::ParseMethod("()V");
    CHECK(empty.parameters.empty());
    CHECK(empty.return_type == "V");

    // AOSP API19: .local/aosp/dalvik/vm/oo/Class.cpp :: loadClassFromDex0
    CHECK_THROWS_AS(static_cast<void>(ClassNameCodec::ParseMethod("(V)V")),
                    ClassNameCodecError);
    CHECK_THROWS_AS(static_cast<void>(ClassNameCodec::ParseMethod("(I")),
                    ClassNameCodecError);
    CHECK_THROWS_AS(static_cast<void>(ClassNameCodec::ParseMethod("()VV")),
                    ClassNameCodecError);
    CHECK_THROWS_AS(static_cast<void>(ClassNameCodec::ParseMethod(
                        "(" + std::string(256, 'I') + ")V")),
                    ClassNameCodecError);
}

TEST_CASE("ClassNameCodec classifies complete field and class descriptors") {
    CHECK(ClassNameCodec::IsPrimitive("I"));
    CHECK(ClassNameCodec::IsPrimitive("V"));
    CHECK_FALSE(ClassNameCodec::IsPrimitive("[I"));
    CHECK_FALSE(ClassNameCodec::IsPrimitive("II"));

    CHECK(ClassNameCodec::IsReference("Ljava/lang/String;"));
    CHECK(ClassNameCodec::IsReference("[I"));
    CHECK(ClassNameCodec::IsReference("[[Ljava/lang/String;"));
    CHECK_FALSE(ClassNameCodec::IsReference("I"));
    CHECK_FALSE(ClassNameCodec::IsReference("[V"));
    CHECK_FALSE(ClassNameCodec::IsReference("Ljava.lang.String;"));

    CHECK(ClassNameCodec::IsArray("[I"));
    CHECK(ClassNameCodec::IsArray("[[Ljava/lang/String;"));
    CHECK_FALSE(ClassNameCodec::IsArray("Ljava/lang/String;"));
    CHECK_FALSE(ClassNameCodec::IsArray("["));
}

TEST_CASE("ClassNameCodec renders API19 Class getName spellings") {
    // AOSP API19: .local/aosp/libcore/libdvm/src/main/java/java/lang/Class.java :: getName
    CHECK(ClassNameCodec::ClassGetName("Ljava/lang/String;") ==
          "java.lang.String");
    CHECK(ClassNameCodec::ClassGetName("I") == "int");
    CHECK(ClassNameCodec::ClassGetName("V") == "void");
    CHECK(ClassNameCodec::ClassGetName("[I") == "[I");
    CHECK(ClassNameCodec::ClassGetName("[Ljava/lang/String;") ==
          "[Ljava.lang.String;");
    CHECK_THROWS_AS(static_cast<void>(ClassNameCodec::ClassGetName(
                        "Ljava.lang.String;")),
                    ClassNameCodecError);
    CHECK_THROWS_AS(static_cast<void>(ClassNameCodec::ClassGetName("[V")),
                    ClassNameCodecError);
}

TEST_CASE("ClassNameCodec converts forName binary names to descriptors") {
    // AOSP API19: .local/aosp/dalvik/vm/native/java_lang_Class.cpp :: Dalvik_java_lang_Class_classForName
    CHECK(ClassNameCodec::BinaryNameToDescriptor("java.lang.String") ==
          "Ljava/lang/String;");
    CHECK(ClassNameCodec::BinaryNameToDescriptor("foo.Bar$Inner") ==
          "Lfoo/Bar$Inner;");
    CHECK(ClassNameCodec::BinaryNameToDescriptor("[I") == "[I");
    CHECK(ClassNameCodec::BinaryNameToDescriptor("[Ljava.lang.String;") ==
          "[Ljava/lang/String;");

    CHECK_THROWS_AS(static_cast<void>(
                        ClassNameCodec::BinaryNameToDescriptor("int")),
                    ClassNameCodecError);
    CHECK_THROWS_AS(static_cast<void>(ClassNameCodec::BinaryNameToDescriptor(
                        "java/lang/String")),
                    ClassNameCodecError);
    CHECK_THROWS_AS(static_cast<void>(ClassNameCodec::BinaryNameToDescriptor(
                        "java..lang.String")),
                    ClassNameCodecError);
    CHECK_THROWS_AS(static_cast<void>(
                        ClassNameCodec::BinaryNameToDescriptor("[V")),
                    ClassNameCodecError);
    CHECK_THROWS_AS(static_cast<void>(ClassNameCodec::BinaryNameToDescriptor(
                        std::string(256, '[') + "I")),
                    ClassNameCodecError);
}

TEST_CASE("reflection linker metadata preserves intrinsic declarations") {
    auto parent = IntrinsicClassBuilder::Interface("Ltest/Parent;");
    parent.UnimplementedVirtual("parent", "()V", 0x0401U);

    auto child = IntrinsicClassBuilder::Interface(
        "Ltest/Child;", {"Ltest/Parent;"});
    child.UnimplementedVirtual("child", "()V", 0x0401U);

    auto owner = IntrinsicClassBuilder::Class(
        "Ltest/Owner;", "Ljava/lang/Object;", {"Ltest/Child;"}, 0x0011U);
    owner.UnimplementedDirect("hidden", "()V", 0x0002U);
    owner.UnimplementedStatic("make", "()Ltest/Owner;", 0x0004U);
    owner.UnimplementedVirtual("run", "()V", 0x0001U);
    owner.UnimplementedFinal("done", "()V", 0x0004U);
    owner.InstanceField("secret", "I", 0x0002U);
    owner.StaticField("state", "J", 0x0044U);

    auto catalog = CoreIntrinsicCatalog();
    catalog.push_back(std::move(parent).Build());
    catalog.push_back(std::move(child).Build());
    catalog.push_back(std::move(owner).Build());

    DexClassLinker linker;
    linker.RegisterIntrinsics(catalog);
    linker.Link();

    const auto owner_id = linker.FindClass("Ltest/Owner;");
    REQUIRE(owner_id.has_value());
    const auto& linked = linker.Class(*owner_id);
    CHECK(linked.defining_loader == kBootstrapLoader);
    CHECK(linked.access_flags == 0x0011U);
    REQUIRE(linked.direct_interfaces.size() == 1);
    CHECK(linker.Class(linked.direct_interfaces[0]).descriptor ==
          "Ltest/Child;");
    REQUIRE(linked.interfaces.size() == 2);
    CHECK(linker.Class(linked.interfaces[0]).descriptor == "Ltest/Child;");
    CHECK(linker.Class(linked.interfaces[1]).descriptor == "Ltest/Parent;");

    REQUIRE(linked.own_direct_methods.size() == 2);
    CHECK(linker.Method(linked.own_direct_methods[0]).name == "hidden");
    CHECK(linker.Method(linked.own_direct_methods[0]).access_flags == 0x0002U);
    CHECK(linker.Method(linked.own_direct_methods[0]).declared_invoke_kind ==
          DeclaredInvokeKind::direct);
    CHECK(linker.Method(linked.own_direct_methods[1]).name == "make");
    CHECK(linker.Method(linked.own_direct_methods[1]).access_flags == 0x000cU);
    CHECK(linker.Method(linked.own_direct_methods[1]).declared_invoke_kind ==
          DeclaredInvokeKind::static_call);

    REQUIRE(linked.own_virtual_methods.size() == 2);
    CHECK(linker.Method(linked.own_virtual_methods[0]).name == "run");
    CHECK(linker.Method(linked.own_virtual_methods[0]).declared_invoke_kind ==
          DeclaredInvokeKind::virtual_call);
    CHECK(linker.Method(linked.own_virtual_methods[1]).access_flags == 0x0014U);

    REQUIRE(linked.own_instance_fields.size() == 1);
    CHECK(linker.Field(linked.own_instance_fields[0]).access_flags == 0x0002U);
    REQUIRE(linked.own_static_fields.size() == 1);
    CHECK(linker.Field(linked.own_static_fields[0]).access_flags == 0x004cU);

    const auto child_id = linker.FindClass("Ltest/Child;");
    REQUIRE(child_id.has_value());
    const auto& child_method =
        linker.Method(linker.Class(*child_id).own_virtual_methods[0]);
    CHECK(child_method.declared_invoke_kind ==
          DeclaredInvokeKind::interface_call);
}

TEST_CASE("DVM-94 intrinsic declarations inherit and replace stable vtable slots") {
    auto parent = IntrinsicClassBuilder::Class("Ltest/Dvm94Parent;");
    parent.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
    parent.VirtualMethod("value", "(JLjava/lang/Object;)I",
                         [](IntrinsicContext&) { return VmValue::Int(1); });
    parent.FinalMethod("sealed", "()I",
                       [](IntrinsicContext&) { return VmValue::Int(2); });
    parent.StaticMethod("factory", "()I",
                        [](IntrinsicContext&) { return VmValue::Int(3); });
    parent.InstanceField("payload", "Ljava/lang/Object;");

    auto child = IntrinsicClassBuilder::Class("Ltest/Dvm94Child;",
                                               "Ltest/Dvm94Parent;");
    child.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });

    auto grandchild = IntrinsicClassBuilder::Class(
        "Ltest/Dvm94Grandchild;", "Ltest/Dvm94Child;");
    grandchild.OverrideMethod(
        "value", "(JLjava/lang/Object;)I",
        [](IntrinsicContext&) { return VmValue::Int(4); });

    auto catalog = CoreIntrinsicCatalog();
    catalog.push_back(std::move(parent).Build());
    catalog.push_back(std::move(child).Build());
    catalog.push_back(std::move(grandchild).Build());
    DexClassLinker linker;
    linker.RegisterIntrinsics(catalog);
    linker.Link();

    const auto parent_id = linker.ResolveDescriptor("Ltest/Dvm94Parent;");
    const auto child_id = linker.ResolveDescriptor("Ltest/Dvm94Child;");
    const auto grandchild_id =
        linker.ResolveDescriptor("Ltest/Dvm94Grandchild;");
    const auto slot = linker.FindVtableIndex(parent_id, "value",
                                              "(JLjava/lang/Object;)I");
    REQUIRE(slot.has_value());
    const auto inherited = linker.Class(parent_id).vtable[*slot];
    CHECK(linker.Class(child_id).vtable[*slot] == inherited);
    CHECK(linker.Method(linker.Class(child_id).vtable[*slot]).owner == parent_id);
    CHECK(linker.Class(grandchild_id).vtable[*slot] != inherited);
    CHECK(linker.Method(linker.Class(grandchild_id).vtable[*slot]).owner ==
          grandchild_id);
    CHECK(linker.Class(child_id).own_virtual_methods.empty());

    const auto& shape = linker.Method(inherited).shape;
    CHECK(shape.return_kind == ValueKind::cat1);
    REQUIRE(shape.parameter_kinds.size() == 2);
    CHECK(shape.parameter_kinds[0] == ValueKind::wide);
    CHECK(shape.parameter_kinds[1] == ValueKind::ref);
    CHECK(shape.parameter_word_offsets == std::vector<std::uint16_t>{1, 3});
    CHECK(shape.incoming_words == 4);

    const auto before = &linker.Method(inherited);
    static_cast<void>(linker.ResolveDescriptor("[Ltest/Dvm94Child;"));
    CHECK(&linker.Method(inherited) == before);
}

TEST_CASE("DVM-94 intrinsic override intent is checked during linking") {
    const auto link_with_child = [](const bool explicit_override,
                                    const std::string& method) {
        auto parent = IntrinsicClassBuilder::Class("Ltest/Dvm94GateParent;");
        parent.VirtualMethod("open", "()V",
                             [](IntrinsicContext&) { return VmValue::Void(); });
        parent.FinalMethod("sealed", "()V",
                           [](IntrinsicContext&) { return VmValue::Void(); });
        auto child = IntrinsicClassBuilder::Class(
            "Ltest/Dvm94GateChild;", "Ltest/Dvm94GateParent;");
        const auto handler = [](IntrinsicContext&) { return VmValue::Void(); };
        if (explicit_override) {
            child.OverrideMethod(method, "()V", handler);
        } else {
            child.VirtualMethod(method, "()V", handler);
        }
        auto catalog = CoreIntrinsicCatalog();
        catalog.push_back(std::move(parent).Build());
        catalog.push_back(std::move(child).Build());
        DexClassLinker linker;
        linker.RegisterIntrinsics(catalog);
        linker.Link();
    };

    CHECK_THROWS_AS(link_with_child(false, "open"), DexVmError);
    CHECK_THROWS_AS(link_with_child(true, "missing"), DexVmError);
    CHECK_THROWS_AS(link_with_child(true, "sealed"), DexVmError);

    CHECK_THROWS_AS(
        [] {
            auto parent = IntrinsicClassBuilder::Class(
                "Ltest/Dvm94VisibilityParent;");
            parent.VirtualMethod(
                "open", "()V",
                [](IntrinsicContext&) { return VmValue::Void(); }, 0x0004U);
            auto child = IntrinsicClassBuilder::Class(
                "Ltest/Dvm94VisibilityChild;",
                "Ltest/Dvm94VisibilityParent;");
            child.OverrideMethod(
                "open", "()V",
                [](IntrinsicContext&) { return VmValue::Void(); }, 0U);
            auto catalog = CoreIntrinsicCatalog();
            catalog.push_back(std::move(parent).Build());
            catalog.push_back(std::move(child).Build());
            DexClassLinker linker;
            linker.RegisterIntrinsics(catalog);
            linker.Link();
        }(),
        DexVmError);

    const auto link_protected_parent = [](const std::uint32_t child_access) {
        auto parent = IntrinsicClassBuilder::Class(
            "Ltest/Dvm94ProtectedParent;");
        parent.VirtualMethod(
            "callback", "()V",
            [](IntrinsicContext&) { return VmValue::Void(); }, 0x0004U);
        auto child = IntrinsicClassBuilder::Class(
            "Ltest/Dvm94ProtectedChild;",
            "Ltest/Dvm94ProtectedParent;");
        child.OverrideMethod(
            "callback", "()V",
            [](IntrinsicContext&) { return VmValue::Void(); }, child_access);
        auto catalog = CoreIntrinsicCatalog();
        catalog.push_back(std::move(parent).Build());
        catalog.push_back(std::move(child).Build());
        DexClassLinker linker;
        linker.RegisterIntrinsics(catalog);
        linker.Link();
    };
    CHECK_NOTHROW(link_protected_parent(0x0004U));
    CHECK_NOTHROW(link_protected_parent(0x0001U));
    CHECK_THROWS_AS(link_protected_parent(0U), DexVmError);
    CHECK_THROWS_AS(link_protected_parent(0x0002U), DexVmError);
}

TEST_CASE("reflection linker metadata preserves DEX order and loader roles") {
    DexClassLinker linker;
    const auto core = CoreIntrinsicCatalog();
    linker.RegisterIntrinsics(core);
    linker.RegisterDex(ReadFixture("interp.dex"));
    linker.Link();

    const auto counter_id = linker.FindClass("LCounter;");
    REQUIRE(counter_id.has_value());
    linker.EnsureClassLinked(*counter_id);
    const auto& counter = linker.Class(*counter_id);
    CHECK(counter.defining_loader == kApplicationLoader);
    REQUIRE(counter.own_direct_methods.size() == 1);
    CHECK(linker.Method(counter.own_direct_methods[0]).name == "<init>");
    CHECK(linker.Method(counter.own_direct_methods[0]).declared_invoke_kind ==
          DeclaredInvokeKind::direct);
    REQUIRE(counter.own_virtual_methods.size() == 3);
    CHECK(linker.Method(counter.own_virtual_methods[0]).name == "describe");
    CHECK(linker.Method(counter.own_virtual_methods[1]).name == "get");
    CHECK(linker.Method(counter.own_virtual_methods[2]).name == "increment");
    REQUIRE(counter.own_static_fields.size() == 1);
    CHECK(linker.Field(counter.own_static_fields[0]).name == "seed");
    REQUIRE(counter.own_instance_fields.size() == 1);
    CHECK(linker.Field(counter.own_instance_fields[0]).name == "value");

    CHECK(linker.Class(linker.ResolveDescriptor("[LCounter;"))
              .defining_loader == kApplicationLoader);
    CHECK(linker.Class(linker.ResolveDescriptor("[[LCounter;"))
              .defining_loader == kApplicationLoader);
    CHECK(linker.Class(linker.ResolveDescriptor("[Ljava/lang/String;"))
              .defining_loader == kBootstrapLoader);
    CHECK(linker.Class(linker.ResolveDescriptor("V")).defining_loader ==
          kBootstrapLoader);
}
