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

TEST_CASE("DVM-224 notification cancellation uses an explicit empty application inventory") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto foreign = IntrinsicClassBuilder::Class("Ltest/ForeignNotificationContext;", "Landroid/content/Context;");
        foreign.OverrideMethod("getPackageName", "()Ljava/lang/String;",
            [](IntrinsicContext& call) { return VmValue::Ref(call.vm.NewStringUtf8("other.application")); });
        AndroidValueVm f(backend, {std::move(foreign).Build()});
        f.context->package_name = "fixture";
        f.vm.SetGcIntegration({{}, {}, [&f](const VmRootVisitor& visit) {
            VisitAndroidSessionRoots(*f.context, visit);
        }});
        const auto base = f.New("Landroid/content/Context;");
        const auto root = f.vm.ProtectReferences(std::array{base});
        const auto service = [&] {
            return f.On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                {VmValue::Ref(f.vm.NewStringUtf8("notification"))}).ref;
        };
        const auto manager = service();
        REQUIRE(manager.IsValid());
        CHECK(f.linker.Class(f.model.ObjectClass(manager)).is_boot_dex);
        CHECK(f.Static("Landroid/app/NotificationManager;", "from",
            "(Landroid/content/Context;)Landroid/app/NotificationManager;", {VmValue::Ref(base)}).ref == manager);
        for (const auto id : {-1, 0, 123, 2147483647}) {
            f.On(manager, "cancel", "(I)V", {VmValue::Int(id)});
            for (const auto tag : {VmObjectRef{}, f.vm.NewStringUtf8(""), f.vm.NewStringUtf8("download")}) {
                f.On(manager, "cancel", "(Ljava/lang/String;I)V", {VmValue::Ref(tag), VmValue::Int(id)});
                f.On(manager, "cancel", "(Ljava/lang/String;I)V", {VmValue::Ref(tag), VmValue::Int(id)});
            }
        }
        f.On(manager, "cancelAll", "()V");
        f.On(manager, "cancelAll", "()V");
        CHECK(f.ledger.Unimplemented().empty());
        // Cancellation never invalidates process-local PendingIntent tokens.
        const auto intent = f.New("Landroid/content/Intent;");
        const auto operation = f.Static("Landroid/app/PendingIntent;", "getService",
            "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;",
            {VmValue::Ref(base), VmValue::Int(4), VmValue::Ref(intent), VmValue::Int(0)}).ref;
        const auto operation_root = f.vm.ProtectReferences(std::array{operation});
        f.On(manager, "cancelAll", "()V");
        REQUIRE(f.context->pending_intents.contains(operation.Value()));
        CHECK_FALSE(f.context->pending_intents.at(operation.Value()).canceled);
        static_cast<void>(f.vm.CollectGarbage("notification-empty-inventory"));
        CHECK(service() == manager);
        f.On(manager, "cancel", "(I)V", {VmValue::Int(123)});
        const auto null_post = f.OnOutcome(manager, "notify", "(ILandroid/app/Notification;)V",
            {VmValue::Int(1), VmValue::Ref(VmObjectRef{})});
        REQUIRE(null_post.exception.IsValid());
        CHECK(f.linker.Class(null_post.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        // The private publication boundary must fail even before value/renderer support is added.
        const auto post = f.linker.FindDirectMethod(f.model.ObjectClass(manager), "nativeRejectPost", "(Ljava/lang/String;)V");
        REQUIRE(post.has_value());
        const auto rejected = f.vm.Call(*post, std::array{VmValue::Ref(manager), VmValue::Ref(f.vm.NewStringUtf8("fixture"))});
        REQUIRE(rejected.exception.IsValid());
        CHECK(f.linker.Class(rejected.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        const auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].id == "dexvm.notification_post");
        CHECK(hits[0].count == 1);
        const auto other = f.vm.NewIntrinsicInstance("Ltest/ForeignNotificationContext;");
        const auto denied = f.OnOutcome(other, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
            {VmValue::Ref(f.vm.NewStringUtf8("notification"))});
        REQUIRE(denied.exception.IsValid());
        CHECK(f.linker.Class(denied.exception_class).descriptor == "Ljava/lang/SecurityException;");
        const auto cancel = f.linker.FindDirectMethod(f.model.ObjectClass(manager), "nativeCancel", "(Ljava/lang/String;Ljava/lang/String;IZ)V");
        REQUIRE(cancel.has_value());
        const auto foreign_cancel = f.vm.Call(*cancel, std::array{VmValue::Ref(manager),
            VmValue::Ref(f.vm.NewStringUtf8("other.application")), VmValue::Ref(VmObjectRef{}), VmValue::Int(1), VmValue::Int(0)});
        REQUIRE(foreign_cancel.exception.IsValid());
        CHECK(f.linker.Class(foreign_cancel.exception_class).descriptor == "Ljava/lang/SecurityException;");
    }
}

TEST_CASE("DVM-211 PendingIntent identity flags cancellation and GC") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "example";
        f.vm.SetGcIntegration({{}, {}, [&f](const VmRootVisitor& visit) {
            VisitAndroidSessionRoots(*f.context, visit);
        }});
        const auto base = f.New("Landroid/content/Context;");
        const auto action = f.vm.NewStringUtf8("Submit");
        const auto intent = f.New("Landroid/content/Intent;", "(Ljava/lang/String;)V", {VmValue::Ref(action)});
        const auto input_roots = f.vm.ProtectReferences(std::array{base, intent});
        constexpr auto descriptor = "Landroid/app/PendingIntent;";
        constexpr auto signature = "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;";
        constexpr int no_create=1<<29, cancel_current=1<<28, update_current=1<<27, one_shot=1<<30;
        const auto token = [&](int flags=0, int request=0, const char* kind="getService") {
            return f.Static(descriptor, kind, signature,
                {VmValue::Ref(base), VmValue::Int(request), VmValue::Ref(intent), VmValue::Int(flags)}).ref;
        };
        CHECK_FALSE(token(no_create).IsValid());
        const auto first = token();
        const auto first_root = f.vm.ProtectReferences(std::array{first});
        REQUIRE(first.IsValid());
        CHECK(token() == first);
        CHECK(token(no_create) == first);
        CHECK(token(0, 1) != first);
        CHECK(token(0, 0, "getBroadcast") != first);
        CHECK(token(one_shot) != first);
        CHECK(token(1) != first); // fill-in flags form part of API19 identity
        const auto snapshot = f.context->pending_intents.at(first.Value()).intent;
        CHECK(snapshot != intent);
        CHECK(f.On(snapshot, "filterEquals", "(Landroid/content/Intent;)Z", {VmValue::Ref(intent)}).AsInt() == 1);
        const auto extra = f.vm.NewStringUtf8("extra");
        const auto extra_root = f.vm.ProtectReferences(std::array{extra});
        const auto put = [&](int value) { f.On(intent, "putExtra", "(Ljava/lang/String;I)Landroid/content/Intent;", {VmValue::Ref(extra), VmValue::Int(value)}); };
        const auto read = [&] { return f.On(snapshot, "getIntExtra", "(Ljava/lang/String;I)I", {VmValue::Ref(extra), VmValue::Int(-1)}).AsInt(); };
        put(7);
        CHECK(token() == first); // extras do not define token identity
        CHECK(read() == -1); // snapshot cannot observe caller mutation
        CHECK(token(update_current) == first);
        CHECK(read() == 7);
        put(9);
        CHECK(read() == 7);
        f.On(intent, "setFlags", "(I)Landroid/content/Intent;", {VmValue::Int(0x10000000)});
        CHECK(token(update_current) == first);
        CHECK(f.On(snapshot, "getFlags", "()I").AsInt() == 0); // update replaces only extras
        CHECK(read() == 9);
        const auto change_action = [&](const char* value) { f.On(intent, "setAction", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(f.vm.NewStringUtf8(value))}); };
        change_action("Sync");
        CHECK(token() != first);
        change_action("Submit");
        CHECK(token(no_create) == first);
        const auto category = f.vm.NewStringUtf8("category");
        const auto category_root = f.vm.ProtectReferences(std::array{category});
        f.On(intent, "addCategory", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(category)});
        CHECK(token() != first);
        f.On(intent, "removeCategory", "(Ljava/lang/String;)V", {VmValue::Ref(category)});
        CHECK(token() == first);
        const auto uri = f.Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;", {VmValue::Ref(f.vm.NewStringUtf8("file:///value"))}).ref;
        f.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(uri)});
        CHECK(token() != first);
        f.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        CHECK(token() == first);
        const auto component = f.New("Landroid/content/ComponentName;", "(Ljava/lang/String;Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("example")), VmValue::Ref(f.vm.NewStringUtf8("example.Worker"))});
        f.On(intent, "setComponent", "(Landroid/content/ComponentName;)Landroid/content/Intent;", {VmValue::Ref(component)});
        CHECK(token() != first);
        f.On(intent, "setComponent", "(Landroid/content/ComponentName;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        CHECK(token() == first);
        f.On(intent, "setType", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(f.vm.NewStringUtf8("text/plain"))});
        CHECK(token() != first);
        f.On(intent, "setType", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        f.On(intent, "setPackage", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(f.vm.NewStringUtf8("example"))});
        CHECK(token() != first);
        f.On(intent, "setPackage", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        CHECK(token() == first);
        const auto alarm_name = f.vm.NewStringUtf8("alarm");
        const auto alarm = f.On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", {VmValue::Ref(alarm_name)}).ref;
        CHECK(f.On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", {VmValue::Ref(alarm_name)}).ref == alarm);
        const auto cancel_alarm = [&](VmObjectRef operation) { f.On(alarm, "cancel", "(Landroid/app/PendingIntent;)V", {VmValue::Ref(operation)}); };
        const auto other = token(0, 2);
        // Seed backend entries to verify removal independently of unsupported scheduling.
        f.context->alarm_operations = {first, other};
        cancel_alarm(first);
        REQUIRE(f.context->alarm_operations.size() == 1);
        CHECK(f.context->alarm_operations.front() == other);
        cancel_alarm(first); // canceling an absent alarm is valid
        cancel_alarm(VmObjectRef{}); // API19 null cancellation is also valid
        CHECK(token(no_create) == first); // alarm cancel never invalidates sender
        static_cast<void>(f.vm.CollectGarbage("pending-intent-owners"));
        CHECK(f.vm.MarkReachable().IsMarked(snapshot));
        CHECK(f.vm.MarkReachable().IsMarked(other)); // alarm keeps its wrapper alive
        CHECK(f.context->pending_intents.size() == 2); // other weak entries swept
        f.On(first, "cancel", "()V");
        f.On(first, "cancel", "()V");
        CHECK_FALSE(token(no_create).IsValid());
        const auto replacement = token();
        const auto replacement_root = f.vm.ProtectReferences(std::array{replacement});
        CHECK(replacement != first);
        CHECK(token(cancel_current | no_create) == replacement);
        CHECK(f.context->pending_intents.at(replacement.Value()).canceled);
        CHECK_FALSE(token(no_create).IsValid());
        const auto next = token();
        const auto next_root = f.vm.ProtectReferences(std::array{next});
        CHECK(token(cancel_current) != next);
        CHECK(f.context->pending_intents.at(next.Value()).canceled);
        const auto fail = f.StaticOutcome(descriptor, "getService", signature,
            {VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0x100)});
        REQUIRE(fail.exception.IsValid());
        CHECK(f.linker.Class(fail.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        const auto null_context = f.StaticOutcome(descriptor, "getService", signature,
            {VmValue::Ref(VmObjectRef{}), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0)});
        REQUIRE(null_context.exception.IsValid());
        CHECK(f.linker.Class(null_context.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto null_intent = f.StaticOutcome(descriptor, "getService", signature,
            {VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
        REQUIRE(null_intent.exception.IsValid());
        CHECK(f.linker.Class(null_intent.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto content_uri = f.Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;", {VmValue::Ref(f.vm.NewStringUtf8("content://example/value"))}).ref;
        f.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(content_uri)});
        REQUIRE(f.StaticOutcome(descriptor, "getService", signature,
            {VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0)}).exception.IsValid());
        f.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        const auto selector = f.New("Landroid/content/Intent;");
        f.On(intent, "setSelector", "(Landroid/content/Intent;)V", {VmValue::Ref(selector)});
        REQUIRE(f.StaticOutcome(descriptor, "getService", signature,
            {VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0)}).exception.IsValid());
        f.On(intent, "setSelector", "(Landroid/content/Intent;)V", {VmValue::Ref(VmObjectRef{})});
        REQUIRE(f.OnOutcome(next, "send", "()V").exception.IsValid());
        REQUIRE(f.OnOutcome(alarm, "set", "(IJLandroid/app/PendingIntent;)V",
            {VmValue::Int(3), VmValue::Long(1), VmValue::Ref(other)}).exception.IsValid());
        CHECK(f.ledger.Unimplemented().size() == 2);
        cancel_alarm(other);
        static_cast<void>(f.vm.CollectGarbage("pending-intent-weak-registry"));
        CHECK_FALSE(f.context->pending_intents.contains(other.Value()));
        ShutdownPendingIntents(f.vm, *f.context);
        CHECK(f.context->pending_intents.empty());
        CHECK(f.context->alarm_operations.empty());
        REQUIRE(f.StaticOutcome(descriptor, "getService", signature,
            {VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0)}).exception.IsValid());
    }
}

TEST_CASE("DVM-211 concurrent contexts share one PendingIntent token") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "example";
        const auto base = f.New("Landroid/content/Context;");
        const auto intent = f.New("Landroid/content/Intent;", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("work"))});
        const auto inputs = f.vm.ProtectReferences(std::array{base, intent});
        const auto klass = f.linker.ResolveDescriptor("Landroid/app/PendingIntent;");
        const auto method = f.linker.FindDirectMethod(klass, "getService",
            "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;");
        REQUIRE(method.has_value());
        const auto first = f.vm.CreateExecutionContext(), second = f.vm.CreateExecutionContext();
        std::array<VmCallOutcome, 2> results;
        const auto run = [&](std::size_t index, const InterpreterExecutionContext& context) {
            for (int repeat=0; repeat<16; ++repeat)
                results[index] = f.vm.Call(context, *method,
                    std::array{VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0)});
        };
        std::thread a([&] { run(0, first); }), b([&] { run(1, second); });
        a.join(); b.join();
        REQUIRE_FALSE(results[0].exception.IsValid());
        REQUIRE_FALSE(results[1].exception.IsValid());
        CHECK(results[0].value.ref.IsValid());
        CHECK(results[0].value.ref == results[1].value.ref);
        CHECK(f.context->pending_intents.size() == 1);
        f.vm.DiscardExecutionContext(first); f.vm.DiscardExecutionContext(second);
    }
}

TEST_CASE("DVM227 notification values reject system templates with catchable recorded failure") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, "notification_template.dex");
        f.context->package_name = "fixture";
        const auto base = f.New("Landroid/content/Context;");
        const auto before = f.Static("Ljava/lang/System;", "currentTimeMillis", "()J").AsLong();
        const auto notification = f.New("Landroid/app/Notification;");
        const auto after = f.Static("Ljava/lang/System;", "currentTimeMillis", "()J").AsLong();
        const auto type = f.model.ObjectClass(notification);
        CHECK(f.linker.Class(type).is_boot_dex);
        CHECK(f.linker.IsAssignable(f.linker.ResolveDescriptor("Landroid/os/Parcelable;"), type));
        const auto field = [&](const char* name, const char* descriptor) {
            const auto found = f.linker.FindFieldRecursive(type, name, descriptor);
            REQUIRE(found.has_value());
            return f.linker.Field(*found).slot;
        };
        const auto timestamp_slot = field("when", "J");
        const auto timestamp = static_cast<std::uint64_t>(f.model.InstanceSlots(notification)[timestamp_slot].bits) |
            static_cast<std::uint64_t>(f.model.InstanceSlots(notification)[timestamp_slot + 1].bits) << 32U;
        CHECK(timestamp >= static_cast<std::uint64_t>(before));
        CHECK(timestamp <= static_cast<std::uint64_t>(after));
        CHECK(f.model.InstanceSlots(notification)[field("priority", "I")].bits == 0);
        CHECK(f.model.InstanceSlots(notification)[field("audioStreamType", "I")].bits == UINT32_MAX);
        const auto extras = VmObjectRef{f.model.InstanceSlots(notification)[field("extras", "Landroid/os/Bundle;")].bits};
        REQUIRE(extras.IsValid());
        const auto title = f.vm.NewStringUtf8("title");
        const auto legacy = f.New("Landroid/app/Notification;", "(ILjava/lang/CharSequence;J)V",
            {VmValue::Int(17), VmValue::Ref(title), VmValue::Long(0x123456789LL)});
        CHECK(f.model.InstanceSlots(legacy)[field("icon", "I")].bits == 17);
        CHECK(f.model.InstanceSlots(legacy)[field("tickerText", "Ljava/lang/CharSequence;")].bits == title.Value());
        CHECK(f.model.InstanceSlots(legacy)[timestamp_slot].bits == 0x23456789U);
        CHECK(f.model.InstanceSlots(legacy)[timestamp_slot + 1].bits == 1U);
        const auto roots = f.vm.ProtectReferences(std::array{base, notification, legacy});
        CHECK(f.vm.MarkReachable().IsMarked(extras));
        CHECK(f.vm.MarkReachable().IsMarked(title));
        static_cast<void>(f.vm.CollectGarbage());
        CHECK(f.model.IsValidRef(extras));
        CHECK(f.On(notification, "describeContents", "()I").AsInt() == 0);
        const auto signature = "(Landroid/content/Context;Ljava/lang/CharSequence;Ljava/lang/CharSequence;Landroid/app/PendingIntent;)V";
        const auto result = f.OnOutcome(legacy, "setLatestEventInfo", signature,
            {VmValue::Ref(base), VmValue::Ref(title), VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
        REQUIRE(result.exception.IsValid());
        CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(f.model.InstanceSlots(legacy)[field("icon", "I")].bits == 17);
        CHECK(f.model.InstanceSlots(legacy)[field("tickerText", "Ljava/lang/CharSequence;")].bits == title.Value());
        CHECK(f.model.InstanceSlots(legacy)[field("contentView", "Landroid/widget/RemoteViews;")].bits == 0);
        CHECK(f.model.InstanceSlots(legacy)[field("contentIntent", "Landroid/app/PendingIntent;")].bits == 0);
        CHECK(f.Static("Lfixture/NotificationTemplateProbe;", "fallback", "(Landroid/content/Context;)I", {VmValue::Ref(base)}).AsInt() == 1);
        auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].id == "dexvm.notification_template");
        CHECK(hits[0].count == 2);
        const auto null_context = f.OnOutcome(legacy, "setLatestEventInfo", signature,
            {VmValue::Ref(VmObjectRef{}), VmValue::Ref(title), VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
        REQUIRE(null_context.exception.IsValid());
        CHECK(f.linker.Class(null_context.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        CHECK(f.ledger.Unimplemented()[0].count == 2);
        const auto parcel = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto rejected_parcel = f.OnOutcome(notification, "writeToParcel", "(Landroid/os/Parcel;I)V", {VmValue::Ref(parcel), VmValue::Int(0)});
        REQUIRE(rejected_parcel.exception.IsValid());
        CHECK(f.linker.Class(rejected_parcel.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        hits = f.ledger.Unimplemented();
        CHECK(std::ranges::any_of(hits, [](const auto& hit) { return hit.id == "dexvm.notification_parcel" && hit.count == 1; }));
        const auto parcel_ctor = f.linker.FindDirectMethod(type, "<init>", "(Landroid/os/Parcel;)V");
        REQUIRE(parcel_ctor.has_value());
        const auto from_parcel = f.vm.Call(*parcel_ctor, std::array{
            VmValue::Ref(f.vm.NewIntrinsicInstance("Landroid/app/Notification;")), VmValue::Ref(parcel)});
        REQUIRE(from_parcel.exception.IsValid());
        CHECK(f.linker.Class(from_parcel.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        hits = f.ledger.Unimplemented();
        CHECK(std::ranges::any_of(hits, [](const auto& hit) { return hit.id == "dexvm.notification_parcel" && hit.count == 2; }));
        const auto creator_field = f.linker.FindFieldRecursive(type, "CREATOR", "Landroid/os/Parcelable$Creator;");
        REQUIRE(creator_field.has_value());
        const auto creator = VmObjectRef{f.linker.Class(type).static_storage[f.linker.Field(*creator_field).slot]};
        const auto array = f.On(creator, "newArray", "(I)[Ljava/lang/Object;", {VmValue::Int(3)}).ref;
        CHECK(f.model.ArrayLength(array) == 3);
        CHECK_FALSE(f.model.GetObjectElement(array, 0).IsValid());
        const auto manager = f.On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
            {VmValue::Ref(f.vm.NewStringUtf8("notification"))}).ref;
        const auto post = f.OnOutcome(manager, "notify", "(ILandroid/app/Notification;)V", {VmValue::Int(1), VmValue::Ref(notification)});
        REQUIRE(post.exception.IsValid());
        CHECK(f.linker.Class(post.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        f.On(manager, "cancelAll", "()V");
        hits = f.ledger.Unimplemented();
        CHECK(std::ranges::any_of(hits, [](const auto& hit) { return hit.id == "dexvm.notification_post" && hit.count == 1; }));
    }
}
