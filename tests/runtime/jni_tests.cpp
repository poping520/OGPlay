#include <cstddef>
#include <cstdint>
#include <set>
#include <string_view>
#include <type_traits>

#include <doctest/doctest.h>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/jni/jni.h"

TEST_CASE("legacy Android JNI ABI uses fixed guest widths and 233 slots") {
    static_assert(sizeof(ogplay::runtime::JniBoolean) == 1);
    static_assert(sizeof(ogplay::runtime::JniByte) == 1);
    static_assert(sizeof(ogplay::runtime::JniChar) == 2);
    static_assert(sizeof(ogplay::runtime::JniShort) == 2);
    static_assert(sizeof(ogplay::runtime::JniInt) == 4);
    static_assert(sizeof(ogplay::runtime::JniLong) == 8);
    static_assert(sizeof(ogplay::runtime::JniFloat) == 4);
    static_assert(sizeof(ogplay::runtime::JniDouble) == 8);
    static_assert(sizeof(ogplay::runtime::JniReference) == 4);
    static_assert(sizeof(ogplay::runtime::JniMethodId) == 4);
    static_assert(sizeof(ogplay::runtime::JniFieldId) == 4);
    static_assert(!std::is_same_v<ogplay::runtime::JniReference,
                                  ogplay::runtime::JniMethodId>);
    static_assert(!std::is_same_v<ogplay::runtime::JniMethodId,
                                  ogplay::runtime::JniFieldId>);

    CHECK(ogplay::runtime::JniReference{}.IsNull());
    CHECK(ogplay::runtime::JniReference{42}.Value() == 42);
    CHECK(static_cast<ogplay::runtime::JniInt>(
              ogplay::runtime::JniStatus::invalid_arguments) == -6);
    CHECK(ogplay::runtime::kJniVersion1_6 == 0x00010006);

    const auto slots = ogplay::runtime::JniNativeInterfaceSlots();
    REQUIRE(slots.size() == ogplay::runtime::kJniNativeInterfaceSlotCount);
    CHECK(slots[0] == "reserved0");
    CHECK(slots[3] == "reserved3");
    CHECK(slots[4] == "GetVersion");
    CHECK(slots[33] == "GetMethodID");
    CHECK(slots[215] == "RegisterNatives");
    CHECK(slots[228] == "ExceptionCheck");
    CHECK(slots[232] == "GetObjectRefType");
    const std::set<std::string_view> unique(slots.begin(), slots.end());
    CHECK(unique.size() == slots.size());

    REQUIRE(ogplay::runtime::FindJniSlot("CallIntMethodA").has_value());
    CHECK(ogplay::runtime::FindJniSlot("CallIntMethodA")->Value() == 51);
    CHECK_FALSE(ogplay::runtime::FindJniSlot("MissingSlot").has_value());
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::JniSlotName(
            ogplay::runtime::JniSlot{233})),
        std::invalid_argument);
}

TEST_CASE("JNI function table seals bindings and traps every missing slot") {
    ogplay::core::CapabilityLedger ledger;
    ogplay::runtime::JniFunctionTable table(ledger);
    const auto get_version =
        ogplay::runtime::FindJniSlot("GetVersion").value();
    const auto find_class = ogplay::runtime::FindJniSlot("FindClass").value();
    const ogplay::memory::GuestAddress thunk{0x70001000U};

    CHECK_THROWS_AS(static_cast<void>(table.Resolve(get_version, 0x1000U)),
                    std::logic_error);
    CHECK_THROWS_AS(table.Bind(ogplay::runtime::JniSlot{0}, thunk),
                    std::invalid_argument);
    CHECK_THROWS_AS(table.Bind(get_version, ogplay::memory::GuestAddress{}),
                    std::invalid_argument);
    table.Bind(get_version, thunk);
    CHECK(table.IsBound(get_version));
    CHECK_FALSE(table.IsBound(find_class));
    CHECK_THROWS_AS(table.Bind(get_version, thunk), std::logic_error);

    table.Seal();
    CHECK(table.IsSealed());
    CHECK(table.Resolve(get_version, 0x1000U) == thunk);
    CHECK_THROWS_AS(table.Bind(find_class, thunk), std::logic_error);
    CHECK_THROWS_AS(table.Seal(), std::logic_error);
    CHECK_THROWS_AS(static_cast<void>(table.Resolve(
                        ogplay::runtime::JniSlot{1}, 0x1000U)),
                    std::invalid_argument);

    try {
        static_cast<void>(table.Resolve(find_class, 0x1234U));
        FAIL("unbound JNI function did not trap");
    } catch (const ogplay::runtime::JniUnimplementedCall& call) {
        CHECK(call.Slot() == find_class);
        CHECK(call.LinkRegister() == 0x1234U);
    }
    CHECK_THROWS_AS(static_cast<void>(table.Resolve(find_class, 0x5678U)),
                    ogplay::runtime::JniUnimplementedCall);

    const auto hits = ledger.Unimplemented();
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].id == "runtime.jni.slot.FindClass");
    CHECK(hits[0].count == 2);
    CHECK(hits[0].first_lr == 0x1234U);
    CHECK(hits[0].last_lr == 0x5678U);
}

TEST_CASE("JNI local frames auto-grow up to the per-thread hard limit") {
    ogplay::runtime::JniReferenceTable references({4, 2, 2});
    references.AttachThread(11, 2);
    references.AttachThread(22, 1);
    CHECK(references.IsThreadAttached(11));
    CHECK_THROWS_AS(references.AttachThread(11),
                    ogplay::runtime::JniReferenceError);
    CHECK_THROWS_AS(references.AttachThread(0),
                    ogplay::runtime::JniReferenceError);

    const ogplay::runtime::JniObjectIdentity host{
        ogplay::runtime::JniObjectDomain::host, 101};
    const ogplay::runtime::JniObjectIdentity vm{
        ogplay::runtime::JniObjectDomain::dex_vm, 202};
    const auto first = references.NewLocal(11, host);
    const auto second = references.NewLocal(11, vm);
    CHECK(references.LocalCount(11) == 2);
    const auto third = references.NewLocal(11, host);
    CHECK(references.LocalCount(11) == 3);
    CHECK_THROWS_AS(references.EnsureLocalCapacity(11, 2),
                    ogplay::runtime::JniReferenceError);
    references.EnsureLocalCapacity(11, 1);
    const auto fourth = references.NewLocal(11, vm);
    CHECK_THROWS_AS(static_cast<void>(references.NewLocal(11, host)),
                    ogplay::runtime::JniReferenceError);
    CHECK(references.Resolve(11, first) == host);
    CHECK(references.IsSameObject(11, first, third));
    CHECK_FALSE(references.IsSameObject(11, first, second));
    CHECK_THROWS_AS(static_cast<void>(references.Resolve(22, first)),
                    ogplay::runtime::JniReferenceError);

    references.DeleteLocal(11, third);
    references.DeleteLocal(11, fourth);
    references.PushLocalFrame(11, 1);
    const auto nested = references.NewLocal(11, vm);
    const auto promoted = references.PopLocalFrame(11, nested);
    CHECK(promoted != nested);
    CHECK(references.Resolve(11, promoted) == vm);
    CHECK_THROWS_AS(static_cast<void>(references.Resolve(11, nested)),
                    ogplay::runtime::JniReferenceError);
    CHECK_THROWS_AS(static_cast<void>(references.PopLocalFrame(11)),
                    ogplay::runtime::JniReferenceError);

    references.DetachThread(11);
    CHECK_FALSE(references.IsThreadAttached(11));
    CHECK_THROWS_AS(static_cast<void>(references.Resolve(11, first)),
                    ogplay::runtime::JniReferenceError);
    references.DetachThread(22);
}

TEST_CASE("JNI global and weak references share identity without hiding errors") {
    ogplay::runtime::JniReferenceTable references({4, 1, 1});
    references.AttachThread(7, 4);
    const ogplay::runtime::JniObjectIdentity object{
        ogplay::runtime::JniObjectDomain::host, 55};
    const auto global = references.NewGlobal(object);
    const auto weak = references.NewWeakGlobal(object);
    CHECK(references.GlobalCount() == 1);
    CHECK(references.WeakGlobalCount() == 1);
    CHECK(references.Resolve(7, global) == object);
    CHECK(references.IsSameObject(7, global, weak));
    CHECK_THROWS_AS(static_cast<void>(references.NewGlobal(object)),
                    ogplay::runtime::JniReferenceError);
    CHECK_THROWS_AS(references.DeleteWeakGlobal(global),
                    ogplay::runtime::JniReferenceError);

    references.ClearWeakReferencesTo(object);
    CHECK_FALSE(references.Resolve(7, weak).has_value());
    CHECK(references.IsSameObject(7, weak, ogplay::runtime::JniReference{}));
    references.DeleteWeakGlobal(weak);
    references.DeleteGlobal(global);
    references.DeleteGlobal(ogplay::runtime::JniReference{});
    references.DeleteWeakGlobal(ogplay::runtime::JniReference{});
    CHECK(references.GlobalCount() == 0);
    CHECK(references.WeakGlobalCount() == 0);
    CHECK_THROWS_AS(static_cast<void>(references.NewLocal(
                        7, {ogplay::runtime::JniObjectDomain::host, 0})),
                    ogplay::runtime::JniReferenceError);
}
