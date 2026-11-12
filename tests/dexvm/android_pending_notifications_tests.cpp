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
#include "../fixtures/bitmap_decode_data.h"

#include "android_value_fixture.h"

using namespace ogplay::test::android_value;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("DVM-224 notification posting and cancellation use the session inventory") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto foreign =
            IntrinsicClassBuilder::Class("Ltest/ForeignNotificationContext;", "Landroid/content/ContextWrapper;");
        foreign.OverrideMethod("getPackageName", "()Ljava/lang/String;", [](IntrinsicContext& call) {
            return VmValue::Ref(call.vm.NewStringUtf8("other.application"));
        });
        AndroidValueVm f(backend, {std::move(foreign).Build()});
        f.context->package_name = "fixture";
        f.vm.SetGcIntegration(
            {{}, {}, [&f](const VmRootVisitor& visit) { VisitAndroidSessionRoots(*f.context, visit); }});
        const auto base = f.New("Landroid/content/Context;");
        const auto root = f.vm.ProtectReferences(std::array{base});
        const auto service = [&] {
            return f
                .On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                    {VmValue::Ref(f.vm.NewStringUtf8("notification"))})
                .ref;
        };
        const auto manager = service();
        REQUIRE(manager.IsValid());
        CHECK(f.linker.Class(f.model.ObjectClass(manager)).is_boot_dex);
        CHECK(f.Static("Landroid/app/NotificationManager;", "from",
                       "(Landroid/content/Context;)Landroid/app/NotificationManager;", {VmValue::Ref(base)})
                  .ref == manager);
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
        const auto operation =
            f.Static("Landroid/app/PendingIntent;", "getService",
                     "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;",
                     {VmValue::Ref(base), VmValue::Int(4), VmValue::Ref(intent), VmValue::Int(0)})
                .ref;
        const auto operation_root = f.vm.ProtectReferences(std::array{operation});
        f.On(manager, "cancelAll", "()V");
        const auto sender = f.On(operation, "getTarget", "()Landroid/content/IIntentSender;").ref;
        REQUIRE(f.context->pending_intents.contains(sender.Value()));
        CHECK_FALSE(f.context->pending_intents.at(sender.Value()).canceled);
        static_cast<void>(f.vm.CollectGarbage("notification-empty-inventory"));
        CHECK(service() == manager);
        f.On(manager, "cancel", "(I)V", {VmValue::Int(123)});
        const auto null_post = f.OnOutcome(manager, "notify", "(ILandroid/app/Notification;)V",
                                           {VmValue::Int(1), VmValue::Ref(VmObjectRef{})});
        REQUIRE(null_post.exception.IsValid());
        CHECK(f.linker.Class(null_post.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto notification = f.New("Landroid/app/Notification;");
        const auto post = f.OnOutcome(manager, "notify", "(ILandroid/app/Notification;)V",
                                      {VmValue::Int(1), VmValue::Ref(notification)});
        REQUIRE_MESSAGE(!post.exception.IsValid(), post.exception_message);
        REQUIRE(f.context->device_state.notifications.size() == 1);
        const auto stored = f.context->device_state.notifications.begin()->second;
        static_cast<void>(f.vm.CollectGarbage("notification-inventory"));
        CHECK(f.model.IsValidRef(stored));
        f.On(manager, "cancel", "(I)V", {VmValue::Int(1)});
        CHECK(f.context->device_state.notifications.empty());
        CHECK(f.ledger.Unimplemented().empty());
        const auto other = f.vm.NewIntrinsicInstance("Ltest/ForeignNotificationContext;");
        f.On(other, "attachBaseContext", "(Landroid/content/Context;)V", {VmValue::Ref(base)});
        CHECK(f.On(other, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                   {VmValue::Ref(f.vm.NewStringUtf8("notification"))})
                  .ref == manager);
        const auto service_binder =
            f.Static("Landroid/app/NotificationManager;", "getService", "()Landroid/app/INotificationManager;").ref;
        const auto foreign_cancel =
            f.OnOutcome(service_binder, "cancelAllNotifications", "(Ljava/lang/String;I)V",
                        {VmValue::Ref(f.vm.NewStringUtf8("other.application")), VmValue::Int(0)});
        REQUIRE(foreign_cancel.exception.IsValid());
        CHECK(f.linker.Class(foreign_cancel.exception_class).descriptor == "Ljava/lang/SecurityException;");
    }
}

TEST_CASE("DVM-211 PendingIntent identity flags cancellation and GC") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "example";
        f.vm.SetGcIntegration(
            {{}, {}, [&f](const VmRootVisitor& visit) { VisitAndroidSessionRoots(*f.context, visit); }});
        const auto base = f.New("Landroid/content/Context;");
        const auto action = f.vm.NewStringUtf8("Submit");
        const auto intent = f.New("Landroid/content/Intent;", "(Ljava/lang/String;)V", {VmValue::Ref(action)});
        const auto input_roots = f.vm.ProtectReferences(std::array{base, intent});
        constexpr auto descriptor = "Landroid/app/PendingIntent;";
        constexpr auto signature = "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;";
        constexpr int no_create = 1 << 29, cancel_current = 1 << 28, update_current = 1 << 27, one_shot = 1 << 30;
        const auto token = [&](int flags = 0, int request = 0, const char* kind = "getService") {
            return f
                .Static(descriptor, kind, signature,
                        {VmValue::Ref(base), VmValue::Int(request), VmValue::Ref(intent), VmValue::Int(flags)})
                .ref;
        };
        const auto target = [&](VmObjectRef wrapper) {
            return wrapper.IsValid() ? f.On(wrapper, "getTarget", "()Landroid/content/IIntentSender;").ref
                                     : VmObjectRef{};
        };
        const auto same = [&](VmObjectRef left, VmObjectRef right) { return target(left) == target(right); };
        CHECK_FALSE(token(no_create).IsValid());
        const auto first = token();
        const auto first_root = f.vm.ProtectReferences(std::array{first});
        REQUIRE(first.IsValid());
        CHECK(same(token(), first));
        CHECK(same(token(no_create), first));
        CHECK(!same(token(0, 1), first));
        CHECK(!same(token(0, 0, "getBroadcast"), first));
        const auto activity = token(0, 0, "getActivity");
        REQUIRE(activity.IsValid());
        CHECK(!same(activity, first));
        CHECK(same(token(no_create, 0, "getActivity"), activity));
        CHECK(!same(token(one_shot), first));
        CHECK(!same(token(1), first));  // fill-in flags form part of API19 identity
        const auto snapshot = f.context->pending_intents.at(target(first).Value()).intent;
        CHECK(snapshot != intent);
        CHECK(f.On(snapshot, "filterEquals", "(Landroid/content/Intent;)Z", {VmValue::Ref(intent)}).AsInt() == 1);
        const auto extra = f.vm.NewStringUtf8("extra");
        const auto extra_root = f.vm.ProtectReferences(std::array{extra});
        const auto put = [&](int value) {
            f.On(intent, "putExtra", "(Ljava/lang/String;I)Landroid/content/Intent;",
                 {VmValue::Ref(extra), VmValue::Int(value)});
        };
        const auto read = [&] {
            return f.On(snapshot, "getIntExtra", "(Ljava/lang/String;I)I", {VmValue::Ref(extra), VmValue::Int(-1)})
                .AsInt();
        };
        put(7);
        CHECK(same(token(), first));  // extras do not define token identity
        CHECK(read() == -1);          // snapshot cannot observe caller mutation
        CHECK(same(token(update_current), first));
        CHECK(read() == 7);
        put(9);
        CHECK(read() == 7);
        f.On(intent, "setFlags", "(I)Landroid/content/Intent;", {VmValue::Int(0x10000000)});
        CHECK(same(token(update_current), first));
        CHECK(f.On(snapshot, "getFlags", "()I").AsInt() == 0);  // update replaces only extras
        CHECK(read() == 9);
        const auto change_action = [&](const char* value) {
            f.On(intent, "setAction", "(Ljava/lang/String;)Landroid/content/Intent;",
                 {VmValue::Ref(f.vm.NewStringUtf8(value))});
        };
        change_action("Sync");
        CHECK(!same(token(), first));
        change_action("Submit");
        CHECK(same(token(no_create), first));
        const auto category = f.vm.NewStringUtf8("category");
        const auto category_root = f.vm.ProtectReferences(std::array{category});
        f.On(intent, "addCategory", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(category)});
        CHECK(!same(token(), first));
        f.On(intent, "removeCategory", "(Ljava/lang/String;)V", {VmValue::Ref(category)});
        CHECK(same(token(), first));
        const auto uri = f.Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                                  {VmValue::Ref(f.vm.NewStringUtf8("file:///value"))})
                             .ref;
        f.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(uri)});
        CHECK(!same(token(), first));
        f.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        CHECK(same(token(), first));
        const auto component =
            f.New("Landroid/content/ComponentName;", "(Ljava/lang/String;Ljava/lang/String;)V",
                  {VmValue::Ref(f.vm.NewStringUtf8("example")), VmValue::Ref(f.vm.NewStringUtf8("example.Worker"))});
        f.On(intent, "setComponent", "(Landroid/content/ComponentName;)Landroid/content/Intent;",
             {VmValue::Ref(component)});
        CHECK(!same(token(), first));
        f.On(intent, "setComponent", "(Landroid/content/ComponentName;)Landroid/content/Intent;",
             {VmValue::Ref(VmObjectRef{})});
        CHECK(same(token(), first));
        f.On(intent, "setType", "(Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(f.vm.NewStringUtf8("text/plain"))});
        CHECK(!same(token(), first));
        f.On(intent, "setType", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        f.On(intent, "setPackage", "(Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(f.vm.NewStringUtf8("example"))});
        CHECK(!same(token(), first));
        f.On(intent, "setPackage", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        CHECK(same(token(), first));
        const auto alarm_name = f.vm.NewStringUtf8("alarm");
        const auto alarm =
            f.On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", {VmValue::Ref(alarm_name)}).ref;
        CHECK(
            f.On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", {VmValue::Ref(alarm_name)}).ref ==
            alarm);
        const auto cancel_alarm = [&](VmObjectRef operation) {
            f.On(alarm, "cancel", "(Landroid/app/PendingIntent;)V", {VmValue::Ref(operation)});
        };
        const auto other = token(0, 2);
        // Seed backend entries to verify removal independently of unsupported scheduling.
        const auto other_root = f.vm.ProtectReferences(std::array{other});
        const auto other_sender = target(other);
        f.context->alarm_operations = {target(first), other_sender};
        cancel_alarm(first);
        REQUIRE(f.context->alarm_operations.size() == 1);
        CHECK(f.context->alarm_operations.front() == other_sender);
        cancel_alarm(first);                   // canceling an absent alarm is valid
        cancel_alarm(VmObjectRef{});           // API19 null cancellation is also valid
        CHECK(same(token(no_create), first));  // alarm cancel never invalidates sender
        static_cast<void>(f.vm.CollectGarbage("pending-intent-owners"));
        f.vm.RunFinalization();
        static_cast<void>(f.vm.CollectGarbage("pending-intent-senders"));
        CHECK(f.vm.MarkReachable().IsMarked(snapshot));
        CHECK(f.vm.MarkReachable().IsMarked(other_sender));  // alarm keeps the sender alive
        CHECK(f.context->pending_intents.size() == 2);       // other weak entries swept
        f.On(first, "cancel", "()V");
        f.On(first, "cancel", "()V");
        CHECK_FALSE(token(no_create).IsValid());
        const auto replacement = token();
        const auto replacement_root = f.vm.ProtectReferences(std::array{replacement});
        CHECK(replacement != first);
        CHECK(same(token(cancel_current | no_create), replacement));
        CHECK(f.context->pending_intents.at(target(replacement).Value()).canceled);
        CHECK_FALSE(token(no_create).IsValid());
        const auto next = token();
        const auto next_root = f.vm.ProtectReferences(std::array{next});
        CHECK(!same(token(cancel_current), next));
        CHECK(f.context->pending_intents.at(target(next).Value()).canceled);
        const auto fail =
            f.StaticOutcome(descriptor, "getService", signature,
                            {VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0x100)});
        REQUIRE(fail.exception.IsValid());
        CHECK(f.linker.Class(fail.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        const auto null_context =
            f.StaticOutcome(descriptor, "getService", signature,
                            {VmValue::Ref(VmObjectRef{}), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0)});
        REQUIRE(null_context.exception.IsValid());
        CHECK(f.linker.Class(null_context.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto null_intent =
            f.StaticOutcome(descriptor, "getService", signature,
                            {VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
        REQUIRE(null_intent.exception.IsValid());
        CHECK(f.linker.Class(null_intent.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto content_uri = f.Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                                          {VmValue::Ref(f.vm.NewStringUtf8("content://example/value"))})
                                     .ref;
        f.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(content_uri)});
        REQUIRE(f.StaticOutcome(descriptor, "getService", signature,
                                {VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0)})
                    .exception.IsValid());
        f.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        const auto selector = f.New("Landroid/content/Intent;");
        f.On(intent, "setSelector", "(Landroid/content/Intent;)V", {VmValue::Ref(selector)});
        REQUIRE(f.StaticOutcome(descriptor, "getService", signature,
                                {VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0)})
                    .exception.IsValid());
        f.On(intent, "setSelector", "(Landroid/content/Intent;)V", {VmValue::Ref(VmObjectRef{})});
        REQUIRE(f.OnOutcome(next, "send", "()V").exception.IsValid());
        REQUIRE(f.OnOutcome(alarm, "set", "(IJLandroid/app/PendingIntent;)V",
                            {VmValue::Int(3), VmValue::Long(1), VmValue::Ref(other)})
                    .exception.IsValid());
        std::vector<std::string> ids;
        for (const auto& hit : f.ledger.Unimplemented())
            ids.push_back(hit.id);
        CHECK_MESSAGE(std::ranges::find(ids, "dexvm.alarm_manager") != ids.end(), ids.size());
        CHECK_MESSAGE(std::ranges::find(ids, "dexvm.pending_intent") != ids.end(), ids.size());
        cancel_alarm(other);
        CHECK(f.context->alarm_operations.empty());
        const auto weak = target(token(0, 3));
        CHECK_FALSE(token(no_create, 4).IsValid());
        static_cast<void>(f.vm.CollectGarbage("pending-intent-weak-registry"));
        f.vm.RunFinalization();
        static_cast<void>(f.vm.CollectGarbage("pending-intent-weak-sender"));
        CHECK_FALSE(f.context->pending_intents.contains(weak.Value()));
        ShutdownPendingIntents(f.vm, *f.context);
        CHECK(f.context->pending_intents.empty());
        CHECK(f.context->alarm_operations.empty());
        REQUIRE(f.StaticOutcome(descriptor, "getService", signature,
                                {VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0)})
                    .exception.IsValid());
    }
}

TEST_CASE("Service foreground notification display degrades after validating the service token") {
    AndroidValueVm f;
    const auto manager =
        f.Static("Landroid/app/ActivityManagerNative;", "getDefault", "()Landroid/app/IActivityManager;").ref;
    const auto token = f.New("Landroid/os/Binder;");
    const auto component =
        f.New("Landroid/content/ComponentName;", "(Ljava/lang/String;Ljava/lang/String;)V",
              {VmValue::Ref(f.vm.NewStringUtf8("fixture")), VmValue::Ref(f.vm.NewStringUtf8("fixture.Worker"))});
    const auto notification = f.New("Landroid/app/Notification;");
    const auto roots = f.vm.ProtectReferences(std::array{manager, token, component, notification});
    f.context->rom_services.emplace("fixture.Worker",
                                    DexVmAndroidContext::RomServiceRecord{VmObjectRef{}, token, component});
    const auto call = [&](int id, VmObjectRef value) {
        return f.OnOutcome(
            manager, "setServiceForeground",
            "(Landroid/content/ComponentName;Landroid/os/IBinder;ILandroid/app/Notification;Z)V",
            {VmValue::Ref(component), VmValue::Ref(token), VmValue::Int(id), VmValue::Ref(value), VmValue::Int(1)});
    };
    const auto invalid = call(1, VmObjectRef{});
    REQUIRE(invalid.exception.IsValid());
    CHECK(f.linker.Class(invalid.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
    CHECK_FALSE(call(1, notification).exception.IsValid());
    CHECK_FALSE(call(0, VmObjectRef{}).exception.IsValid());
    CHECK(f.ledger.Unimplemented().empty());
}

TEST_CASE("DVM-211 concurrent contexts share one PendingIntent token") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        std::array<VmCallOutcome, 2> results;
        std::array<VmObjectRef, 2> workers;
        VmObjectRef base, intent;
        VmMethodId method;
        auto thread = IntrinsicClassBuilder::Class("Ltest/PendingWorker;", "Ljava/lang/Thread;");
        thread.Constructor("()V", [](IntrinsicContext& call) {
            const auto owner = call.vm.Linker().ResolveDescriptor("Ljava/lang/Thread;");
            const auto init = call.vm.Linker().FindDirectMethod(owner, "<init>", "()V");
            REQUIRE(init);
            const auto result = call.vm.Call(*init, std::array{VmValue::Ref(call.receiver)});
            if (result.exception.IsValid()) call.vm.SetPendingException(result.exception);
            return VmValue::Void();
        });
        thread.OverrideMethod("run", "()V", [&](IntrinsicContext& call) {
            const auto index = call.receiver == workers[0] ? 0U : 1U;
            for (int repeat = 0; repeat < 16; ++repeat)
                results[index] = call.vm.Call(
                    method, std::array{VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0)});
            return VmValue::Void();
        });
        AndroidValueVm f(backend, {std::move(thread).Build()});
        f.context->package_name = "example";
        base = f.New("Landroid/content/Context;");
        const auto base_root = f.vm.ProtectReferences(std::array{base});
        intent = f.New("Landroid/content/Intent;", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("work"))});
        const auto intent_root = f.vm.ProtectReferences(std::array{intent});
        const auto klass = f.linker.ResolveDescriptor("Landroid/app/PendingIntent;");
        const auto linked = f.linker.FindDirectMethod(
            klass, "getService", "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;");
        REQUIRE(linked);
        method = *linked;
        const auto primed = f.vm.Call(
            method, std::array{VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(1 << 29)});
        REQUIRE_MESSAGE(!primed.exception.IsValid(), primed.exception_message);
        REQUIRE_FALSE(primed.value.ref.IsValid());
        workers[0] = f.New("Ltest/PendingWorker;");
        const auto first_root = f.vm.ProtectReferences(std::array{workers[0]});
        workers[1] = f.New("Ltest/PendingWorker;");
        const auto second_root = f.vm.ProtectReferences(std::array{workers[1]});
        for (const auto worker : workers)
            f.On(worker, "start", "()V");
        for (const auto worker : workers)
            f.On(worker, "join", "()V");
        for (const auto& result : results)
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message,
                            f.vm.DescribeThrowableCauses(result.exception));
        REQUIRE(results[0].value.ref.IsValid());
        REQUIRE(results[1].value.ref.IsValid());
        const auto result_roots = f.vm.ProtectReferences(std::array{results[0].value.ref, results[1].value.ref});
        CHECK(f.On(results[0].value.ref, "equals", "(Ljava/lang/Object;)Z", {results[1].value}).AsInt() == 1);
        CHECK(f.context->pending_intents.size() == 1);
    }
}

TEST_CASE("DVM227 ROM notification values preserve constructors and local Parcel contents") {
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
                               static_cast<std::uint64_t>(f.model.InstanceSlots(notification)[timestamp_slot + 1].bits)
                                   << 32U;
        CHECK(timestamp >= static_cast<std::uint64_t>(before));
        CHECK(timestamp <= static_cast<std::uint64_t>(after));
        CHECK(f.model.InstanceSlots(notification)[field("priority", "I")].bits == 0);
        CHECK(f.model.InstanceSlots(notification)[field("audioStreamType", "I")].bits == UINT32_MAX);
        const auto extras =
            VmObjectRef{f.model.InstanceSlots(notification)[field("extras", "Landroid/os/Bundle;")].bits};
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
        const auto parcel = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto parcel_root = f.vm.ProtectReferences(std::array{parcel});
        f.On(legacy, "writeToParcel", "(Landroid/os/Parcel;I)V", {VmValue::Ref(parcel), VmValue::Int(0)});
        f.On(parcel, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto restored = f.New("Landroid/app/Notification;", "(Landroid/os/Parcel;)V", {VmValue::Ref(parcel)});
        const auto restored_root = f.vm.ProtectReferences(std::array{restored});
        CHECK(f.model.InstanceSlots(restored)[field("icon", "I")].bits == 17);
        CHECK(f.model.InstanceSlots(restored)[timestamp_slot].bits == 0x23456789U);
        CHECK(f.model.InstanceSlots(restored)[timestamp_slot + 1].bits == 1U);
        CHECK(f.vm.StringUtf8(VmObjectRef{
                  f.model.InstanceSlots(restored)[field("tickerText", "Ljava/lang/CharSequence;")].bits}) == "title");
        const auto creator_field = f.linker.FindFieldRecursive(type, "CREATOR", "Landroid/os/Parcelable$Creator;");
        REQUIRE(creator_field.has_value());
        const auto creator = VmObjectRef{f.linker.Class(type).static_storage[f.linker.Field(*creator_field).slot]};
        const auto array = f.On(creator, "newArray", "(I)[Ljava/lang/Object;", {VmValue::Int(3)}).ref;
        CHECK(f.model.ArrayLength(array) == 3);
        CHECK_FALSE(f.model.GetObjectElement(array, 0).IsValid());
        const auto manager = f.On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                                  {VmValue::Ref(f.vm.NewStringUtf8("notification"))})
                                 .ref;
        const auto post = f.OnOutcome(manager, "notify", "(ILandroid/app/Notification;)V",
                                      {VmValue::Int(1), VmValue::Ref(notification)});
        REQUIRE_MESSAGE(!post.exception.IsValid(), post.exception_message);
        CHECK(f.context->device_state.notifications.size() == 1);
        f.On(manager, "cancelAll", "()V");
        CHECK(f.context->device_state.notifications.empty());
    }
}
