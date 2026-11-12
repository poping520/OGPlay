#include "ogplay/runtime/dexvm/platform_tables.h"
// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_content_BroadcastReceiver.cpp ----
// BroadcastReceiver is inert: the session never dispatches broadcasts, so
// the base onReceive stays a recorded no-op games may override.

#include "catalog.h"

#include <algorithm>
#include <ranges>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/hal/host_environment.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/runtime/database/database_runtime.h"
#include "sqlite3.h"

namespace ogplay::runtime::android_intrinsics {

namespace {
[[noreturn]] void UnsupportedLocalService(dx::Interpreter& vm, const char* reason) {
    if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.service_resolution", 0);
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", reason};
}

struct ExplicitServiceTarget {
    const loader::AndroidManifestServiceComponent* declaration = nullptr;
    bool hostable = true;
};

ExplicitServiceTarget ResolveExplicitService(dx::Interpreter& vm, const Context& context, dx::VmObjectRef intent) {
    if (!context->service_inventory_known || !intent.IsValid()) return {};
    const auto component = CallAndroidMethod(vm, intent, "getComponent", "()Landroid/content/ComponentName;").ref;
    if (!component.IsValid()) return {};
    const auto package = vm.StringUtf8(CallAndroidMethod(vm, component, "getPackageName", "()Ljava/lang/String;").ref);
    if (package != context->package_name) return {};
    const auto name = vm.StringUtf8(CallAndroidMethod(vm, component, "getClassName", "()Ljava/lang/String;").ref);
    const auto found = std::find_if(context->service_components.rbegin(), context->service_components.rend(),
                                    [&](const auto& service) { return service.name == name; });
    if (found == context->service_components.rend()) return {};
    const auto& process =
        context->application_process_name.empty() ? context->package_name : context->application_process_name;
    return {&*found, (found->flags & 0x0002U) == 0 && (found->process_name.empty() || found->process_name == process)};
}

void RecordSecondaryProcessService(dx::Interpreter& vm, const std::string_view api,
                                   const loader::AndroidManifestServiceComponent& service) {
    if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.service_resolution", 0);
    if (auto* logger = vm.Log()) {
        logger->Write(core::LogLevel::warn, "runtime.service.secondary_process",
                      "service cannot be hosted in the application process", {},
                      {{"api", std::string(api)}, {"component", service.name}, {"process", service.process_name}},
                      {.mode = core::RateLimitMode::first_and_count});
    }
}

void RequireAbsentService(dx::IntrinsicContext& call, const Context& context, dx::VmObjectRef intent) {
    const auto unsupported = [&call](const std::string& reason) -> void {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.service_resolution", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", reason};
    };
    if (!context->service_inventory_known) unsupported("service inventory is unavailable");
    // Only action-only queries are closed here. Read through the existing
    // Intent API; do not maintain a second copy of Intent state.
    for (const auto& [name, signature] :
         {std::pair{"getComponent", "()Landroid/content/ComponentName;"}, std::pair{"getData", "()Landroid/net/Uri;"},
          std::pair{"getType", "()Ljava/lang/String;"}, std::pair{"getCategories", "()Ljava/util/Set;"}}) {
        if (CallAndroidMethod(call.vm, intent, name, signature).ref.IsValid()) {
            unsupported("only an action-only Intent is supported");
        }
    }
    const auto action = CallAndroidMethod(call.vm, intent, "getAction", "()Ljava/lang/String;").ref;
    if (!action.IsValid()) unsupported("an action is required");
    const auto action_name = call.vm.StringUtf8(action);
    if (context->application_enabled) {
        for (const auto& service : context->service_components) {
            if (!service.enabled) continue;
            for (const auto& filter : service.intent_filters) {
                if (std::find(filter.actions.begin(), filter.actions.end(), action_name) == filter.actions.end())
                    continue;
                // A possible local match is not an absent service. Data
                // constraints and positive ResolveInfo materialization are deferred.
                unsupported(filter.has_data ? "candidate requires data-filter resolution"
                                            : "positive service resolution is not implemented");
            }
        }
    }
}
}  // namespace

std::optional<DexVmAndroidContext::ActivityCommand>
ResolveAndroidActivity(dx::IntrinsicContext& call, const Context& context, const std::int32_t request_code) {
    if (context->process_stopping) throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "process is stopping"};
    const auto intent = call.arguments[0].ref;
    if (!intent.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "intent is null"};
    const auto component = CallAndroidMethod(call.vm, intent, "getComponent", "()Landroid/content/ComponentName;").ref;
    const auto roots = call.vm.ProtectReferences(std::array{component});
    std::string component_name;
    std::string activity_class;
    if (component.IsValid()) {
        const auto package = CallAndroidMethod(call.vm, component, "getPackageName", "()Ljava/lang/String;").ref;
        if (call.vm.StringUtf8(package) != context->package_name) {
            const auto action = CallAndroidMethod(call.vm, intent, "getAction", "()Ljava/lang/String;").ref;
            const auto data = CallAndroidMethod(call.vm, intent, "getData", "()Landroid/net/Uri;").ref;
            const auto external_roots = call.vm.ProtectReferences(std::array{action, data});
            if (action.IsValid() && data.IsValid() && call.vm.StringUtf8(action) == "android.intent.action.VIEW") {
                const auto uri_text =
                    call.vm.StringUtf8(CallAndroidMethod(call.vm, data, "toString", "()Ljava/lang/String;").ref);
                const auto target = ParseDisabledWebTarget(uri_text);
                if (target.http_or_https && !context->strict_webview_errors) {
                    if (request_code >= 0) {
                        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.activity_result", 0);
                        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                              "external Activity results are unavailable"};
                    }
                    LogDisabledWeb(call.vm, "external_browser", target);
                    return std::nullopt;
                }
            }
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "startActivity outside this package is not supported"};
        }
        component_name =
            call.vm.StringUtf8(CallAndroidMethod(call.vm, component, "getClassName", "()Ljava/lang/String;").ref);
        activity_class = component_name;
        if (context->activity_inventory_known) {
            const auto* declared =
                loader::FindAndroidManifestActivityComponent(context->activity_components, component_name);
            if (!context->application_enabled || declared == nullptr || !declared->enabled) {
                throw dx::VmJavaThrow{"Landroid/content/ActivityNotFoundException;",
                                      "No enabled Activity found for component " + component_name};
            }
            if (declared->target_activity) {
                const auto* target = loader::FindAndroidManifestActivityComponent(context->activity_components,
                                                                                  *declared->target_activity);
                if (target == nullptr || !target->enabled)
                    throw dx::VmJavaThrow{"Landroid/content/ActivityNotFoundException;",
                                          "Activity alias target is not enabled: " + *declared->target_activity};
            }
            activity_class = declared->target_activity.value_or(declared->name);
        }
    } else {
        const auto unsupported = [&](const std::string& reason) -> void {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.activity_resolution", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", reason};
        };
        const auto data = CallAndroidMethod(call.vm, intent, "getData", "()Landroid/net/Uri;").ref;
        const auto type = CallAndroidMethod(call.vm, intent, "getType", "()Ljava/lang/String;").ref;
        const auto action = CallAndroidMethod(call.vm, intent, "getAction", "()Ljava/lang/String;").ref;
        const auto intent_roots = call.vm.ProtectReferences(std::array{data, type, action});
        if (action.IsValid() && data.IsValid() && call.vm.StringUtf8(action) == "android.intent.action.VIEW") {
            const auto uri_text =
                call.vm.StringUtf8(CallAndroidMethod(call.vm, data, "toString", "()Ljava/lang/String;").ref);
            const auto target = ParseDisabledWebTarget(uri_text);
            if (target.http_or_https) {
                if (context->strict_webview_errors) unsupported("external browser launch is disabled");
                if (request_code >= 0) {
                    if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.activity_result", 0);
                    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                          "external Activity results are unavailable"};
                }
                LogDisabledWeb(call.vm, "external_browser", target);
                return std::nullopt;
            }
        }
        if (!context->activity_inventory_known) unsupported("activity inventory is unavailable");
        if (data.IsValid() || type.IsValid()) unsupported("data and MIME activity resolution is not supported");
        if (!action.IsValid())
            throw dx::VmJavaThrow{"Landroid/content/ActivityNotFoundException;",
                                  "No Activity found for an Intent without an action"};
        const auto action_name = call.vm.StringUtf8(action);
        std::vector<std::string> categories;
        const auto category_set = CallAndroidMethod(call.vm, intent, "getCategories", "()Ljava/util/Set;").ref;
        if (category_set.IsValid()) {
            const auto iterator = CallAndroidMethod(call.vm, category_set, "iterator", "()Ljava/util/Iterator;").ref;
            const auto category_roots = call.vm.ProtectReferences(std::array{category_set, iterator});
            while (CallAndroidMethod(call.vm, iterator, "hasNext", "()Z").AsInt()) {
                const auto category = CallAndroidMethod(call.vm, iterator, "next", "()Ljava/lang/Object;").ref;
                categories.push_back(call.vm.StringUtf8(category));
            }
        }
        std::vector<const loader::AndroidManifestActivityComponent*> matches;
        if (context->application_enabled) {
            for (const auto& candidate : context->activity_components) {
                const auto* registered =
                    loader::FindAndroidManifestActivityComponent(context->activity_components, candidate.name);
                if (!candidate.enabled || !registered->enabled) continue;
                if (registered->target_activity) {
                    const auto* target = loader::FindAndroidManifestActivityComponent(context->activity_components,
                                                                                      *registered->target_activity);
                    if (target == nullptr || !target->enabled) continue;
                }
                for (const auto& filter : candidate.intent_filters) {
                    if (filter.has_data) continue;
                    if (std::find(filter.actions.begin(), filter.actions.end(), action_name) == filter.actions.end())
                        continue;
                    if (std::find(filter.categories.begin(), filter.categories.end(),
                                  "android.intent.category.DEFAULT") == filter.categories.end())
                        continue;
                    const auto categories_match = std::ranges::all_of(categories, [&](const std::string& category) {
                        return std::find(filter.categories.begin(), filter.categories.end(), category) !=
                               filter.categories.end();
                    });
                    if (!categories_match) continue;
                    if (std::ranges::none_of(matches,
                                             [&](const auto* match) { return match->name == registered->name; }))
                        matches.push_back(registered);
                    break;
                }
            }
        }
        if (matches.empty())
            throw dx::VmJavaThrow{"Landroid/content/ActivityNotFoundException;",
                                  "No Activity found for action " + action_name};
        if (matches.size() != 1U) unsupported("multiple matching activities require chooser resolution");
        const auto& resolved = *matches.front();
        component_name = resolved.name;
        activity_class = resolved.target_activity.value_or(resolved.name);
        const auto package = call.vm.NewStringUtf8(context->package_name);
        const auto name = call.vm.NewStringUtf8(component_name);
        const auto resolved_component = NewAndroidComponentName(call.vm, package, name);
        const auto resolved_roots = call.vm.ProtectReferences(std::array{package, name, resolved_component});
        static_cast<void>(CallAndroidMethod(call.vm, intent, "setComponent",
                                            "(Landroid/content/ComponentName;)Landroid/content/Intent;",
                                            {dx::VmValue::Ref(resolved_component)}));
    }
    std::uint32_t launch_mode{};
    bool package_affinity = true;
    if (context->activity_inventory_known) {
        const auto* target = loader::FindAndroidManifestActivityComponent(context->activity_components, component_name);
        if (target != nullptr) {
            launch_mode = target->launch_mode;
            package_affinity = !target->task_affinity || *target->task_affinity == context->package_name;
        }
    }
    std::replace(activity_class.begin(), activity_class.end(), '.', '/');
    const auto type = call.vm.Linker().ResolveDescriptor("Landroid/content/Intent;");
    const auto copy = call.vm.NewIntrinsicInstance("Landroid/content/Intent;");
    const auto copy_roots = call.vm.ProtectReferences(std::array{copy});
    const auto constructor = call.vm.Linker().FindDirectMethod(type, "<init>", "(Landroid/content/Intent;)V");
    if (!constructor)
        throw dx::DexVmError(dx::DexVmErrorReason::unresolved_reference, "Intent copy constructor is missing");
    const auto copied = call.vm.Call(*constructor, std::array{dx::VmValue::Ref(copy), dx::VmValue::Ref(intent)});
    if (copied.exception.IsValid())
        throw dx::VmJavaThrow{call.vm.Linker().Class(copied.exception_class).descriptor, copied.exception_message,
                              copied.exception};
    return DexVmAndroidContext::ActivityCommand{
        call.receiver, copy, "L" + activity_class + ";", component_name, request_code, launch_mode, package_affinity};
}

// android.content.SharedPreferences, $Editor and
// $OnSharedPreferenceChangeListener keep their original AOSP interface shape
// from BootDex; no intrinsic interface declarations remain here.

namespace {
std::vector<std::string> ContentUriPath(dx::Interpreter& vm, dx::VmObjectRef uri) {
    if (!uri.IsValid()) return {};
    const auto authority = CallAndroidMethod(vm, uri, "getAuthority", "()Ljava/lang/String;").ref;
    if (!authority.IsValid())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Invalid Uri used for observer"};
    std::vector<std::string> result{vm.StringUtf8(authority)};
    const auto parts = CallAndroidMethod(vm, uri, "getPathSegments", "()Ljava/util/List;").ref;
    const auto root = vm.ProtectReferences(std::array{parts});
    const auto count = CallAndroidMethod(vm, parts, "size", "()I").AsInt();
    for (int i = 0; i < count; ++i)
        result.push_back(
            vm.StringUtf8(CallAndroidMethod(vm, parts, "get", "(I)Ljava/lang/Object;", {dx::VmValue::Int(i)}).ref));
    return result;
}

constexpr auto kObserverCallback = "Lorg/ogplay/framework/LocalContentChange;";
constexpr auto kObserver = "Landroid/database/IContentObserver;";
}  // namespace

Decl Declare_local_ContentChange_Class(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(kObserverCallback, "Ljava/lang/Object;", {"Ljava/lang/Runnable;"});
    const auto transport = builder.BoundInstanceField("transport", kObserver, dx::kAccPrivate);
    const auto uri = builder.BoundInstanceField("uri", "Landroid/net/Uri;", dx::kAccPrivate);
    const auto self = builder.BoundInstanceField("self", "Z", dx::kAccPrivate);
    builder.Constructor("(Landroid/database/IContentObserver;Landroid/net/Uri;Z)V", [=](dx::IntrinsicContext& call) {
        dx::IntrinsicCall fields(call);
        fields.SetRef(transport, call.arguments[0].ref);
        fields.SetRef(uri, call.arguments[1].ref);
        fields.SetInt(self, call.arguments[2].AsInt());
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("run", "()V", [=](dx::IntrinsicContext& call) {
        dx::IntrinsicCall fields(call);
        return CallAndroidMethod(call.vm, fields.GetRef(transport), "onChange", "(ZLandroid/net/Uri;)V",
                                 {dx::VmValue::Int(fields.GetInt(self)), dx::VmValue::Ref(fields.GetRef(uri))});
    });
    return std::move(builder).Build();
}

void NotifyLocalContentChange(dx::Interpreter& vm, const Context& context, dx::VmObjectRef uri, dx::VmObjectRef sender,
                              bool wants_self) {
    const auto path = ContentUriPath(vm, uri);
    std::vector<dx::VmObjectRef> targets;
    for (const auto& item : context->local_content_observers) {
        if (item.transport == sender && !wants_self) continue;
        const auto common = std::min(path.size(), item.path.size());
        if (!std::equal(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(common), item.path.begin())) continue;
        if (path.size() <= item.path.size() || item.descendants) targets.push_back(item.transport);
    }
    const auto roots = vm.ProtectReferences(targets);
    for (const auto target : targets) {
        const auto type = vm.Model().ObjectClass(target);
        const auto backing =
            vm.Linker().FindFieldRecursive(type, "mContentObserver", "Landroid/database/ContentObserver;");
        if (backing) {
            const auto observer =
                dx::VmObjectRef{vm.Model().InstanceSlots(target)[vm.Linker().Field(*backing).slot].bits};
            if (!observer.IsValid()) continue;
            const auto handler =
                vm.Linker().FindFieldRecursive(vm.Linker().ResolveDescriptor("Landroid/database/ContentObserver;"),
                                               "mHandler", "Landroid/os/Handler;");
            if (!handler) throw std::logic_error("ContentObserver handler is unavailable");
            if (vm.Model().InstanceSlots(observer)[vm.Linker().Field(*handler).slot].bits != 0) {
                static_cast<void>(CallAndroidMethod(vm, target, "onChange", "(ZLandroid/net/Uri;)V",
                                                    {dx::VmValue::Int(target == sender), dx::VmValue::Ref(uri)}));
                continue;
            }
        }
        const auto callback =
            NewPlatformObject(vm, kObserverCallback, "(Landroid/database/IContentObserver;Landroid/net/Uri;Z)V",
                              {dx::VmValue::Ref(target), dx::VmValue::Ref(uri), dx::VmValue::Int(target == sender)});
        const auto callback_root = vm.ProtectReferences(std::array{callback});
        const auto looper = EnsureMainLooper(vm, context);
        auto& handler = context->singletons["content_observer_handler"];
        if (!handler.IsValid())
            handler =
                NewPlatformObject(vm, "Landroid/os/Handler;", "(Landroid/os/Looper;)V", {dx::VmValue::Ref(looper)});
        static_cast<void>(
            CallAndroidMethod(vm, handler, "post", "(Ljava/lang/Runnable;)Z", {dx::VmValue::Ref(callback)}));
    }
}

Decl Declare_local_ClipboardService_Class(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalClipboardService;",
                                                    "Landroid/content/IClipboard$Stub;");
    for (const auto& [name, descriptor] : std::array{
             std::pair{"setPrimaryClip", "(Landroid/content/ClipData;Ljava/lang/String;)V"},
             std::pair{"getPrimaryClip", "(Ljava/lang/String;)Landroid/content/ClipData;"},
             std::pair{"getPrimaryClipDescription", "(Ljava/lang/String;)Landroid/content/ClipDescription;"},
             std::pair{"hasPrimaryClip", "(Ljava/lang/String;)Z"},
             std::pair{"hasClipboardText", "(Ljava/lang/String;)Z"},
             std::pair{"addPrimaryClipChangedListener",
                       "(Landroid/content/IOnPrimaryClipChangedListener;Ljava/lang/String;)V"},
             std::pair{"removePrimaryClipChangedListener", "(Landroid/content/IOnPrimaryClipChangedListener;)V"},
         })
        RejectUnsupportedServiceMethod(builder, "android.content.IClipboard", name, descriptor);
    return std::move(builder).Build();
}

Decl Declare_local_ContentService_Class(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalContentService;",
                                                    "Landroid/content/IContentService$Stub;");
    builder.VirtualMethod(
        "registerContentObserver", "(Landroid/net/Uri;ZLandroid/database/IContentObserver;I)V",
        [context](dx::IntrinsicContext& call) {
            if (call.arguments[3].AsInt() != 0)
                throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "content observer outside the current user"};
            if (!call.arguments[0].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "uri == null"};
            const auto observer = dx::IntrinsicCall(call).NonNullRef(2, "observer");
            context->local_content_observers.push_back(
                {observer, ContentUriPath(call.vm, call.arguments[0].ref), call.arguments[1].AsInt() != 0});
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("unregisterContentObserver", "(Landroid/database/IContentObserver;)V",
                          [context](dx::IntrinsicContext& call) {
                              std::erase_if(context->local_content_observers,
                                            [&](const auto& item) { return item.transport == call.arguments[0].ref; });
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod("notifyChange", "(Landroid/net/Uri;Landroid/database/IContentObserver;ZZI)V",
                          [context](dx::IntrinsicContext& call) {
                              if (call.arguments[4].AsInt() != 0)
                                  throw dx::VmJavaThrow{"Ljava/lang/SecurityException;",
                                                        "content notification outside the current user"};
                              NotifyLocalContentChange(call.vm, context, call.arguments[0].ref, call.arguments[1].ref,
                                                       call.arguments[2].AsInt() != 0);
                              return dx::VmValue::Void();
                          });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- DVM-88: ContentValues, Cursor and bounded SQLite-on-VFS ------------

namespace ogplay::runtime::android_intrinsics {
namespace {

using DbValue = DexVmAndroidContext::DatabaseValue;

[[noreturn]] void DbThrow(const std::string& message) {
    throw dx::VmJavaThrow{"Landroid/database/sqlite/SQLiteException;", message};
}

[[noreturn]] void DbThrow(const database::Error& error) {
    const char* descriptor;
    switch (error.Code() & 0xff) {
    case SQLITE_IOERR: descriptor = "Landroid/database/sqlite/SQLiteDiskIOException;"; break;
    case SQLITE_CORRUPT:
    case SQLITE_NOTADB: descriptor = "Landroid/database/sqlite/SQLiteDatabaseCorruptException;"; break;
    case SQLITE_CONSTRAINT: descriptor = "Landroid/database/sqlite/SQLiteConstraintException;"; break;
    case SQLITE_ABORT: descriptor = "Landroid/database/sqlite/SQLiteAbortException;"; break;
    case SQLITE_DONE: descriptor = "Landroid/database/sqlite/SQLiteDoneException;"; break;
    case SQLITE_FULL: descriptor = "Landroid/database/sqlite/SQLiteFullException;"; break;
    case SQLITE_MISUSE: descriptor = "Landroid/database/sqlite/SQLiteMisuseException;"; break;
    case SQLITE_PERM: descriptor = "Landroid/database/sqlite/SQLiteAccessPermException;"; break;
    case SQLITE_BUSY: descriptor = "Landroid/database/sqlite/SQLiteDatabaseLockedException;"; break;
    case SQLITE_LOCKED: descriptor = "Landroid/database/sqlite/SQLiteTableLockedException;"; break;
    case SQLITE_READONLY: descriptor = "Landroid/database/sqlite/SQLiteReadOnlyDatabaseException;"; break;
    case SQLITE_CANTOPEN: descriptor = "Landroid/database/sqlite/SQLiteCantOpenDatabaseException;"; break;
    case SQLITE_TOOBIG: descriptor = "Landroid/database/sqlite/SQLiteBlobTooBigException;"; break;
    case SQLITE_RANGE: descriptor = "Landroid/database/sqlite/SQLiteBindOrColumnIndexOutOfRangeException;"; break;
    case SQLITE_NOMEM: descriptor = "Landroid/database/sqlite/SQLiteOutOfMemoryException;"; break;
    case SQLITE_MISMATCH: descriptor = "Landroid/database/sqlite/SQLiteDatatypeMismatchException;"; break;
    case SQLITE_INTERRUPT: descriptor = "Landroid/os/OperationCanceledException;"; break;
    default: descriptor = "Landroid/database/sqlite/SQLiteException;"; break;
    }
    throw dx::VmJavaThrow{descriptor, error.what()};
}

template <typename Fn>
auto DbNative(Fn fn) {
    return [fn = std::move(fn)](dx::IntrinsicContext& call) -> dx::VmValue {
        try {
            return fn(call);
        } catch (const dx::VmJavaThrow&) {
            throw;
        } catch (const database::Error& error) {
            DbThrow(error);
        } catch (const std::exception& error) {
            DbThrow(error.what());
        }
    };
}

std::string DbString(dx::IntrinsicContext& call, const dx::VmObjectRef ref) {
    if (!ref.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "null string"};
    return call.vm.StringUtf8(ref);
}

std::string ValueString(const DbValue& value) {
    if (std::holds_alternative<std::monostate>(value)) return {};
    if (const auto* text = std::get_if<std::string>(&value)) return *text;
    if (const auto* integer = std::get_if<std::int64_t>(&value)) return std::to_string(*integer);
    if (const auto* real = std::get_if<double>(&value)) return std::to_string(*real);
    return {};
}

}  // namespace

Decl Declare_android_database_CursorWindow_Natives(const Context& context) {
    const auto require = [context](const std::int32_t token) -> DexVmAndroidContext::CursorWindowState& {
        const auto found = context->cursor_windows.find(static_cast<std::uint32_t>(token));
        if (found == context->cursor_windows.end()) DbThrow("CursorWindow token is stale");
        return found->second;
    };
    const auto cell = [require](const std::int32_t token, const std::int32_t row,
                                const std::int32_t column) -> DbValue& {
        auto& window = require(token);
        if (row < 0 || column < 0 || static_cast<std::size_t>(row) >= window.rows.size() || column >= window.columns)
            DbThrow("CursorWindow index is invalid");
        return window.rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
    };
    const auto value_cost = [](const DbValue& value) -> std::size_t {
        if (const auto* text = std::get_if<std::string>(&value)) return text->size() + 1U;
        if (const auto* blob = std::get_if<std::vector<std::byte>>(&value)) return blob->size();
        return 0U;
    };
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/database/CursorWindow;", "Landroid/database/sqlite/SQLiteClosable;", {"Landroid/os/Parcelable;"});
    builder.MergesBootClass();
    builder.StaticMethod("nativeCreate", "(Ljava/lang/String;I)I", [context](dx::IntrinsicContext& call) {
        const auto size = call.arguments[1].AsInt();
        if (size <= 0) DbThrow("CursorWindow capacity is invalid");
        const auto token = context->next_cursor_window_token++;
        context->cursor_windows.emplace(
            token, DexVmAndroidContext::CursorWindowState{
                       call.arguments[0].ref.IsValid() ? call.vm.StringUtf8(call.arguments[0].ref) : "<unnamed>",
                       static_cast<std::size_t>(size)});
        return dx::VmValue::Int(static_cast<std::int32_t>(token));
    });
    builder.StaticMethod("nativeDispose", "(I)V", [context](dx::IntrinsicContext& call) {
        if (context->cursor_windows.erase(static_cast<std::uint32_t>(call.arguments[0].AsInt())) == 0)
            DbThrow("CursorWindow token is stale");
        return dx::VmValue::Void();
    });
    builder.StaticMethod("nativeClear", "(I)V", [require](dx::IntrinsicContext& call) {
        auto& window = require(call.arguments[0].AsInt());
        window.rows.clear();
        window.used = 0;
        return dx::VmValue::Void();
    });
    builder.StaticMethod("nativeGetNumRows", "(I)I", [require](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(require(call.arguments[0].AsInt()).rows.size()));
    });
    builder.StaticMethod("nativeSetNumColumns", "(II)Z", [require](dx::IntrinsicContext& call) {
        auto& window = require(call.arguments[0].AsInt());
        const auto columns = call.arguments[1].AsInt();
        if (columns < 0 || !window.rows.empty()) return dx::VmValue::Int(0);
        window.columns = columns;
        return dx::VmValue::Int(1);
    });
    builder.StaticMethod("nativeAllocRow", "(I)Z", [require](dx::IntrinsicContext& call) {
        auto& window = require(call.arguments[0].AsInt());
        const auto cost = static_cast<std::size_t>(window.columns) * 16U;
        if (window.used + cost > window.capacity) return dx::VmValue::Int(0);
        window.rows.emplace_back(static_cast<std::size_t>(window.columns));
        window.used += cost;
        return dx::VmValue::Int(1);
    });
    builder.StaticMethod("nativeFreeLastRow", "(I)V", [require, value_cost](dx::IntrinsicContext& call) {
        auto& window = require(call.arguments[0].AsInt());
        if (!window.rows.empty()) {
            std::size_t cost = static_cast<std::size_t>(window.columns) * 16U;
            for (const auto& value : window.rows.back())
                cost += value_cost(value);
            window.used -= std::min(window.used, cost);
            window.rows.pop_back();
        }
        return dx::VmValue::Void();
    });
    builder.StaticMethod("nativeGetType", "(III)I", [cell](dx::IntrinsicContext& call) {
        const auto& value = cell(call.arguments[0].AsInt(), call.arguments[1].AsInt(), call.arguments[2].AsInt());
        return dx::VmValue::Int(std::holds_alternative<std::monostate>(value) ? 0
                                : std::holds_alternative<std::int64_t>(value) ? 1
                                : std::holds_alternative<double>(value)       ? 2
                                : std::holds_alternative<std::string>(value)  ? 3
                                                                              : 4);
    });
    builder.StaticMethod("nativeGetLong", "(III)J", [cell](dx::IntrinsicContext& call) {
        const auto& v = cell(call.arguments[0].AsInt(), call.arguments[1].AsInt(), call.arguments[2].AsInt());
        return dx::VmValue::Long(std::holds_alternative<std::int64_t>(v) ? std::get<std::int64_t>(v)
                                                                         : std::stoll(ValueString(v)));
    });
    builder.StaticMethod("nativeGetDouble", "(III)D", [cell](dx::IntrinsicContext& call) {
        const auto& v = cell(call.arguments[0].AsInt(), call.arguments[1].AsInt(), call.arguments[2].AsInt());
        return dx::VmValue::Double(std::holds_alternative<double>(v) ? std::get<double>(v) : std::stod(ValueString(v)));
    });
    builder.StaticMethod("nativeGetString", "(III)Ljava/lang/String;", [cell](dx::IntrinsicContext& call) {
        const auto& v = cell(call.arguments[0].AsInt(), call.arguments[1].AsInt(), call.arguments[2].AsInt());
        return dx::VmValue::Ref(std::holds_alternative<std::monostate>(v) ? dx::VmObjectRef{}
                                                                          : call.vm.NewStringUtf8(ValueString(v)));
    });
    builder.StaticMethod("nativeGetBlob", "(III)[B", [cell](dx::IntrinsicContext& call) {
        const auto& v = cell(call.arguments[0].AsInt(), call.arguments[1].AsInt(), call.arguments[2].AsInt());
        if (!std::holds_alternative<std::vector<std::byte>>(v)) return dx::VmValue::Ref(dx::VmObjectRef{});
        const auto& bytes = std::get<std::vector<std::byte>>(v);
        auto out = call.vm.Model().NewPrimitiveArray(call.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                                                     static_cast<JniSize>(bytes.size()));
        call.vm.Model().WriteByteRegion(out, 0, bytes);
        return dx::VmValue::Ref(out);
    });
    const auto put = [require, value_cost](auto convert) {
        return [require, value_cost, convert](dx::IntrinsicContext& call) {
            auto& window = require(call.arguments[0].AsInt());
            const auto row = call.arguments[2].AsInt();
            const auto column = call.arguments[3].AsInt();
            if (row < 0 || column < 0 || static_cast<std::size_t>(row) >= window.rows.size() ||
                column >= window.columns)
                DbThrow("CursorWindow index is invalid");
            auto& target = window.rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
            auto replacement = convert(call);
            const auto old_cost = value_cost(target);
            const auto new_cost = value_cost(replacement);
            if (new_cost > old_cost && window.used + (new_cost - old_cost) > window.capacity)
                return dx::VmValue::Int(0);
            window.used = window.used - old_cost + new_cost;
            target = std::move(replacement);
            return dx::VmValue::Int(1);
        };
    };
    builder.StaticMethod("nativePutLong", "(IJII)Z",
                         put([](dx::IntrinsicContext& c) -> DbValue { return c.arguments[1].AsLong(); }));
    builder.StaticMethod("nativePutDouble", "(IDII)Z",
                         put([](dx::IntrinsicContext& c) -> DbValue { return c.arguments[1].AsDouble(); }));
    builder.StaticMethod("nativePutString", "(ILjava/lang/String;II)Z", put([](dx::IntrinsicContext& c) -> DbValue {
                             return c.arguments[1].ref.IsValid() ? DbValue(c.vm.StringUtf8(c.arguments[1].ref))
                                                                 : DbValue(std::monostate{});
                         }));
    builder.StaticMethod("nativePutBlob", "(I[BII)Z", put([](dx::IntrinsicContext& c) -> DbValue {
                             return c.arguments[1].ref.IsValid()
                                        ? DbValue(c.vm.Model().ReadByteRegion(
                                              c.arguments[1].ref, 0, c.vm.Model().ArrayLength(c.arguments[1].ref)))
                                        : DbValue(std::monostate{});
                         }));
    builder.StaticMethod("nativePutNull", "(III)Z", [cell](dx::IntrinsicContext& c) {
        cell(c.arguments[0].AsInt(), c.arguments[1].AsInt(), c.arguments[2].AsInt()) = std::monostate{};
        return dx::VmValue::Int(1);
    });
    builder.StaticMethod("nativeGetName", "(I)Ljava/lang/String;", [require](dx::IntrinsicContext& c) {
        return dx::VmValue::Ref(c.vm.NewStringUtf8(require(c.arguments[0].AsInt()).name));
    });
    builder.StaticMethod("nativeCreateFromParcel", "(Landroid/os/Parcel;)I", [](dx::IntrinsicContext&) -> dx::VmValue {
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "CursorWindow Parcel transport is outside the process boundary"};
    });
    builder.StaticMethod("nativeWriteToParcel", "(ILandroid/os/Parcel;)V", [](dx::IntrinsicContext&) -> dx::VmValue {
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "CursorWindow Parcel transport is outside the process boundary"};
    });
    builder.StaticMethod(
        "nativeCopyStringToBuffer", "(IIILandroid/database/CharArrayBuffer;)V",
        [cell](dx::IntrinsicContext& call) -> dx::VmValue {
            const auto buffer = call.arguments[3].ref;
            if (!buffer.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "buffer == null"};
            const auto& value = cell(call.arguments[0].AsInt(), call.arguments[1].AsInt(), call.arguments[2].AsInt());
            if (std::holds_alternative<std::vector<std::byte>>(value)) DbThrow("Unable to convert BLOB to string");
            const auto text = std::holds_alternative<std::monostate>(value)
                                  ? std::u16string{}
                                  : call.vm.Model().StringValue(call.vm.NewStringUtf8(ValueString(value)));
            const auto find_field = [&](const char* name, const char* descriptor) {
                const auto field =
                    call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(buffer), name, descriptor);
                if (!field.has_value())
                    throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                                         "CharArrayBuffer field is unavailable");
                return call.vm.Linker().Field(*field).slot;
            };
            const auto data_slot = find_field("data", "[C");
            const auto size_slot = find_field("sizeCopied", "I");
            auto slots = call.vm.Model().InstanceSlots(buffer);
            auto data = dx::VmObjectRef(slots[data_slot].bits);
            if (!data.IsValid() || call.vm.Model().ArrayLength(data) < static_cast<JniSize>(text.size())) {
                data =
                    call.vm.Model().NewPrimitiveArray(call.vm.Linker().ResolveDescriptor("[C"),
                                                      JniPrimitiveKind::character, static_cast<JniSize>(text.size()));
                slots[data_slot] = {data.Value(), dx::SlotTag::ref};
            }
            for (std::size_t index = 0; index < text.size(); ++index)
                call.vm.Model().SetPrimitiveElement(data, static_cast<JniSize>(index), text[index]);
            slots[size_slot] = {static_cast<std::uint32_t>(text.size()), dx::SlotTag::cat1};
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

Decl Declare_android_database_sqlite_SQLiteConnection_Natives(const Context& context) {
    const auto connection = [context](std::int32_t token) {
        const auto found = context->sqlite_connections.find(static_cast<std::uint32_t>(token));
        if (found == context->sqlite_connections.end() || !found->second->IsOpen())
            DbThrow("SQLite connection token is stale");
        return found->second;
    };
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/database/sqlite/SQLiteConnection;", "Ljava/lang/Object;");
    builder.MergesBootClass();
    builder.StaticMethod(
        "nativeOpen", "(Ljava/lang/String;ILjava/lang/String;ZZ)I", DbNative([context](dx::IntrinsicContext& call) {
            if (context->vfs == nullptr) DbThrow("SQLite VFS is unavailable");
            const auto path = DbString(call, call.arguments[0].ref);
            const auto flags = call.arguments[1].AsInt();
            const auto token = context->next_sqlite_connection_token++;
            context->sqlite_connections.emplace(
                token,
                database::Connection::Open(
                    *context->vfs, path, {},
                    [context] { return 1'400'000'000'000LL + context->uptime_millis.load(std::memory_order_relaxed); },
                    hal::FillSecureRandom,
                    {.read_only = (flags & 0x00000001) != 0, .create = (flags & 0x10000000) != 0}));
            return dx::VmValue::Int(static_cast<std::int32_t>(token));
        }));
    builder.StaticMethod("nativeClose", "(I)V", DbNative([context, connection](dx::IntrinsicContext& call) {
                             const auto token = call.arguments[0].AsInt();
                             connection(token)->Close();
                             context->sqlite_connections.erase(static_cast<std::uint32_t>(token));
                             return dx::VmValue::Void();
                         }));
    builder.StaticMethod(
        "nativePrepareStatement", "(ILjava/lang/String;)I", DbNative([connection](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(static_cast<std::int32_t>(
                connection(call.arguments[0].AsInt())->Prepare(DbString(call, call.arguments[1].ref))));
        }));
    builder.StaticMethod(
        "nativeFinalizeStatement", "(II)V", DbNative([connection](dx::IntrinsicContext& call) {
            connection(call.arguments[0].AsInt())->Finalize(static_cast<std::uint32_t>(call.arguments[1].AsInt()));
            return dx::VmValue::Void();
        }));
    builder.StaticMethod("nativeGetParameterCount", "(II)I", DbNative([connection](dx::IntrinsicContext& call) {
                             return dx::VmValue::Int(
                                 connection(call.arguments[0].AsInt())
                                     ->ParameterCount(static_cast<std::uint32_t>(call.arguments[1].AsInt())));
                         }));
    builder.StaticMethod(
        "nativeIsReadOnly", "(II)Z", DbNative([connection](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(
                connection(call.arguments[0].AsInt())->IsReadOnly(static_cast<std::uint32_t>(call.arguments[1].AsInt()))
                    ? 1
                    : 0);
        }));
    builder.StaticMethod("nativeGetColumnCount", "(II)I", DbNative([connection](dx::IntrinsicContext& call) {
                             return dx::VmValue::Int(
                                 connection(call.arguments[0].AsInt())
                                     ->ColumnCount(static_cast<std::uint32_t>(call.arguments[1].AsInt())));
                         }));
    builder.StaticMethod(
        "nativeGetColumnName", "(III)Ljava/lang/String;", DbNative([connection](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(call.vm.NewStringUtf8(
                connection(call.arguments[0].AsInt())
                    ->ColumnName(static_cast<std::uint32_t>(call.arguments[1].AsInt()), call.arguments[2].AsInt())));
        }));
    const auto bind = [connection](auto value) {
        return DbNative([connection, value](dx::IntrinsicContext& call) {
            connection(call.arguments[0].AsInt())
                ->Bind(static_cast<std::uint32_t>(call.arguments[1].AsInt()), call.arguments[2].AsInt(), value(call));
            return dx::VmValue::Void();
        });
    };
    builder.StaticMethod("nativeBindNull", "(III)V",
                         bind([](dx::IntrinsicContext&) -> DbValue { return std::monostate{}; }));
    builder.StaticMethod("nativeBindLong", "(IIIJ)V",
                         bind([](dx::IntrinsicContext& call) -> DbValue { return call.arguments[3].AsLong(); }));
    builder.StaticMethod("nativeBindDouble", "(IIID)V",
                         bind([](dx::IntrinsicContext& call) -> DbValue { return call.arguments[3].AsDouble(); }));
    builder.StaticMethod(
        "nativeBindString", "(IIILjava/lang/String;)V",
        bind([](dx::IntrinsicContext& call) -> DbValue { return DbString(call, call.arguments[3].ref); }));
    builder.StaticMethod("nativeBindBlob", "(III[B)V", bind([](dx::IntrinsicContext& call) -> DbValue {
                             return call.vm.Model().ReadByteRegion(call.arguments[3].ref, 0,
                                                                   call.vm.Model().ArrayLength(call.arguments[3].ref));
                         }));
    builder.StaticMethod(
        "nativeResetStatementAndClearBindings", "(II)V", DbNative([connection](dx::IntrinsicContext& call) {
            connection(call.arguments[0].AsInt())->Reset(static_cast<std::uint32_t>(call.arguments[1].AsInt()));
            return dx::VmValue::Void();
        }));
    builder.StaticMethod(
        "nativeExecute", "(II)V", DbNative([connection](dx::IntrinsicContext& call) {
            connection(call.arguments[0].AsInt())->Execute(static_cast<std::uint32_t>(call.arguments[1].AsInt()));
            return dx::VmValue::Void();
        }));
    builder.StaticMethod("nativeExecuteForLong", "(II)J", DbNative([connection](dx::IntrinsicContext& call) {
                             const auto value =
                                 connection(call.arguments[0].AsInt())
                                     ->ExecuteScalar(static_cast<std::uint32_t>(call.arguments[1].AsInt()));
                             if (const auto* integer = std::get_if<std::int64_t>(&value))
                                 return dx::VmValue::Long(*integer);
                             return dx::VmValue::Long(std::stoll(ValueString(value)));
                         }));
    builder.StaticMethod(
        "nativeExecuteForString", "(II)Ljava/lang/String;", DbNative([connection](dx::IntrinsicContext& call) {
            const auto value = connection(call.arguments[0].AsInt())
                                   ->ExecuteScalar(static_cast<std::uint32_t>(call.arguments[1].AsInt()));
            return dx::VmValue::Ref(std::holds_alternative<std::monostate>(value)
                                        ? dx::VmObjectRef{}
                                        : call.vm.NewStringUtf8(ValueString(value)));
        }));
    builder.StaticMethod("nativeExecuteForChangedRowCount", "(II)I", DbNative([connection](dx::IntrinsicContext& call) {
                             auto db = connection(call.arguments[0].AsInt());
                             db->Execute(static_cast<std::uint32_t>(call.arguments[1].AsInt()));
                             return dx::VmValue::Int(db->ChangedRows());
                         }));
    builder.StaticMethod("nativeExecuteForLastInsertedRowId", "(II)J",
                         DbNative([connection](dx::IntrinsicContext& call) {
                             auto db = connection(call.arguments[0].AsInt());
                             db->Execute(static_cast<std::uint32_t>(call.arguments[1].AsInt()));
                             return dx::VmValue::Long(db->ChangedRows() > 0 ? db->LastInsertRowId() : -1);
                         }));
    builder.StaticMethod(
        "nativeExecuteForCursorWindow", "(IIIIIZ)J", DbNative([context, connection](dx::IntrinsicContext& call) {
            const auto found = context->cursor_windows.find(static_cast<std::uint32_t>(call.arguments[2].AsInt()));
            if (found == context->cursor_windows.end()) DbThrow("CursorWindow token is stale");
            auto& window = found->second;
            auto result =
                connection(call.arguments[0].AsInt())
                    ->QueryWindow(static_cast<std::uint32_t>(call.arguments[1].AsInt()), call.arguments[3].AsInt(),
                                  call.arguments[4].AsInt(), call.arguments[5].AsInt() != 0, window.capacity);
            window.rows = std::move(result.rows);
            window.used = result.used;
            window.columns = result.columns;
            return dx::VmValue::Long((static_cast<std::int64_t>(result.start_position) << 32) |
                                     static_cast<std::uint32_t>(result.total_rows));
        }));
    builder.StaticMethod("nativeGetDbLookaside", "(I)I", [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.StaticMethod("nativeCancel", "(I)V", DbNative([connection](dx::IntrinsicContext& call) {
                             connection(call.arguments[0].AsInt())->Cancel();
                             return dx::VmValue::Void();
                         }));
    builder.StaticMethod("nativeResetCancel", "(IZ)V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.StaticMethod(
        "nativeRegisterLocalizedCollators", "(ILjava/lang/String;)V",
        DbNative([connection](dx::IntrinsicContext& call) -> dx::VmValue {
            connection(call.arguments[0].AsInt())->RegisterLocalizedCollators(DbString(call, call.arguments[1].ref));
            return dx::VmValue::Void();
        }));
    builder.StaticMethod("nativeRegisterCustomFunction", "(ILandroid/database/sqlite/SQLiteCustomFunction;)V",
                         [](dx::IntrinsicContext&) -> dx::VmValue {
                             throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                                   "custom SQLite functions are unavailable"};
                         });
    builder.StaticMethod("nativeExecuteForBlobFileDescriptor", "(II)I",
                         [](dx::IntrinsicContext&) { return dx::VmValue::Int(-1); });
    return std::move(builder).Build();
}

Decl Declare_android_database_sqlite_SQLiteDebug_Natives(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/database/sqlite/SQLiteDebug;", "Ljava/lang/Object;");
    builder.MergesBootClass();
    builder.StaticMethod("nativeGetPagerStats", "(Landroid/database/sqlite/SQLiteDebug$PagerStats;)V",
                         [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    return std::move(builder).Build();
}

Decl Declare_android_database_sqlite_SQLiteConnection_Operation_Overrides(const Context&) {
    auto builder =
        dx::IntrinsicClassBuilder::Class("Landroid/database/sqlite/SQLiteConnection$Operation;", "Ljava/lang/Object;");
    builder.MergesBootClass();
    // sDateFormat is used only by verbose operation diagnostics. Normal SQL
    // execution never reads it; keep it null until NativeDecimalFormat exists.
    builder.ClassInitializer([](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    return std::move(builder).Build();
}

Decl Declare_android_database_sqlite_SQLiteGlobal_Natives(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/database/sqlite/SQLiteGlobal;", "Ljava/lang/Object;");
    builder.MergesBootClass();
    builder.StaticMethod("nativeReleaseMemory", "()I", [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime {

void RegisterAndroidDatabaseStateTables(dexvm::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context) {
    if (context == nullptr) return;
    vm.RegisterIntrinsicStateTable(
        {"android.database",
         [context](const dexvm::VmObjectRef owner, const dexvm::VmRootVisitor& visit) {
             if (const auto found = context->sqlite_helpers.find(owner.Value());
                 found != context->sqlite_helpers.end() && found->second.database.IsValid()) {
                 visit(found->second.database);
             }
         },
         [context](const dexvm::VmObjectRef owner) { context->sqlite_helpers.erase(owner.Value()); },
         {}});
}

void ReleaseAndroidDatabaseResources(const std::shared_ptr<DexVmAndroidContext>& context) {
    if (context == nullptr) return;
    for (auto& [token, connection] : context->sqlite_connections) {
        static_cast<void>(token);
        if (connection && connection->IsOpen()) connection->Close();
    }
    context->sqlite_connections.clear();
    context->cursor_windows.clear();
}

}  // namespace ogplay::runtime

// ---- migrated from android_content_Context.cpp ----
namespace ogplay::runtime::android_intrinsics {

namespace {

// Reads a preferences file once per name. Damaged XML is a real failure.

}  // namespace

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_IntentFilter.cpp ----

namespace {

constexpr std::int32_t kMatchAdjustmentNormal = 0x00008000;
constexpr std::int32_t kMatchCategoryEmpty = 0x00100000;

[[nodiscard]] bool FilterContains(const std::vector<std::string>& values, const std::string_view value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

}  // namespace

// ---- migrated from android_content_pm_PackageManager.cpp ----

namespace ogplay::runtime::android_intrinsics {

namespace {

constexpr std::int32_t kGetActivities = 0x00000001;
constexpr std::int32_t kGetMetaData = 0x00000080;
constexpr std::int32_t kGetDisabledComponents = 0x00000200;
constexpr std::int32_t kGetPermissions = 0x00001000;
// This flag only adds uninstalled or blocked packages, absent from the sealed single-APK inventory.
constexpr std::int32_t kGetUninstalledPackages = 0x00002000;
constexpr std::int32_t kMatchDefaultOnly = 0x00010000;
constexpr std::int32_t kPermissionGranted = 0;
constexpr std::int32_t kPermissionDenied = -1;

[[nodiscard]] const dx::LinkedField& Field(dx::IntrinsicContext& call, const dx::VmObjectRef object,
                                           const std::string_view name, const std::string_view descriptor) {
    const auto field = call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(object), std::string(name),
                                                           std::string(descriptor));
    if (!field.has_value()) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             "PackageManager field is not linked: " + std::string(name));
    }
    return call.vm.Linker().Field(*field);
}

void SetInt(dx::IntrinsicContext& call, const dx::VmObjectRef object, const std::string_view name,
            const std::int32_t value) {
    const auto& field = Field(call, object, name, "I");
    call.vm.Model().InstanceSlots(object)[field.slot] = {static_cast<std::uint32_t>(value), dx::SlotTag::cat1};
}

void SetBoolean(dx::IntrinsicContext& call, const dx::VmObjectRef object, const std::string_view name,
                const bool value) {
    const auto& field = Field(call, object, name, "Z");
    call.vm.Model().InstanceSlots(object)[field.slot] = {value ? 1U : 0U, dx::SlotTag::cat1};
}

void SetRef(dx::IntrinsicContext& call, const dx::VmObjectRef object, const std::string_view name,
            const std::string_view descriptor, const dx::VmObjectRef value) {
    const auto& field = Field(call, object, name, descriptor);
    call.vm.Model().InstanceSlots(object)[field.slot] = {value.Value(), dx::SlotTag::ref};
}

[[nodiscard]] dx::VmObjectRef String(dx::IntrinsicContext& call, const std::string& value) {
    return call.vm.NewStringUtf8(value);
}

[[nodiscard]] std::string RequiredString(dx::IntrinsicContext& call, const std::size_t argument,
                                         const std::string_view name) {
    dx::IntrinsicCall typed(call);
    return call.vm.StringUtf8(typed.NonNullRef(argument, name));
}

void RequireCurrentPackage(const Context& context, const std::string_view package_name) {
    if (package_name != context->package_name) {
        throw dx::VmJavaThrow{"Landroid/content/pm/PackageManager$NameNotFoundException;", std::string(package_name)};
    }
}

void RequireFlags(dx::IntrinsicContext& call, const std::int32_t flags, const std::int32_t supported,
                  const std::string_view method, const std::string_view capability) {
    if ((flags & ~supported) != 0) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented(capability, 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              std::string(method) + " flags are outside the bounded API19 " +
                                  "PackageManager surface: " + std::to_string(flags)};
    }
}

void RequireComponentInfoQuery(dx::IntrinsicContext& call, const bool known, const std::int32_t flags,
                               const std::string_view capability, const std::string_view method) {
    if (known && (flags & ~(kGetMetaData | kGetDisabledComponents | kGetUninstalledPackages)) == 0) return;
    if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented(capability, 0);
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                          std::string(method) +
                              " requires current-package component facts and "
                              "flags 0, GET_META_DATA, GET_DISABLED_COMPONENTS or GET_UNINSTALLED_PACKAGES"};
}

[[nodiscard]] std::pair<std::string, std::string>
ReadComponentIdentity(dx::IntrinsicContext& call, const Context& context, const dx::VmObjectRef component) {
    const auto package = CallAndroidMethod(call.vm, component, "getPackageName", "()Ljava/lang/String;").ref;
    const auto name = CallAndroidMethod(call.vm, component, "getClassName", "()Ljava/lang/String;").ref;
    if (!package.IsValid() || !name.IsValid())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "ComponentName has no package or class"};
    const auto package_name = call.vm.StringUtf8(package);
    RequireCurrentPackage(context, package_name);
    return {package_name, call.vm.StringUtf8(name)};
}

[[nodiscard]] dx::VmObjectRef MakeStringArray(dx::IntrinsicContext& call, const std::vector<std::string>& values) {
    const auto array_class = call.vm.Linker().ResolveDescriptor("[Ljava/lang/String;");
    const auto string_class = call.vm.Linker().ResolveDescriptor("Ljava/lang/String;");
    const auto array = call.vm.Model().NewObjectArray(array_class, string_class, static_cast<JniSize>(values.size()));
    JniSize index{};
    for (const auto& value : values) {
        call.vm.Model().SetObjectElement(array, index++, String(call, value));
    }
    return array;
}

[[nodiscard]] std::string NormalizedComponentName(const std::string& package, const std::string& name) {
    try {
        return loader::NormalizeAndroidManifestClassName(package, name);
    } catch (const loader::AndroidManifestStartupError& error) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, error.what());
    }
}

void PopulateComponentInfo(dx::IntrinsicContext& call, const Context& context, const dx::VmObjectRef info,
                           const std::string& name, const bool enabled, const bool exported,
                           const std::string& process_name, const dx::VmObjectRef application_info) {
    const auto roots = call.vm.ProtectReferences(std::array{info, application_info});
    SetRef(call, info, "name", "Ljava/lang/String;", String(call, name));
    SetRef(call, info, "packageName", "Ljava/lang/String;", String(call, context->package_name));
    SetRef(call, info, "applicationInfo", "Landroid/content/pm/ApplicationInfo;", application_info);
    SetBoolean(call, info, "enabled", enabled);
    SetBoolean(call, info, "exported", exported);
    SetRef(call, info, "processName", "Ljava/lang/String;",
           String(call, process_name.empty() ? context->package_name : process_name));
}

void SetComponentMetaData(dx::IntrinsicContext& call, const Context& context, const dx::VmObjectRef info,
                          const std::vector<loader::AndroidManifestMetaData>& meta_data, const bool include_meta_data) {
    if (!include_meta_data || meta_data.empty()) return;
    const auto root = call.vm.ProtectReferences(std::array{info});
    const auto bundle = NewAndroidBundle(call.vm);
    const auto bundle_root = call.vm.ProtectReferences(std::array{bundle});
    for (const auto& item : meta_data)
        PutAndroidMetaData(call, context, bundle, item.name, item.value);
    SetRef(call, info, "metaData", "Landroid/os/Bundle;", bundle);
}

[[nodiscard]] dx::VmObjectRef MakeComponentActivityInfo(dx::IntrinsicContext& call, const Context& context,
                                                        const std::string& name, const bool enabled,
                                                        const bool exported, const std::string& process_name,
                                                        const std::optional<std::string>& permission,
                                                        const dx::VmObjectRef application_info) {
    const auto info = call.vm.NewIntrinsicInstance("Landroid/content/pm/ActivityInfo;");
    const auto roots = call.vm.ProtectReferences(std::array{info, application_info});
    PopulateComponentInfo(call, context, info, name, enabled, exported, process_name, application_info);
    if (permission) {
        SetRef(call, info, "permission", "Ljava/lang/String;", String(call, *permission));
    }
    return info;
}

[[nodiscard]] dx::VmObjectRef MakeActivityInfo(dx::IntrinsicContext& call, const Context& context,
                                               const loader::AndroidManifestActivityComponent& component,
                                               const dx::VmObjectRef application_info) {
    const auto info =
        MakeComponentActivityInfo(call, context, NormalizedComponentName(context->package_name, component.name),
                                  component.enabled, loader::AndroidManifestActivityExported(component),
                                  context->application_process_name, std::nullopt, application_info);
    const auto roots = call.vm.ProtectReferences(std::array{info});
    SetInt(call, info, "theme", static_cast<std::int32_t>(component.theme.value_or(0U)));
    SetInt(call, info, "launchMode", static_cast<std::int32_t>(component.launch_mode));
    SetInt(call, info, "screenOrientation", component.screen_orientation);
    SetInt(call, info, "configChanges", static_cast<int>(component.config_changes));
    const auto affinity = component.task_affinity.value_or(context->package_name);
    SetRef(call, info, "taskAffinity", "Ljava/lang/String;",
           affinity.empty() ? dx::VmObjectRef{} : String(call, affinity));
    if (component.kind == loader::AndroidManifestComponentKind::activity_alias &&
        component.target_activity.has_value()) {
        SetRef(call, info, "targetActivity", "Ljava/lang/String;",
               String(call, NormalizedComponentName(context->package_name, *component.target_activity)));
    }
    return info;
}

[[nodiscard]] dx::VmObjectRef MakeReceiverInfo(dx::IntrinsicContext& call, const Context& context,
                                               const loader::AndroidManifestReceiverComponent& receiver,
                                               const bool include_meta_data, const dx::VmObjectRef application_info) {
    const auto info = MakeComponentActivityInfo(call, context, receiver.name, receiver.enabled,
                                                loader::AndroidManifestReceiverExported(receiver),
                                                receiver.process_name, receiver.permission, application_info);
    SetComponentMetaData(call, context, info, receiver.meta_data, include_meta_data);
    return info;
}

// These are ordinary BootDex objects: constructors establish Java defaults
// (notably ArrayList storage and ResolveInfo.specificIndex/targetUserId).
[[nodiscard]] dx::VmObjectRef NewQueryObject(dx::IntrinsicContext& call, const char* descriptor) {
    const auto klass = call.vm.Linker().ResolveDescriptor(descriptor);
    const auto propagate = [&](const dx::VmCallOutcome& outcome) {
        if (outcome.exception.IsValid())
            throw dx::VmJavaThrow{call.vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                                  outcome.exception};
    };
    propagate(call.vm.EnsureClassInitialized(klass));
    const auto object = call.vm.NewIntrinsicInstance(descriptor);
    const auto root = call.vm.ProtectReferences(std::array{object});
    const auto constructor = call.vm.Linker().FindDirectMethod(klass, "<init>", "()V");
    if (!constructor)
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             "package query BootDex constructor is unavailable");
    propagate(call.vm.Call(*constructor, std::array{dx::VmValue::Ref(object)}));
    return object;
}

[[nodiscard]] dx::VmObjectRef QueryBroadcastReceivers(dx::IntrinsicContext& call, const Context& context,
                                                      const dx::VmObjectRef intent, const std::int32_t flags) {
    const auto unsupported = [&call](const std::string& reason) -> void {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.receiver_query", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "queryBroadcastReceivers: " + reason};
    };
    if (!context->receiver_inventory_known) unsupported("receiver inventory is unavailable");
    if ((flags & ~kMatchDefaultOnly) != 0) unsupported("only flags=0 or MATCH_DEFAULT_ONLY is supported");
    for (const auto& [name, signature] :
         {std::pair{"getSelector", "()Landroid/content/Intent;"}, std::pair{"getData", "()Landroid/net/Uri;"},
          std::pair{"getType", "()Ljava/lang/String;"}}) {
        if (CallAndroidMethod(call.vm, intent, name, signature).ref.IsValid())
            unsupported("only implicit Intents without selector/data/type are supported");
    }
    const auto package = CallAndroidMethod(call.vm, intent, "getPackage", "()Ljava/lang/String;").ref;
    if (package.IsValid() && call.vm.StringUtf8(package) != context->package_name)
        unsupported("external package inventory is unavailable");
    const auto action = CallAndroidMethod(call.vm, intent, "getAction", "()Ljava/lang/String;").ref;
    if (!action.IsValid() || call.vm.StringUtf8(action).empty()) unsupported("a nonempty action is required");
    const auto action_name = call.vm.StringUtf8(action);
    std::vector<std::string> categories;
    const auto category_set = CallAndroidMethod(call.vm, intent, "getCategories", "()Ljava/util/Set;").ref;
    if (category_set.IsValid()) {
        const auto category_root = call.vm.ProtectReferences(std::array{category_set});
        const auto iterator = CallAndroidMethod(call.vm, category_set, "iterator", "()Ljava/util/Iterator;").ref;
        const auto iterator_root = call.vm.ProtectReferences(std::array{iterator});
        while (CallAndroidMethod(call.vm, iterator, "hasNext", "()Z").AsInt()) {
            categories.push_back(
                call.vm.StringUtf8(CallAndroidMethod(call.vm, iterator, "next", "()Ljava/lang/Object;").ref));
        }
    }

    struct Match {
        const loader::AndroidManifestReceiverComponent* receiver;
        const loader::AndroidManifestIntentFilter* filter;
        bool is_default;
    };

    std::vector<Match> matches;
    if (context->application_enabled) {
        for (const auto& receiver : context->receiver_components) {
            if (!receiver.enabled) continue;
            if (std::any_of(matches.begin(), matches.end(),
                            [&](const auto& match) { return match.receiver->name == receiver.name; }))
                continue;
            for (const auto& filter : receiver.intent_filters) {
                if (!FilterContains(filter.actions, action_name) ||
                    !std::ranges::all_of(
                        categories, [&](const auto& category) { return FilterContains(filter.categories, category); }))
                    continue;
                const auto is_default = FilterContains(filter.categories, "android.intent.category.DEFAULT");
                if ((flags & kMatchDefaultOnly) != 0 && !is_default) continue;
                if (filter.has_data) unsupported("candidate requires data-filter resolution");
                matches.push_back({&receiver, &filter, is_default});
                // API 19 IntentResolver accepts the first matching filter for a
                // component, then sorts results; it does not pick its highest priority.
                break;
            }
        }
    }
    std::stable_sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
        if (a.filter->priority != b.filter->priority) return a.filter->priority > b.filter->priority;
        // preferredOrder=0, match=EMPTY+NORMAL and system=false are equal for
        // this installed APK and the supported data-free query shape.
        return a.is_default && !b.is_default;
    });
    const auto list = NewQueryObject(call, "Ljava/util/ArrayList;");
    const auto list_root = call.vm.ProtectReferences(std::array{list});
    if (matches.empty()) return list;
    const auto application = MakeApplicationInfo(call, context, false);
    const auto application_root = call.vm.ProtectReferences(std::array{application});
    for (const auto& match : matches) {
        const auto result = NewQueryObject(call, "Landroid/content/pm/ResolveInfo;");
        const auto result_root = call.vm.ProtectReferences(std::array{result});
        SetRef(call, result, "activityInfo", "Landroid/content/pm/ActivityInfo;",
               MakeReceiverInfo(call, context, *match.receiver, false, application));
        SetInt(call, result, "priority", match.filter->priority);
        SetInt(call, result, "preferredOrder", 0);
        SetInt(call, result, "match", kMatchCategoryEmpty + kMatchAdjustmentNormal);
        SetBoolean(call, result, "isDefault", match.is_default);
        SetBoolean(call, result, "system", false);
        SetInt(call, result, "icon", static_cast<std::int32_t>(match.filter->icon));
        if (match.filter->label) {
            if (const auto* resource = std::get_if<std::uint32_t>(&*match.filter->label))
                SetInt(call, result, "labelRes", static_cast<std::int32_t>(*resource));
            else
                SetRef(call, result, "nonLocalizedLabel", "Ljava/lang/CharSequence;",
                       String(call, std::get<std::string>(*match.filter->label)));
        }
        static_cast<void>(CallAndroidMethod(call.vm, list, "add", "(Ljava/lang/Object;)Z", {dx::VmValue::Ref(result)}));
    }
    return list;
}

[[nodiscard]] dx::VmObjectRef MakeServiceInfo(dx::IntrinsicContext& call, const Context& context,
                                              const loader::AndroidManifestServiceComponent& service,
                                              const bool include_meta_data, const dx::VmObjectRef application_info) {
    const auto info = call.vm.NewIntrinsicInstance("Landroid/content/pm/ServiceInfo;");
    const auto roots = call.vm.ProtectReferences(std::array{info, application_info});
    PopulateComponentInfo(call, context, info, service.name, service.enabled,
                          loader::AndroidManifestServiceExported(service), service.process_name, application_info);
    if (service.permission) SetRef(call, info, "permission", "Ljava/lang/String;", String(call, *service.permission));
    SetInt(call, info, "flags", static_cast<std::int32_t>(service.flags));
    SetComponentMetaData(call, context, info, service.meta_data, include_meta_data);
    return info;
}

[[nodiscard]] dx::VmObjectRef MakePermissionInfo(dx::IntrinsicContext& call, const Context& context,
                                                 const loader::AndroidManifestPermissionDefinition& definition,
                                                 const bool include_meta_data) {
    const auto info = call.vm.NewIntrinsicInstance("Landroid/content/pm/PermissionInfo;");
    const auto root = call.vm.ProtectReferences(std::array{info});
    SetRef(call, info, "name", "Ljava/lang/String;", String(call, definition.name));
    SetRef(call, info, "packageName", "Ljava/lang/String;", String(call, definition.package_name));
    SetInt(call, info, "protectionLevel", static_cast<std::int32_t>(definition.protection_level));
    SetInt(call, info, "flags", static_cast<std::int32_t>(definition.flags));
    SetInt(call, info, "descriptionRes", static_cast<std::int32_t>(definition.description_res));
    if (definition.group) SetRef(call, info, "group", "Ljava/lang/String;", String(call, *definition.group));
    if (definition.nonlocalized_description)
        SetRef(call, info, "nonLocalizedDescription", "Ljava/lang/CharSequence;",
               String(call, *definition.nonlocalized_description));
    SetComponentMetaData(call, context, info, definition.meta_data, include_meta_data);
    return info;
}

[[nodiscard]] dx::VmObjectRef MakeActivityInfoArray(dx::IntrinsicContext& call, const Context& context,
                                                    const dx::VmObjectRef application_info,
                                                    const bool include_disabled) {
    std::vector<std::size_t> selected;
    selected.reserve(context->activity_components.size());
    for (std::size_t index = 0; index < context->activity_components.size(); ++index) {
        if (context->activity_components[index].enabled || include_disabled) selected.push_back(index);
    }
    const auto array_class = call.vm.Linker().ResolveDescriptor("[Landroid/content/pm/ActivityInfo;");
    const auto element_class = call.vm.Linker().ResolveDescriptor("Landroid/content/pm/ActivityInfo;");
    const auto array =
        call.vm.Model().NewObjectArray(array_class, element_class, static_cast<JniSize>(selected.size()));
    const auto roots = call.vm.ProtectReferences(std::array{array, application_info});
    JniSize slot{};
    for (const auto index : selected) {
        call.vm.Model().SetObjectElement(
            array, slot++, MakeActivityInfo(call, context, context->activity_components[index], application_info));
    }
    return array;
}

// Both queries materialize the same sealed Manifest facts, with distinct
// ApplicationInfo installation semantics. Never retain mutable guest results.
[[nodiscard]] dx::VmObjectRef MakePackageInfo(dx::IntrinsicContext& call, const Context& context,
                                              const std::int32_t flags, const PackageInfoKind kind) {
    const auto info = call.vm.NewIntrinsicInstance("Landroid/content/pm/PackageInfo;");
    const auto info_root = call.vm.ProtectReferences(std::array{info});
    SetRef(call, info, "packageName", "Ljava/lang/String;", String(call, context->package_name));
    SetInt(call, info, "versionCode", static_cast<std::int32_t>(context->package_version_code));
    SetRef(call, info, "versionName", "Ljava/lang/String;", String(call, context->package_version_name));
    const auto application = MakeApplicationInfo(call, context, (flags & kGetMetaData) != 0, kind);
    const auto roots = call.vm.ProtectReferences(std::array{application});
    SetRef(call, info, "applicationInfo", "Landroid/content/pm/ApplicationInfo;", application);
    if ((flags & kGetPermissions) != 0) {
        SetRef(call, info, "requestedPermissions", "[Ljava/lang/String;",
               MakeStringArray(call, context->requested_permissions));
    }
    if ((flags & kGetActivities) != 0 && !context->activity_components.empty()) {
        SetRef(call, info, "activities", "[Landroid/content/pm/ActivityInfo;",
               MakeActivityInfoArray(call, context, application, (flags & kGetDisabledComponents) != 0));
    }
    return info;
}

dx::VmValue InstalledApplications(dx::IntrinsicContext& call, const Context& context) {
    const auto flags = call.arguments[0].AsInt();
    if (!context->package_inventory_known || context->package_name.empty() || context->package_resource_path.empty() ||
        context->package_resource_path.front() != '/' || (flags & ~(kGetMetaData | kGetUninstalledPackages)) != 0) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.installed_applications", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "getInstalledApplications requires the sealed single-APK "
                              "installed inventory and flags 0, GET_META_DATA or GET_UNINSTALLED_PACKAGES"};
    }
    const auto list = NewQueryObject(call, "Ljava/util/ArrayList;");
    const auto list_root = call.vm.ProtectReferences(std::array{list});
    // AOSP lists installed applications even when enabled=false.
    // Each query gets a fresh value; Context's stable object is separate.
    const auto application = MakeApplicationInfo(call, context, (flags & kGetMetaData) != 0);
    const auto application_root = call.vm.ProtectReferences(std::array{application});
    static_cast<void>(
        CallAndroidMethod(call.vm, list, "add", "(Ljava/lang/Object;)Z", {dx::VmValue::Ref(application)}));
    return dx::VmValue::Ref(list);
}

dx::VmValue PackageActivityInfo(dx::IntrinsicContext& call, const Context& context, const dx::VmObjectRef component,
                                const std::int32_t flags) {
    RequireComponentInfoQuery(call, context->activity_inventory_known, flags, "dexvm.activity_info", "getActivityInfo");
    const auto [package, name] = ReadComponentIdentity(call, context, component);
    const auto* found = loader::FindAndroidManifestActivityComponent(context->activity_components, name);
    if (found == nullptr || (!(flags & kGetDisabledComponents) && (!context->application_enabled || !found->enabled)))
        throw dx::VmJavaThrow{"Landroid/content/pm/PackageManager$NameNotFoundException;", package + "/" + name};
    const auto application = MakeApplicationInfo(call, context, (flags & kGetMetaData) != 0);
    const auto root = call.vm.ProtectReferences(std::array{application});
    const auto info = MakeActivityInfo(call, context, *found, application);
    SetComponentMetaData(call, context, info, found->meta_data, (flags & kGetMetaData) != 0);
    return dx::VmValue::Ref(info);
}

dx::VmValue InstallerPackageName(dx::IntrinsicContext& call, const Context& context) {
    const auto ref = call.arguments[0].ref;
    const auto package = ref.IsValid() ? call.vm.StringUtf8(ref) : "null";
    if (!ref.IsValid() || package.empty() || package != context->package_name)
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Unknown package: " + package};
    return dx::VmValue::Ref(dx::VmObjectRef{});
}

dx::VmValue PackageCheckPermission(dx::IntrinsicContext& call, const Context& context) {
    const auto permission = RequiredString(call, 0U, "permissionName");
    const auto package = RequiredString(call, 1U, "packageName");
    return dx::VmValue::Int(package == context->package_name && context->granted_permissions.contains(permission)
                                ? kPermissionGranted
                                : kPermissionDenied);
}

dx::VmValue PackageHasSystemFeature(dx::IntrinsicContext& call, const Context& context) {
    return dx::VmValue::Int(context->system_features.contains(RequiredString(call, 0U, "name")));
}

dx::VmValue QueryReceiverInfo(dx::IntrinsicContext& call, const Context& context, const dx::VmObjectRef component,
                              const std::int32_t flags) {
    RequireComponentInfoQuery(call, context->receiver_inventory_known, flags, "dexvm.receiver_info", "getReceiverInfo");
    const auto [package_name, class_name] = ReadComponentIdentity(call, context, component);
    const auto found = std::find_if(context->receiver_components.rbegin(), context->receiver_components.rend(),
                                    [&](const auto& receiver) { return receiver.name == class_name; });
    if (found == context->receiver_components.rend() ||
        (!(flags & kGetDisabledComponents) && (!context->application_enabled || !found->enabled))) {
        throw dx::VmJavaThrow{"Landroid/content/pm/PackageManager$NameNotFoundException;",
                              package_name + "/" + class_name};
    }
    const auto application = MakeApplicationInfo(call, context, (flags & kGetMetaData) != 0);
    const auto root = call.vm.ProtectReferences(std::array{application});
    return dx::VmValue::Ref(MakeReceiverInfo(call, context, *found, (flags & kGetMetaData) != 0, application));
}

dx::VmValue QueryServiceInfo(dx::IntrinsicContext& call, const Context& context, const dx::VmObjectRef component,
                             const std::int32_t flags) {
    RequireComponentInfoQuery(call, context->service_inventory_known, flags, "dexvm.service_info", "getServiceInfo");
    const auto [package_name, class_name] = ReadComponentIdentity(call, context, component);
    const auto found = std::find_if(context->service_components.rbegin(), context->service_components.rend(),
                                    [&](const auto& service) { return service.name == class_name; });
    if (found == context->service_components.rend() ||
        (!(flags & kGetDisabledComponents) && (!context->application_enabled || !found->enabled))) {
        throw dx::VmJavaThrow{"Landroid/content/pm/PackageManager$NameNotFoundException;",
                              package_name + "/" + class_name};
    }
    const auto application = MakeApplicationInfo(call, context, (flags & kGetMetaData) != 0);
    const auto root = call.vm.ProtectReferences(std::array{application});
    return dx::VmValue::Ref(MakeServiceInfo(call, context, *found, (flags & kGetMetaData) != 0, application));
}

[[noreturn]] void UnsupportedPackageIntentQuery(dx::IntrinsicContext& call, const std::string_view capability,
                                                const char* reason) {
    if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented(capability, 0);
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", reason};
}

using ComponentInfoQuery = dx::VmValue (*)(dx::IntrinsicContext&, const Context&, dx::VmObjectRef, std::int32_t);

// A disengaged result leaves the Intent to the caller's implicit query path.
// An engaged null means the explicit component is absent or disabled.
[[nodiscard]] std::optional<dx::VmObjectRef> QueryExplicitComponent(dx::IntrinsicContext& call, const Context& context,
                                                                    const dx::VmObjectRef intent,
                                                                    const std::int32_t flags,
                                                                    const ComponentInfoQuery query_info,
                                                                    const std::string_view capability) {
    constexpr auto component_type = "()Landroid/content/ComponentName;";
    auto component = CallAndroidMethod(call.vm, intent, "getComponent", component_type).ref;
    if (!component.IsValid()) {
        const auto selector = CallAndroidMethod(call.vm, intent, "getSelector", "()Landroid/content/Intent;").ref;
        if (selector.IsValid()) component = CallAndroidMethod(call.vm, selector, "getComponent", component_type).ref;
    }
    if (!component.IsValid()) return std::nullopt;
    const auto component_root = call.vm.ProtectReferences(std::array{component});
    const auto package = CallAndroidMethod(call.vm, component, "getPackageName", "()Ljava/lang/String;").ref;
    if (package.IsValid() && call.vm.StringUtf8(package) != context->package_name)
        UnsupportedPackageIntentQuery(call, capability, "external package inventory is unavailable");

    dx::VmObjectRef info;
    try {
        info = query_info(call, context, component, flags & ~kMatchDefaultOnly).ref;
    } catch (const dx::VmJavaThrow& error) {
        if (error.existing.IsValid() || error.descriptor != "Landroid/content/pm/PackageManager$NameNotFoundException;")
            throw;
        return dx::VmObjectRef{};
    }
    const auto info_root = call.vm.ProtectReferences(std::array{info});
    const auto result = NewQueryObject(call, "Landroid/content/pm/ResolveInfo;");
    if (query_info == QueryServiceInfo)
        SetRef(call, result, "serviceInfo", "Landroid/content/pm/ServiceInfo;", info);
    else
        SetRef(call, result, "activityInfo", "Landroid/content/pm/ActivityInfo;", info);
    return result;
}

[[nodiscard]] dx::VmValue MakeComponentQueryList(dx::IntrinsicContext& call, const dx::VmObjectRef result) {
    const auto result_root = call.vm.ProtectReferences(std::array{result});
    const auto list = NewQueryObject(call, "Ljava/util/ArrayList;");
    const auto list_root = call.vm.ProtectReferences(std::array{list});
    if (result.IsValid())
        static_cast<void>(CallAndroidMethod(call.vm, list, "add", "(Ljava/lang/Object;)Z", {dx::VmValue::Ref(result)}));
    return dx::VmValue::Ref(list);
}

dx::VmValue QueryPermissionInfo(dx::IntrinsicContext& call, const Context& context) {
    const auto name = RequiredString(call, 0U, "permissionName");
    const auto flags = call.arguments[1].AsInt();
    if (!context->permission_inventory_known || (flags & ~kGetMetaData) != 0) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.permission_info", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "getPermissionInfo requires current-package definitions and flags 0 or GET_META_DATA"};
    }
    const auto found =
        std::find_if(context->defined_permissions.begin(), context->defined_permissions.end(),
                     [&](const auto& definition) { return definition.registered && definition.name == name; });
    if (found == context->defined_permissions.end())
        throw dx::VmJavaThrow{"Landroid/content/pm/PackageManager$NameNotFoundException;", name};
    return dx::VmValue::Ref(MakePermissionInfo(call, context, *found, (flags & kGetMetaData) != 0));
}

dx::VmObjectRef MakeProviderInfo(dx::IntrinsicContext& call, const Context& context,
                                 const loader::AndroidManifestProviderComponent& provider,
                                 dx::VmObjectRef application_info, bool include_meta) {
    const auto info = NewQueryObject(call, "Landroid/content/pm/ProviderInfo;");
    const auto info_root = call.vm.ProtectReferences(std::array{info});
    PopulateComponentInfo(call, context, info, provider.name, provider.enabled,
                          provider.exported.value_or(context->target_sdk_version < 17), provider.process_name,
                          application_info);
    SetRef(call, info, "authority", "Ljava/lang/String;", String(call, provider.authority));
    if (provider.read_permission)
        SetRef(call, info, "readPermission", "Ljava/lang/String;", String(call, *provider.read_permission));
    if (provider.write_permission)
        SetRef(call, info, "writePermission", "Ljava/lang/String;", String(call, *provider.write_permission));
    SetInt(call, info, "initOrder", provider.init_order);
    SetBoolean(call, info, "grantUriPermissions", provider.grant_uri_permissions);
    SetComponentMetaData(call, context, info, provider.meta_data, include_meta);
    return info;
}

bool HasProviderAuthority(std::string_view names, std::string_view authority) {
    for (const auto name : names | std::views::split(';'))
        if (std::ranges::equal(name, authority)) return true;
    return false;
}

}  // namespace

Decl Declare_local_PackageService_Class(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalPackageService;",
                                                    "Landroid/content/pm/IPackageManager$Stub;");
    builder.VirtualMethod(
        "getPackageInfo", "(Ljava/lang/String;II)Landroid/content/pm/PackageInfo;",
        [context](dx::IntrinsicContext& call) {
            if (call.arguments[2].AsInt() != 0)
                throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "package query outside the current user"};
            const auto package = RequiredString(call, 0U, "packageName");
            if (package != context->package_name) return dx::VmValue::Ref(dx::VmObjectRef{});
            const auto flags = call.arguments[1].AsInt();
            RequireFlags(call, flags, kGetActivities | kGetMetaData | kGetPermissions | kGetUninstalledPackages,
                         "getPackageInfo", "dexvm.package_info_activities");
            return dx::VmValue::Ref(MakePackageInfo(call, context, flags, PackageInfoKind::installed));
        });
    builder.VirtualMethod("getPackagesForUid", "(I)[Ljava/lang/String;", [context](dx::IntrinsicContext& call) {
        if (static_cast<std::uint32_t>(call.arguments[0].AsInt()) != context->application_uid)
            return dx::VmValue::Ref(dx::VmObjectRef{});
        return dx::VmValue::Ref(MakeStringArray(call, {context->package_name}));
    });
    builder.VirtualMethod("getApplicationInfo", "(Ljava/lang/String;II)Landroid/content/pm/ApplicationInfo;",
                          [context](dx::IntrinsicContext& call) {
                              if (call.arguments[2].AsInt() != 0)
                                  throw dx::VmJavaThrow{"Ljava/lang/SecurityException;",
                                                        "package query outside the current user"};
                              if (RequiredString(call, 0U, "packageName") != context->package_name)
                                  return dx::VmValue::Ref(dx::VmObjectRef{});
                              const auto flags = call.arguments[1].AsInt();
                              RequireFlags(call, flags, kGetMetaData | kGetUninstalledPackages, "getApplicationInfo",
                                           "dexvm.installed_applications");
                              return dx::VmValue::Ref(MakeApplicationInfo(call, context, (flags & kGetMetaData) != 0));
                          });
    builder.VirtualMethod("getInstallerPackageName", "(Ljava/lang/String;)Ljava/lang/String;",
                          [context](dx::IntrinsicContext& call) { return InstallerPackageName(call, context); });
    builder.VirtualMethod("checkPermission", "(Ljava/lang/String;Ljava/lang/String;)I",
                          [context](dx::IntrinsicContext& call) { return PackageCheckPermission(call, context); });
    builder.VirtualMethod("hasSystemFeature", "(Ljava/lang/String;)Z",
                          [context](dx::IntrinsicContext& call) { return PackageHasSystemFeature(call, context); });
    builder.VirtualMethod(
        "getInstalledApplications", "(II)Landroid/content/pm/ParceledListSlice;",
        [context](dx::IntrinsicContext& call) {
            if (call.arguments[1].AsInt() != 0)
                throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "package query outside the current user"};
            const auto applications = InstalledApplications(call, context);
            return dx::VmValue::Ref(NewPlatformObject(call.vm, "Landroid/content/pm/ParceledListSlice;",
                                                      "(Ljava/util/List;)V", {applications}));
        });
    builder.VirtualMethod("getActivityInfo", "(Landroid/content/ComponentName;II)Landroid/content/pm/ActivityInfo;",
                          [context](dx::IntrinsicContext& call) {
                              if (call.arguments[2].AsInt() != 0)
                                  throw dx::VmJavaThrow{"Ljava/lang/SecurityException;",
                                                        "package query outside the current user"};
                              return PackageActivityInfo(call, context,
                                                         dx::IntrinsicCall(call).NonNullRef(0, "component"),
                                                         call.arguments[1].AsInt());
                          });
    builder.VirtualMethod("getReceiverInfo", "(Landroid/content/ComponentName;II)Landroid/content/pm/ActivityInfo;",
                          [context](dx::IntrinsicContext& call) {
                              if (call.arguments[2].AsInt() != 0)
                                  throw dx::VmJavaThrow{"Ljava/lang/SecurityException;",
                                                        "package query outside the current user"};
                              return QueryReceiverInfo(call, context,
                                                       dx::IntrinsicCall(call).NonNullRef(0, "component"),
                                                       call.arguments[1].AsInt());
                          });
    builder.VirtualMethod("getServiceInfo", "(Landroid/content/ComponentName;II)Landroid/content/pm/ServiceInfo;",
                          [context](dx::IntrinsicContext& call) {
                              if (call.arguments[2].AsInt() != 0)
                                  throw dx::VmJavaThrow{"Ljava/lang/SecurityException;",
                                                        "package query outside the current user"};
                              return QueryServiceInfo(call, context, dx::IntrinsicCall(call).NonNullRef(0, "component"),
                                                      call.arguments[1].AsInt());
                          });
    builder.VirtualMethod("getPermissionInfo", "(Ljava/lang/String;I)Landroid/content/pm/PermissionInfo;",
                          [context](dx::IntrinsicContext& call) { return QueryPermissionInfo(call, context); });
    builder.VirtualMethod(
        "queryIntentActivities", "(Landroid/content/Intent;Ljava/lang/String;II)Ljava/util/List;",
        [context](dx::IntrinsicContext& call) {
            if (call.arguments[3].AsInt() != 0 || call.arguments[1].ref.IsValid())
                UnsupportedPackageIntentQuery(call, "dexvm.activity_query",
                                              "activity query requires current user and no resolved type");
            const auto intent = dx::IntrinsicCall(call).NonNullRef(0, "intent");
            const auto result = QueryExplicitComponent(call, context, intent, call.arguments[2].AsInt(),
                                                       PackageActivityInfo, "dexvm.activity_query");
            if (!result)
                UnsupportedPackageIntentQuery(call, "dexvm.activity_query",
                                              "queryIntentActivities requires an explicit component");
            return MakeComponentQueryList(call, *result);
        });
    builder.VirtualMethod(
        "queryIntentReceivers", "(Landroid/content/Intent;Ljava/lang/String;II)Ljava/util/List;",
        [context](dx::IntrinsicContext& call) {
            if (call.arguments[3].AsInt() != 0 || call.arguments[1].ref.IsValid())
                UnsupportedLocalService(call.vm, "receiver query requires current user and no resolved type");
            const auto intent = dx::IntrinsicCall(call).NonNullRef(0, "intent");
            const auto flags = call.arguments[2].AsInt();
            if (const auto result =
                    QueryExplicitComponent(call, context, intent, flags, QueryReceiverInfo, "dexvm.receiver_query"))
                return MakeComponentQueryList(call, *result);
            return dx::VmValue::Ref(QueryBroadcastReceivers(call, context, intent, flags));
        });
    builder.VirtualMethod(
        "resolveContentProvider", "(Ljava/lang/String;II)Landroid/content/pm/ProviderInfo;",
        [context](dx::IntrinsicContext& call) {
            if (call.arguments[2].AsInt() != 0)
                throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "provider query outside the current user"};
            const auto authority = RequiredString(call, 0U, "authority");
            const auto flags = call.arguments[1].AsInt();
            RequireFlags(call, flags, kGetMetaData | kGetDisabledComponents, "resolveContentProvider",
                         "dexvm.provider_info");
            for (const auto& provider : context->provider_components) {
                if (!HasProviderAuthority(provider.authority, authority)) continue;
                if ((!context->application_enabled || !provider.enabled) && !(flags & kGetDisabledComponents)) continue;
                const auto meta = (flags & kGetMetaData) != 0;
                const auto application = MakeApplicationInfo(call, context, meta);
                const auto root = call.vm.ProtectReferences(std::array{application});
                return dx::VmValue::Ref(MakeProviderInfo(call, context, provider, application, meta));
            }
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    const auto query_service = [context](dx::IntrinsicContext& call) -> dx::VmObjectRef {
        const auto intent = dx::IntrinsicCall(call).NonNullRef(0, "intent");
        const auto flags = call.arguments[2].AsInt();
        if (call.arguments[3].AsInt() != 0 || call.arguments[1].ref.IsValid())
            UnsupportedLocalService(call.vm,
                                    "local service query requires current user, no resolved type and supported flags");
        if (const auto result =
                QueryExplicitComponent(call, context, intent, flags, QueryServiceInfo, "dexvm.service_resolution"))
            return *result;
        if ((flags & ~0x40) != 0)
            UnsupportedLocalService(call.vm,
                                    "local service query requires current user, no resolved type and supported flags");
        RequireAbsentService(call, context, intent);
        return dx::VmObjectRef{};
    };
    builder.VirtualMethod(
        "queryIntentServices", "(Landroid/content/Intent;Ljava/lang/String;II)Ljava/util/List;",
        [query_service](dx::IntrinsicContext& call) { return MakeComponentQueryList(call, query_service(call)); });
    builder.VirtualMethod(
        "resolveService", "(Landroid/content/Intent;Ljava/lang/String;II)Landroid/content/pm/ResolveInfo;",
        [query_service](dx::IntrinsicContext& call) { return dx::VmValue::Ref(query_service(call)); });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
namespace {
using RomService = DexVmAndroidContext::RomServiceRecord;

dx::VmObjectRef ServiceIntentCopy(dx::Interpreter& vm, dx::VmObjectRef intent) {
    return NewPlatformObject(vm, "Landroid/content/Intent;", "(Landroid/content/Intent;)V", {dx::VmValue::Ref(intent)});
}

RomService& EnsureRomService(dx::IntrinsicContext& call, const Context& context, dx::VmObjectRef thread,
                             dx::VmObjectRef intent, const loader::AndroidManifestServiceComponent& declaration) {
    if (const auto found = context->rom_services.find(declaration.name); found != context->rom_services.end())
        return found->second;
    auto& vm = call.vm;
    const auto token = NewPlatformObject(vm, "Landroid/os/Binder;", "()V", {});
    const auto token_root = vm.ProtectReferences(std::array{token});
    const auto app = MakeApplicationInfo(call, context, true);
    const auto app_root = vm.ProtectReferences(std::array{app});
    const auto info = MakeServiceInfo(call, context, declaration, true, app);
    const auto info_root = vm.ProtectReferences(std::array{info});
    constexpr auto compat_type = "Landroid/content/res/CompatibilityInfo;";
    const auto type = vm.Linker().ResolveDescriptor(compat_type);
    const auto field = vm.Linker().FindFieldRecursive(type, "DEFAULT_COMPATIBILITY_INFO", compat_type);
    if (!field) throw std::logic_error("CompatibilityInfo default is unavailable");
    const auto compat = dx::VmObjectRef{vm.Linker().Class(type).static_storage[vm.Linker().Field(*field).slot]};
    const auto component = CallAndroidMethod(vm, intent, "getComponent", "()Landroid/content/ComponentName;").ref;
    const auto [entry, inserted] =
        context->rom_services.try_emplace(declaration.name, RomService{thread, token, component});
    try {
        static_cast<void>(CallAndroidMethod(
            vm, thread, "scheduleCreateService",
            "(Landroid/os/IBinder;Landroid/content/pm/ServiceInfo;Landroid/content/res/CompatibilityInfo;I)V",
            {dx::VmValue::Ref(token), dx::VmValue::Ref(info), dx::VmValue::Ref(compat), dx::VmValue::Int(2)}));
    } catch (...) {
        if (inserted) context->rom_services.erase(entry);
        throw;
    }
    return entry->second;
}

void RetireRomService(dx::Interpreter& vm, DexVmAndroidContext& context,
                      std::map<std::string, RomService, std::less<>>::iterator service) {
    if (service->second.started || !service->second.connections.empty()) return;
    const auto& record = service->second;
    static_cast<void>(CallAndroidMethod(vm, record.thread, "scheduleStopService", "(Landroid/os/IBinder;)V",
                                        {dx::VmValue::Ref(record.token)}));
    context.rom_services.erase(service);
}

void UnbindRomService(dx::Interpreter& vm, RomService& service) {
    if (!service.bound) return;
    static_cast<void>(CallAndroidMethod(vm, service.thread, "scheduleUnbindService",
                                        "(Landroid/os/IBinder;Landroid/content/Intent;)V",
                                        {dx::VmValue::Ref(service.token), dx::VmValue::Ref(service.bind_intent)}));
    service.bound = false;
}

void PublishRomServiceConnection(dx::Interpreter& vm, const RomService& service, dx::VmObjectRef connection) {
    if (!service.binder.IsValid()) return;
    static_cast<void>(CallAndroidMethod(vm, connection, "connected",
                                        "(Landroid/content/ComponentName;Landroid/os/IBinder;)V",
                                        {dx::VmValue::Ref(service.component), dx::VmValue::Ref(service.binder)}));
}

void RequireRomServiceRequest(dx::IntrinsicContext& call, dx::VmObjectRef type, std::int32_t user) {
    if (user != 0 || type.IsValid())
        UnsupportedLocalService(call.vm, "service request requires current user and no resolved type");
}
}  // namespace

dx::VmObjectRef ApplicationProviders(dx::IntrinsicContext& call, const Context& context,
                                     dx::VmObjectRef application_info) {
    if (!context->application_enabled) return dx::VmObjectRef{};
    const auto& process =
        context->application_process_name.empty() ? context->package_name : context->application_process_name;
    std::vector<const loader::AndroidManifestProviderComponent*> declarations;
    for (const auto& provider : context->provider_components) {
        if (!provider.enabled) continue;
        if (!provider.process_name.empty() && provider.process_name != process) {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.provider.secondary_process", 0);
            continue;
        }
        declarations.push_back(&provider);
    }
    if (declarations.empty()) return dx::VmObjectRef{};
    std::stable_sort(declarations.begin(), declarations.end(),
                     [](const auto* a, const auto* b) { return a->init_order > b->init_order; });
    auto& vm = call.vm;
    const auto list = NewQueryObject(call, "Ljava/util/ArrayList;");
    const auto roots = vm.ProtectReferences(std::array{list, application_info});
    for (const auto* provider : declarations) {
        const auto info = MakeProviderInfo(call, context, *provider, application_info, true);
        static_cast<void>(CallAndroidMethod(vm, list, "add", "(Ljava/lang/Object;)Z", {dx::VmValue::Ref(info)}));
    }
    return list;
}

void AddLocalProviderMethods(dx::IntrinsicClassBuilder& builder, const Context& context) {
    builder.VirtualMethod(
        "getContentProvider",
        "(Landroid/app/IApplicationThread;Ljava/lang/String;IZ)Landroid/app/IActivityManager$ContentProviderHolder;",
        [context](dx::IntrinsicContext& call) {
            if (call.arguments[2].AsInt() != 0)
                throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "provider query outside the current user"};
            const auto authority = RequiredString(call, 1U, "authority");
            const auto found = context->local_provider_holders.find(authority);
            if (found != context->local_provider_holders.end()) return dx::VmValue::Ref(found->second);
            if (authority == "settings") {
                auto& vm = call.vm;
                const auto info = NewQueryObject(call, "Landroid/content/pm/ProviderInfo;");
                const auto info_root = vm.ProtectReferences(std::array{info});
                const auto app = NewQueryObject(call, "Landroid/content/pm/ApplicationInfo;");
                SetInt(call, app, "uid", static_cast<std::int32_t>(context->application_uid));
                SetRef(call, info, "applicationInfo", "Landroid/content/pm/ApplicationInfo;", app);
                SetRef(call, info, "authority", "Ljava/lang/String;", call.arguments[1].ref);
                SetRef(call, info, "name", "Ljava/lang/String;",
                       String(call, "org.ogplay.framework.LocalSettingsProvider"));
                const auto holder = NewPlatformObject(vm, "Landroid/app/IActivityManager$ContentProviderHolder;",
                                                      "(Landroid/content/pm/ProviderInfo;)V", {dx::VmValue::Ref(info)});
                const auto holder_root = vm.ProtectReferences(std::array{holder});
                const auto provider = NewPlatformObject(vm, "Lorg/ogplay/framework/LocalSettingsProvider;", "()V", {});
                SetRef(call, holder, "provider", "Landroid/content/IContentProvider;", provider);
                SetBoolean(call, holder, "noReleaseNeeded", true);
                return dx::VmValue::Ref(context->local_provider_holders.emplace(authority, holder).first->second);
            }
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.VirtualMethod(
        "publishContentProviders", "(Landroid/app/IApplicationThread;Ljava/util/List;)V",
        [context](dx::IntrinsicContext& call) {
            auto& vm = call.vm;
            const auto list = dx::IntrinsicCall(call).NonNullRef(1, "providers");
            const auto count = CallAndroidMethod(vm, list, "size", "()I").AsInt();
            for (int i = 0; i < count; ++i) {
                const auto holder =
                    CallAndroidMethod(vm, list, "get", "(I)Ljava/lang/Object;", {dx::VmValue::Int(i)}).ref;
                const auto info = dx::VmObjectRef{
                    vm.Model()
                        .InstanceSlots(holder)[Field(call, holder, "info", "Landroid/content/pm/ProviderInfo;").slot]
                        .bits};
                const auto authority = dx::VmObjectRef{
                    vm.Model().InstanceSlots(info)[Field(call, info, "authority", "Ljava/lang/String;").slot].bits};
                const auto names = vm.StringUtf8(authority);
                std::size_t start = 0;
                for (;;) {
                    const auto end = names.find(';', start);
                    const auto name = names.substr(start, end == std::string::npos ? end : end - start);
                    if (!name.empty()) context->local_provider_holders.try_emplace(name, holder);
                    if (end == std::string::npos) break;
                    start = end + 1;
                }
            }
            return dx::VmValue::Void();
        });
    RejectUnsupportedServiceMethod(builder, "android.app.IActivityManager", "getProviderMimeType",
                                   "(Landroid/net/Uri;I)Ljava/lang/String;");
}

namespace {
constexpr auto kBatteryChanged = "android.intent.action.BATTERY_CHANGED";

bool MatchesLocalBroadcast(dx::Interpreter& vm, dx::VmObjectRef filter, dx::VmObjectRef intent, dx::VmValue type) {
    const auto action = CallAndroidMethod(vm, intent, "getAction", "()Ljava/lang/String;");
    const auto scheme = CallAndroidMethod(vm, intent, "getScheme", "()Ljava/lang/String;");
    const auto data = CallAndroidMethod(vm, intent, "getData", "()Landroid/net/Uri;");
    const auto categories = CallAndroidMethod(vm, intent, "getCategories", "()Ljava/util/Set;");
    const auto tag = vm.NewStringUtf8("ActivityManager");
    const auto roots = vm.ProtectReferences(std::array{action.ref, scheme.ref, data.ref, categories.ref, tag});
    return CallAndroidMethod(
               vm, filter, "match",
               "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Landroid/net/Uri;Ljava/util/Set;Ljava/lang/String;)I",
               {action, type, scheme, data, categories, dx::VmValue::Ref(tag)})
               .AsInt() >= 0;
}

void DispatchLocalBroadcast(dx::Interpreter& vm, dx::VmObjectRef receiver, dx::VmObjectRef intent, dx::VmValue code,
                            dx::VmValue data, dx::VmValue extras, bool initial_sticky) {
    const auto copy =
        NewPlatformObject(vm, "Landroid/content/Intent;", "(Landroid/content/Intent;)V", {dx::VmValue::Ref(intent)});
    static_cast<void>(CallAndroidMethod(vm, receiver, "performReceive",
                                        "(Landroid/content/Intent;ILjava/lang/String;Landroid/os/Bundle;ZZI)V",
                                        {dx::VmValue::Ref(copy), code, data, extras, dx::VmValue::Int(0),
                                         dx::VmValue::Int(initial_sticky), dx::VmValue::Int(0)}));
}

std::string LocalStickyAction(dx::Interpreter& vm, const Context& context, dx::VmObjectRef intent) {
    if (!context->granted_permissions.contains("android.permission.BROADCAST_STICKY"))
        throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "BROADCAST_STICKY permission is required"};
    const auto action = CallAndroidMethod(vm, intent, "getAction", "()Ljava/lang/String;").ref;
    if (!action.IsValid() || CallAndroidMethod(vm, intent, "getData", "()Landroid/net/Uri;").ref.IsValid() ||
        CallAndroidMethod(vm, intent, "getType", "()Ljava/lang/String;").ref.IsValid() ||
        CallAndroidMethod(vm, intent, "getCategories", "()Ljava/util/Set;").ref.IsValid() ||
        CallAndroidMethod(vm, intent, "getPackage", "()Ljava/lang/String;").ref.IsValid() ||
        CallAndroidMethod(vm, intent, "getComponent", "()Landroid/content/ComponentName;").ref.IsValid())
        UnsupportedLocalService(vm, "sticky broadcasts require an untargeted action");
    const auto name = vm.StringUtf8(action);
    if (name == kBatteryChanged)
        throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "battery broadcasts are platform-owned"};
    return name;
}

void EnsureLocalBatteryBroadcast(dx::Interpreter& vm, const Context& context) {
    if (context->local_sticky_broadcasts.contains(kBatteryChanged)) return;
    const auto action = vm.NewStringUtf8(kBatteryChanged);
    const auto battery =
        NewPlatformObject(vm, "Landroid/content/Intent;", "(Ljava/lang/String;)V", {dx::VmValue::Ref(action)});
    const auto root = vm.ProtectReferences(std::array{battery});
    static_cast<void>(CallAndroidMethod(vm, battery, "putExtra", "(Ljava/lang/String;Z)Landroid/content/Intent;",
                                        {dx::VmValue::Ref(vm.NewStringUtf8("present")), dx::VmValue::Int(0)}));
    context->local_sticky_broadcasts.try_emplace(kBatteryChanged, battery);
}
}  // namespace

void AddLocalActivityBindingMethods(dx::IntrinsicClassBuilder& builder, const Context& context) {
    builder.VirtualMethod(
        "setServiceForeground", "(Landroid/content/ComponentName;Landroid/os/IBinder;ILandroid/app/Notification;Z)V",
        [context](dx::IntrinsicContext& call) {
            const auto found = std::ranges::find_if(
                context->rom_services, [&](const auto& entry) { return entry.second.token == call.arguments[1].ref; });
            if (found == context->rom_services.end()) return dx::VmValue::Void();
            const auto component = found->second.component;
            if (CallAndroidMethod(call.vm, component, "equals", "(Ljava/lang/Object;)Z", {call.arguments[0]}).AsInt() ==
                0)
                return dx::VmValue::Void();
            if (call.arguments[2].AsInt() != 0 && !call.arguments[3].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "null notification"};
            RecordUiDegraded(call.vm, "Service foreground notification display");
            return dx::VmValue::Void();
        });
    builder.VirtualMethod(
        "registerReceiver",
        "(Landroid/app/IApplicationThread;Ljava/lang/String;Landroid/content/IIntentReceiver;"
        "Landroid/content/IntentFilter;Ljava/lang/String;I)Landroid/content/Intent;",
        [context](dx::IntrinsicContext& call) {
            if (call.arguments[5].AsInt() != 0 || call.arguments[4].ref.IsValid())
                UnsupportedLocalService(call.vm, "receiver registration requires current user and no permission");
            const auto receiver = call.arguments[2].ref;
            const auto filter = dx::IntrinsicCall(call).NonNullRef(3, "filter");
            if (receiver.IsValid()) {
                const auto copy = NewPlatformObject(call.vm, "Landroid/content/IntentFilter;",
                                                    "(Landroid/content/IntentFilter;)V", {dx::VmValue::Ref(filter)});
                context->local_broadcast_filters.emplace_back(receiver, copy);
            }
            auto& vm = call.vm;
            EnsureLocalBatteryBroadcast(vm, context);
            std::vector<dx::VmObjectRef> sticky;
            for (const auto& [_, intent] : context->local_sticky_broadcasts)
                sticky.push_back(intent);
            const auto roots = vm.ProtectReferences(sticky);
            const auto null = dx::VmValue::Ref(dx::VmObjectRef{});
            dx::VmObjectRef first;
            for (const auto intent : sticky) {
                if (!MatchesLocalBroadcast(vm, filter, intent, null)) continue;
                if (!first.IsValid()) first = intent;
                if (receiver.IsValid())
                    DispatchLocalBroadcast(vm, receiver, intent, dx::VmValue::Int(0), null, null, true);
            }
            if (!first.IsValid()) return null;
            return dx::VmValue::Ref(NewPlatformObject(vm, "Landroid/content/Intent;", "(Landroid/content/Intent;)V",
                                                      {dx::VmValue::Ref(first)}));
        });
    builder.VirtualMethod("unregisterReceiver", "(Landroid/content/IIntentReceiver;)V",
                          [context](dx::IntrinsicContext& call) {
                              std::erase_if(context->local_broadcast_filters,
                                            [&](const auto& item) { return item.first == call.arguments[0].ref; });
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod(
        "broadcastIntent",
        "(Landroid/app/IApplicationThread;Landroid/content/Intent;Ljava/lang/String;Landroid/content/IIntentReceiver;"
        "ILjava/lang/String;Landroid/os/Bundle;Ljava/lang/String;IZZI)I",
        [context](dx::IntrinsicContext& call) {
            if (call.arguments[11].AsInt() != 0 || call.arguments[3].ref.IsValid() || call.arguments[7].ref.IsValid() ||
                call.arguments[8].AsInt() != -1 || call.arguments[9].AsInt())
                UnsupportedLocalService(call.vm,
                                        "only unordered process-local broadcasts without a permission are supported");
            auto& vm = call.vm;
            const auto intent = dx::IntrinsicCall(call).NonNullRef(1, "intent");
            const auto package = CallAndroidMethod(vm, intent, "getPackage", "()Ljava/lang/String;").ref;
            if (package.IsValid() && vm.StringUtf8(package) != context->package_name) return dx::VmValue::Int(0);
            if (CallAndroidMethod(vm, intent, "getComponent", "()Landroid/content/ComponentName;").ref.IsValid())
                UnsupportedLocalService(vm, "explicit broadcasts are unsupported");
            if (call.arguments[10].AsInt()) {
                const auto key = LocalStickyAction(vm, context, intent);
                const auto copy = NewPlatformObject(vm, "Landroid/content/Intent;", "(Landroid/content/Intent;)V",
                                                    {dx::VmValue::Ref(intent)});
                context->local_sticky_broadcasts.insert_or_assign(key, copy);
            }
            std::vector<dx::VmObjectRef> targets;
            const auto registrations = context->local_broadcast_filters;
            std::vector<dx::VmObjectRef> refs;
            for (const auto& [receiver, filter] : registrations) {
                refs.push_back(receiver);
                refs.push_back(filter);
            }
            const auto roots = vm.ProtectReferences(refs);
            for (const auto& [receiver, filter] : registrations) {
                if (std::ranges::find(targets, receiver) != targets.end()) continue;
                if (MatchesLocalBroadcast(vm, filter, intent, call.arguments[2])) targets.push_back(receiver);
            }
            for (const auto receiver : targets)
                DispatchLocalBroadcast(vm, receiver, intent, call.arguments[4], call.arguments[5], call.arguments[6],
                                       false);
            return dx::VmValue::Int(0);
        });
    builder.VirtualMethod("unbroadcastIntent", "(Landroid/app/IApplicationThread;Landroid/content/Intent;I)V",
                          [context](dx::IntrinsicContext& call) {
                              if (call.arguments[2].AsInt() != 0)
                                  UnsupportedLocalService(call.vm, "sticky broadcast outside current user");
                              const auto intent = dx::IntrinsicCall(call).NonNullRef(1, "intent");
                              context->local_sticky_broadcasts.erase(LocalStickyAction(call.vm, context, intent));
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod(
        "startService",
        "(Landroid/app/IApplicationThread;Landroid/content/Intent;Ljava/lang/String;I)Landroid/content/ComponentName;",
        [context](dx::IntrinsicContext& call) {
            RequireRomServiceRequest(call, call.arguments[2].ref, call.arguments[3].AsInt());
            const auto intent = dx::IntrinsicCall(call).NonNullRef(1, "service");
            const auto target = ResolveExplicitService(call.vm, context, intent);
            if (context->local_services_stopping || !context->application_enabled || !target.declaration ||
                !target.declaration->enabled)
                return dx::VmValue::Ref(dx::VmObjectRef{});
            if (!target.hostable) {
                RecordSecondaryProcessService(call.vm, "startService", *target.declaration);
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            const auto thread = dx::IntrinsicCall(call).NonNullRef(0, "application thread");
            const auto copy = ServiceIntentCopy(call.vm, intent);
            const auto roots = call.vm.ProtectReferences(std::array{copy});
            auto& service = EnsureRomService(call, context, thread, copy, *target.declaration);
            if (service.start_id == std::numeric_limits<std::int32_t>::max())
                UnsupportedLocalService(call.vm, "service start ID exhausted");
            const auto next_id = service.start_id + 1;
            static_cast<void>(CallAndroidMethod(
                call.vm, thread, "scheduleServiceArgs", "(Landroid/os/IBinder;ZIILandroid/content/Intent;)V",
                {dx::VmValue::Ref(service.token), dx::VmValue::Int(0), dx::VmValue::Int(next_id), dx::VmValue::Int(0),
                 dx::VmValue::Ref(copy)}));
            service.start_id = next_id;
            service.started = true;
            return dx::VmValue::Ref(service.component);
        });
    builder.VirtualMethod(
        "stopService", "(Landroid/app/IApplicationThread;Landroid/content/Intent;Ljava/lang/String;I)I",
        [context](dx::IntrinsicContext& call) {
            RequireRomServiceRequest(call, call.arguments[2].ref, call.arguments[3].AsInt());
            const auto target =
                ResolveExplicitService(call.vm, context, dx::IntrinsicCall(call).NonNullRef(1, "service"));
            if (!target.declaration) return dx::VmValue::Int(0);
            const auto found = context->rom_services.find(target.declaration->name);
            if (found == context->rom_services.end() || !found->second.started) return dx::VmValue::Int(0);
            found->second.started = false;
            RetireRomService(call.vm, *context, found);
            return dx::VmValue::Int(1);
        });
    builder.VirtualMethod("stopServiceToken", "(Landroid/content/ComponentName;Landroid/os/IBinder;I)Z",
                          [context](dx::IntrinsicContext& call) {
                              const auto found = std::ranges::find_if(context->rom_services, [&](const auto& entry) {
                                  return entry.second.token == call.arguments[1].ref;
                              });
                              const auto id = call.arguments[2].AsInt();
                              if (found == context->rom_services.end() || !found->second.started ||
                                  (id != -1 && id != found->second.start_id))
                                  return dx::VmValue::Int(0);
                              found->second.started = false;
                              RetireRomService(call.vm, *context, found);
                              return dx::VmValue::Int(1);
                          });
    builder.VirtualMethod(
        "bindService",
        "(Landroid/app/IApplicationThread;Landroid/os/IBinder;Landroid/content/Intent;Ljava/lang/String;"
        "Landroid/app/IServiceConnection;II)I",
        [context](dx::IntrinsicContext& call) {
            RequireRomServiceRequest(call, call.arguments[3].ref, call.arguments[6].AsInt());
            auto& vm = call.vm;
            const auto intent = dx::IntrinsicCall(call).NonNullRef(2, "service");
            const auto component =
                CallAndroidMethod(vm, intent, "getComponent", "()Landroid/content/ComponentName;").ref;
            if (!component.IsValid()) {
                RequireAbsentService(call, context, intent);
                return dx::VmValue::Int(0);
            }
            const auto target = ResolveExplicitService(vm, context, intent);
            if (context->local_services_stopping || !context->application_enabled || !target.declaration ||
                !target.declaration->enabled)
                return dx::VmValue::Int(0);
            if (!target.hostable) {
                RecordSecondaryProcessService(vm, "bindService", *target.declaration);
                return dx::VmValue::Int(0);
            }
            if (vm.CurrentContextToken() != 1 || call.arguments[5].AsInt() != 1)
                UnsupportedLocalService(vm, "local bind requires main thread and BIND_AUTO_CREATE");
            const auto thread = dx::IntrinsicCall(call).NonNullRef(0, "application thread");
            const auto connection = dx::IntrinsicCall(call).NonNullRef(4, "connection");
            for (const auto& [name, service] : context->rom_services) {
                if (std::ranges::find(service.connections, connection) == service.connections.end()) continue;
                if (name != target.declaration->name)
                    UnsupportedLocalService(vm, "one connection cannot bind multiple components");
                return dx::VmValue::Int(1);
            }
            const auto copy = ServiceIntentCopy(vm, intent);
            const auto root = vm.ProtectReferences(std::array{copy});
            auto& service = EnsureRomService(call, context, thread, copy, *target.declaration);
            if (!service.bound) {
                static_cast<void>(CallAndroidMethod(vm, thread, "scheduleBindService",
                                                    "(Landroid/os/IBinder;Landroid/content/Intent;ZI)V",
                                                    {dx::VmValue::Ref(service.token), dx::VmValue::Ref(copy),
                                                     dx::VmValue::Int(service.rebind), dx::VmValue::Int(2)}));
                service.bound = true;
                service.bind_intent = copy;
                if (!service.rebind) service.published = false;
                service.rebind = false;
            }
            service.connections.push_back(connection);
            if (service.published) PublishRomServiceConnection(vm, service, connection);
            return dx::VmValue::Int(1);
        });
    builder.VirtualMethod("publishService", "(Landroid/os/IBinder;Landroid/content/Intent;Landroid/os/IBinder;)V",
                          [context](dx::IntrinsicContext& call) {
                              const auto found = std::ranges::find_if(context->rom_services, [&](const auto& entry) {
                                  return entry.second.token == call.arguments[0].ref;
                              });
                              if (found == context->rom_services.end() || !found->second.bound)
                                  return dx::VmValue::Void();
                              auto& service = found->second;
                              service.binder = call.arguments[2].ref;
                              service.published = true;
                              const auto connections = service.connections;
                              const auto roots = call.vm.ProtectReferences(connections);
                              for (const auto connection : connections)
                                  PublishRomServiceConnection(call.vm, service, connection);
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod(
        "unbindService", "(Landroid/app/IServiceConnection;)Z", [context](dx::IntrinsicContext& call) {
            const auto connection = dx::IntrinsicCall(call).NonNullRef(0, "connection");
            const auto found = std::ranges::find_if(context->rom_services, [&](const auto& entry) {
                return std::ranges::find(entry.second.connections, connection) != entry.second.connections.end();
            });
            if (found == context->rom_services.end()) return dx::VmValue::Int(0);
            if (call.vm.CurrentContextToken() != 1)
                UnsupportedLocalService(call.vm, "local unbind requires the main thread");
            auto& service = found->second;
            std::erase(service.connections, connection);
            if (service.connections.empty()) {
                UnbindRomService(call.vm, service);
                RetireRomService(call.vm, *context, found);
            }
            return dx::VmValue::Int(1);
        });
    builder.VirtualMethod("unbindFinished", "(Landroid/os/IBinder;Landroid/content/Intent;Z)V",
                          [context](dx::IntrinsicContext& call) {
                              const auto found = std::ranges::find_if(context->rom_services, [&](const auto& entry) {
                                  return entry.second.token == call.arguments[0].ref;
                              });
                              if (found != context->rom_services.end() && !found->second.bound)
                                  found->second.rebind = call.arguments[2].AsInt() != 0;
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod("serviceDoneExecuting", "(Landroid/os/IBinder;III)V",
                          [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime {
void ShutdownLocalServices(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    context.local_services_stopping = true;
    std::exception_ptr failure;
    while (!context.rom_services.empty()) {
        const auto service = context.rom_services.begin();
        auto& record = service->second;
        try {
            android_intrinsics::UnbindRomService(vm, record);
            record.connections.clear();
            record.started = false;
            android_intrinsics::RetireRomService(vm, context, service);
        } catch (...) {
            context.rom_services.erase(service);
            if (!failure) failure = std::current_exception();
        }
    }
    context.local_broadcast_filters.clear();
    context.local_sticky_broadcasts.clear();
    if (failure) std::rethrow_exception(failure);
}
}  // namespace ogplay::runtime
