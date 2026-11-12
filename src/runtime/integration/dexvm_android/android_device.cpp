// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_provider_Settings_System.cpp ----
#include "catalog.h"

#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace ogplay::runtime::android_intrinsics {
namespace {

using LocationState = SimulatedDeviceState::Location;
using Provider = LocationState::Provider;
using Geofence = DexVmAndroidContext::LocationRegistrations::Geofence;
using dx::VmObjectRef;
using dx::VmValue;

constexpr auto kRequestClass = "Landroid/location/LocationRequest;";
constexpr auto kPropertiesClass = "Lcom/android/internal/location/ProviderProperties;";

enum class Resolution { none, coarse, fine };

bool Requested(const Context& context, std::string_view permission) {
    return std::ranges::find(context->requested_permissions, permission) != context->requested_permissions.end();
}

Resolution CallerResolution(const Context& context) {
    if (Requested(context, "android.permission.ACCESS_FINE_LOCATION")) return Resolution::fine;
    if (Requested(context, "android.permission.ACCESS_COARSE_LOCATION")) return Resolution::coarse;
    return Resolution::none;
}

Resolution MinimumResolution(std::string_view name) {
    return name == "network" || name == "fused" ? Resolution::coarse : Resolution::fine;
}

void CheckResolution(const Context& context, std::string_view provider) {
    const auto minimum = MinimumResolution(provider);
    if (CallerResolution(context) >= minimum) return;
    const auto required =
        minimum == Resolution::fine ? "ACCESS_FINE_LOCATION" : "ACCESS_COARSE_LOCATION or ACCESS_FINE_LOCATION";
    throw dx::VmJavaThrow{"Ljava/lang/SecurityException;",
                          "\"" + std::string(provider) + "\" location provider requires " + required + " permission."};
}

void CheckPackage(dx::Interpreter& vm, const Context& context, VmObjectRef name) {
    const auto package = name.IsValid() ? vm.StringUtf8(name) : "null";
    if (!name.IsValid() || package != context->package_name)
        throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "invalid package name: " + package};
}

bool AllowedBySecureSettings(const LocationState& state, std::string_view name) {
    // Settings.Secure.isLocationProviderEnabledForUser uses comma-delimited membership.
    for (auto allowed = state.allowed_providers; !allowed.empty();) {
        const auto comma = allowed.find(',');
        if (allowed.substr(0, comma) == name) return true;
        if (comma == std::string_view::npos) break;
        allowed.remove_prefix(comma + 1);
    }
    return false;
}

bool Enabled(const LocationState& state, const Provider& provider) {
    return provider.always_enabled || AllowedBySecureSettings(state, provider.name);
}

const Provider* FindProvider(const LocationState& state, std::string_view name) {
    const auto it = std::ranges::find(state.providers, name, &Provider::name);
    return it == state.providers.end() ? nullptr : &*it;
}

VmObjectRef Properties(dx::Interpreter& vm, const Provider& provider) {
    std::vector<VmValue> arguments;
    for (const auto flag : provider.capabilities)
        arguments.push_back(VmValue::Int(flag));
    arguments.push_back(VmValue::Int(provider.power_requirement));
    arguments.push_back(VmValue::Int(provider.accuracy));
    return NewPlatformObject(vm, kPropertiesClass, "(ZZZZZZZII)V", std::move(arguments));
}

bool MeetsCriteria(dx::Interpreter& vm, const Provider& provider, VmObjectRef criteria) {
    const auto method = vm.Linker().FindDirectMethod(
        vm.Linker().ResolveDescriptor("Landroid/location/LocationProvider;"), "propertiesMeetCriteria",
        "(Ljava/lang/String;Lcom/android/internal/location/ProviderProperties;Landroid/location/Criteria;)Z");
    if (!method) throw std::logic_error("LocationProvider.propertiesMeetCriteria is unavailable");
    const auto properties = Properties(vm, provider);
    const auto roots = vm.ProtectReferences(std::array{properties});
    const auto outcome = vm.Call(*method, std::array{VmValue::Ref(vm.NewStringUtf8(provider.name)),
                                                     VmValue::Ref(properties), VmValue::Ref(criteria)});
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
    return outcome.value.AsInt() != 0;
}

std::vector<std::string_view> FindProviders(dx::Interpreter& vm, const Context& context, VmObjectRef criteria,
                                            bool enabled_only) {
    const auto& state = context->device_state.location;
    const auto allowed = CallerResolution(context);
    std::vector<std::string_view> names;
    for (const auto& provider : state.providers) {
        if (allowed < MinimumResolution(provider.name)) continue;
        if (enabled_only && !Enabled(state, provider)) continue;
        if (criteria.IsValid() && !MeetsCriteria(vm, provider, criteria)) continue;
        names.push_back(provider.name);
    }
    return names;
}

VmValue ProviderList(dx::Interpreter& vm, std::span<const std::string_view> names) {
    const auto list = NewPlatformObject(vm, "Ljava/util/ArrayList;", "()V", {});
    const auto roots = vm.ProtectReferences(std::array{list});
    for (const auto name : names)
        static_cast<void>(
            CallAndroidMethod(vm, list, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(vm.NewStringUtf8(name))}));
    return VmValue::Ref(list);
}

VmObjectRef RequestOrDefault(dx::Interpreter& vm, VmObjectRef request) {
    return request.IsValid() ? request : NewPlatformObject(vm, kRequestClass, "()V", {});
}

std::string RequestProvider(dx::Interpreter& vm, VmObjectRef request) {
    const auto name = CallAndroidMethod(vm, request, "getProvider", "()Ljava/lang/String;").ref;
    return name.IsValid() ? vm.StringUtf8(name) : "null";
}

VmObjectRef SanitizeRequest(dx::Interpreter& vm, VmObjectRef request, Resolution resolution) {
    const auto copy =
        NewPlatformObject(vm, kRequestClass, "(Landroid/location/LocationRequest;)V", {VmValue::Ref(request)});
    const auto root = vm.ProtectReferences(std::array{copy});
    constexpr auto kRequestResult = "(J)Landroid/location/LocationRequest;";
    if (resolution < Resolution::fine) {
        constexpr int kAccuracyFine = 100, kAccuracyBlock = 102, kPowerHigh = 203, kPowerLow = 201;
        const auto quality = CallAndroidMethod(vm, copy, "getQuality", "()I").AsInt();
        if (quality == kAccuracyFine || quality == kPowerHigh)
            static_cast<void>(CallAndroidMethod(vm, copy, "setQuality", "(I)Landroid/location/LocationRequest;",
                                                {VmValue::Int(quality == kAccuracyFine ? kAccuracyBlock : kPowerLow)}));
        constexpr std::int64_t kCoarseInterval = 10 * 60 * 1000;
        if (CallAndroidMethod(vm, copy, "getInterval", "()J").AsLong() < kCoarseInterval)
            static_cast<void>(
                CallAndroidMethod(vm, copy, "setInterval", kRequestResult, {VmValue::Long(kCoarseInterval)}));
        if (CallAndroidMethod(vm, copy, "getFastestInterval", "()J").AsLong() < kCoarseInterval)
            static_cast<void>(
                CallAndroidMethod(vm, copy, "setFastestInterval", kRequestResult, {VmValue::Long(kCoarseInterval)}));
    }
    if (CallAndroidMethod(vm, copy, "getFastestInterval", "()J").AsLong() >
        CallAndroidMethod(vm, copy, "getInterval", "()J").AsLong()) {
        // API19 createSanitizedRequest clamps the input request in this branch.
        const auto interval = CallAndroidMethod(vm, request, "getInterval", "()J");
        static_cast<void>(CallAndroidMethod(vm, request, "setFastestInterval", kRequestResult, {interval}));
    }
    return copy;
}

VmObjectRef PendingIntentKey(dx::Interpreter& vm, VmObjectRef intent) {
    if (!intent.IsValid())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid pending intent: null"};
    const auto sender = CallAndroidMethod(vm, intent, "getTarget", "()Landroid/content/IIntentSender;").ref;
    return CallAndroidMethod(vm, sender, "asBinder", "()Landroid/os/IBinder;").ref;
}

std::pair<bool, VmObjectRef> ReceiverKey(dx::Interpreter& vm, VmObjectRef listener, VmObjectRef intent) {
    if (!intent.IsValid() && !listener.IsValid())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "need either listener or intent"};
    if (intent.IsValid() && listener.IsValid())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "cannot register both listener and intent"};
    if (intent.IsValid()) return {true, PendingIntentKey(vm, intent)};
    return {false, CallAndroidMethod(vm, listener, "asBinder", "()Landroid/os/IBinder;").ref};
}

VmValue RequestUpdates(dx::IntrinsicContext& call, const Context& context) {
    auto& vm = call.vm;
    const auto request = RequestOrDefault(vm, call.arguments[0].ref);
    const auto root = vm.ProtectReferences(std::array{request});
    CheckPackage(vm, context, call.arguments[3].ref);
    const auto provider_name = CallAndroidMethod(vm, request, "getProvider", "()Ljava/lang/String;").ref;
    const auto name = provider_name.IsValid() ? vm.StringUtf8(provider_name) : "null";
    CheckResolution(context, name);
    const auto work = CallAndroidMethod(vm, request, "getWorkSource", "()Landroid/os/WorkSource;").ref;
    if (work.IsValid() && CallAndroidMethod(vm, work, "size", "()I").AsInt() > 0 &&
        !Requested(context, "android.permission.UPDATE_DEVICE_STATS"))
        throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "Requires UPDATE_DEVICE_STATS permission"};
    if (CallAndroidMethod(vm, request, "getHideFromAppOps", "()Z").AsInt() &&
        !Requested(context, "android.permission.UPDATE_APP_OPS_STATS"))
        throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "Requires UPDATE_APP_OPS_STATS permission"};
    const auto sanitized = SanitizeRequest(vm, request, CallerResolution(context));
    const auto sanitized_root = vm.ProtectReferences(std::array{sanitized});

    const auto listener = call.arguments[1].ref;
    const auto intent = call.arguments[2].ref;
    const auto key = ReceiverKey(vm, listener, intent);
    const auto key_root = vm.ProtectReferences(std::array{key.second});
    const auto& state = context->device_state.location;
    if (!provider_name.IsValid())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "provider name must not be null"};
    const auto* provider = FindProvider(state, name);
    if (!provider) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "provider doesn't exist: " + name};

    const auto enabled = Enabled(state, *provider);
    if (!enabled && intent.IsValid()) {
        if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.location_pending_intent_delivery", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "PendingIntent delivery for KEY_PROVIDER_ENABLED is unsupported"};
    }
    // Commit before notifying the listener: a direct local callback may re-enter removeUpdates.
    auto& receiver =
        context->location_registrations.receivers
            .try_emplace(key,
                         DexVmAndroidContext::LocationRegistrations::Receiver{intent.IsValid() ? intent : listener, {}})
            .first->second;
    receiver.requests.insert_or_assign(provider->name, sanitized);
    if (!enabled) {
        const auto target = receiver.target;
        const auto target_root = vm.ProtectReferences(std::array{target});
        try {
            static_cast<void>(CallAndroidMethod(vm, target, "onProviderDisabled", "(Ljava/lang/String;)V",
                                                {VmValue::Ref(vm.NewStringUtf8(name))}));
        } catch (const dx::VmJavaThrow& error) {
            if (error.descriptor != "Landroid/os/RemoteException;") throw;
        }
    }
    return VmValue::Void();
}

void CheckGeofenceResolution(const Context& context) {
    if (CallerResolution(context) < Resolution::fine)
        throw dx::VmJavaThrow{"Ljava/lang/SecurityException;",
                              "Geofence usage requires ACCESS_FINE_LOCATION permission"};
}

Geofence ReadGeofence(dx::Interpreter& vm, VmObjectRef fence, VmObjectRef intent, VmObjectRef sender) {
    return {fence,
            intent,
            sender,
            CallAndroidMethod(vm, fence, "getLatitude", "()D").AsDouble(),
            CallAndroidMethod(vm, fence, "getLongitude", "()D").AsDouble(),
            CallAndroidMethod(vm, fence, "getRadius", "()F").AsFloat(),
            CallAndroidMethod(vm, fence, "getType", "()I").AsInt()};
}

bool SameGeofence(const Geofence& a, const Geofence& b) {
    return a.sender == b.sender && (a.fence == b.fence || (a.latitude == b.latitude && a.longitude == b.longitude &&
                                                           a.radius == b.radius && a.type == b.type));
}

bool GpsCommand(dx::Interpreter& vm, std::string_view command, VmObjectRef extras) {
    if (command == "force_time_injection") return true;
    if (command != "delete_aiding_data") return false;  // The disabled engine has not advertised XTRA.
    if (!extras.IsValid()) return true;
    for (const auto* flag : {"ephemeris", "almanac", "position", "time", "iono", "utc", "health", "svdir", "svsteer",
                             "sadata", "rti", "celldb-info", "all"}) {
        if (CallAndroidMethod(vm, extras, "getBoolean", "(Ljava/lang/String;)Z", {VmValue::Ref(vm.NewStringUtf8(flag))})
                .AsInt())
            return true;
    }
    return false;
}

}  // namespace

static std::optional<std::string_view> ReadLocationSecureSetting(const LocationState& state, std::string_view key) {
    if (key == "location_providers_allowed") return state.allowed_providers;
    if (key == "allow_mock_location") return state.allow_mock_location ? "1" : "0";
    if (key == "location_mode") {
        // Settings.Secure.getLocationModeForUser derives the mode from GPS and network.
        if (AllowedBySecureSettings(state, "gps")) return AllowedBySecureSettings(state, "network") ? "3" : "1";
        return AllowedBySecureSettings(state, "network") ? "2" : "0";
    }
    return std::nullopt;
}

Decl Declare_local_LocationService_Class(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalLocationService;",
                                                    "Landroid/location/ILocationManager$Stub;");
    builder.VirtualMethod("getAllProviders", "()Ljava/util/List;", [context](dx::IntrinsicContext& call) {
        std::vector<std::string_view> names;
        for (const auto& provider : context->device_state.location.providers)
            names.push_back(provider.name);
        return ProviderList(call.vm, names);
    });
    builder.VirtualMethod(
        "getProviders", "(Landroid/location/Criteria;Z)Ljava/util/List;", [context](dx::IntrinsicContext& call) {
            return ProviderList(call.vm,
                                FindProviders(call.vm, context, call.arguments[0].ref, call.arguments[1].AsInt() != 0));
        });
    builder.VirtualMethod("getBestProvider", "(Landroid/location/Criteria;Z)Ljava/lang/String;",
                          [context](dx::IntrinsicContext& call) {
                              const auto enabled_only = call.arguments[1].AsInt() != 0;
                              auto names = FindProviders(call.vm, context, call.arguments[0].ref, enabled_only);
                              if (names.empty()) names = FindProviders(call.vm, context, VmObjectRef{}, enabled_only);
                              if (names.empty()) return VmValue::Ref(VmObjectRef{});
                              for (const auto* preferred : {"gps", "network"}) {
                                  if (std::ranges::find(names, preferred) != names.end())
                                      return MakeString(call, preferred);
                              }
                              return VmValue::Ref(call.vm.NewStringUtf8(names.front()));
                          });
    builder.VirtualMethod("providerMeetsCriteria", "(Ljava/lang/String;Landroid/location/Criteria;)Z",
                          [context](dx::IntrinsicContext& call) {
                              const auto arg = call.arguments[0].ref;
                              const auto name = arg.IsValid() ? call.vm.StringUtf8(arg) : "null";
                              const auto* provider = FindProvider(context->device_state.location, name);
                              if (!provider)
                                  throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "provider=" + name};
                              return VmValue::Int(MeetsCriteria(call.vm, *provider, call.arguments[1].ref));
                          });
    builder.VirtualMethod("getProviderProperties",
                          "(Ljava/lang/String;)Lcom/android/internal/location/ProviderProperties;",
                          [context](dx::IntrinsicContext& call) {
                              const auto arg = call.arguments[0].ref;
                              const auto name = arg.IsValid() ? call.vm.StringUtf8(arg) : "null";
                              const auto* provider = FindProvider(context->device_state.location, name);
                              if (!provider) return VmValue::Ref(VmObjectRef{});
                              CheckResolution(context, name);
                              return VmValue::Ref(Properties(call.vm, *provider));
                          });
    builder.VirtualMethod("isProviderEnabled", "(Ljava/lang/String;)Z", [context](dx::IntrinsicContext& call) {
        const auto arg = call.arguments[0].ref;
        const auto name = arg.IsValid() ? call.vm.StringUtf8(arg) : "null";
        CheckResolution(context, name);
        const auto& state = context->device_state.location;
        const auto* provider = FindProvider(state, name);
        return VmValue::Int(provider && Enabled(state, *provider));
    });

    builder.VirtualMethod("requestLocationUpdates",
                          "(Landroid/location/LocationRequest;Landroid/location/ILocationListener;"
                          "Landroid/app/PendingIntent;Ljava/lang/String;)V",
                          [context](dx::IntrinsicContext& call) { return RequestUpdates(call, context); });
    builder.VirtualMethod("removeUpdates",
                          "(Landroid/location/ILocationListener;Landroid/app/PendingIntent;Ljava/lang/String;)V",
                          [context](dx::IntrinsicContext& call) {
                              CheckPackage(call.vm, context, call.arguments[2].ref);
                              const auto key = ReceiverKey(call.vm, call.arguments[0].ref, call.arguments[1].ref);
                              context->location_registrations.receivers.erase(key);
                              return VmValue::Void();
                          });
    builder.VirtualMethod(
        "locationCallbackFinished", "(Landroid/location/ILocationListener;)V", [](dx::IntrinsicContext& call) {
            // No outstanding wake locks. In particular, completion never re-adds a receiver.
            static_cast<void>(CallAndroidMethod(call.vm, call.arguments[0].ref, "asBinder", "()Landroid/os/IBinder;"));
            return VmValue::Void();
        });
    builder.VirtualMethod("getLastLocation",
                          "(Landroid/location/LocationRequest;Ljava/lang/String;)Landroid/location/Location;",
                          [context](dx::IntrinsicContext& call) {
                              const auto request = RequestOrDefault(call.vm, call.arguments[0].ref);
                              const auto roots = call.vm.ProtectReferences(std::array{request});
                              CheckPackage(call.vm, context, call.arguments[1].ref);
                              CheckResolution(context, RequestProvider(call.vm, request));
                              return VmValue::Ref(VmObjectRef{});
                          });
    builder.VirtualMethod(
        "requestGeofence",
        "(Landroid/location/LocationRequest;Landroid/location/Geofence;"
        "Landroid/app/PendingIntent;Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            auto& vm = call.vm;
            const auto request = RequestOrDefault(vm, call.arguments[0].ref);
            const auto roots = vm.ProtectReferences(std::array{request});
            CheckGeofenceResolution(context);
            const auto intent = call.arguments[2].ref;
            if (!intent.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid pending intent: null"};
            CheckPackage(vm, context, call.arguments[3].ref);
            CheckResolution(context, RequestProvider(vm, request));
            const auto sanitized = SanitizeRequest(vm, request, CallerResolution(context));
            const auto sanitized_root = vm.ProtectReferences(std::array{sanitized});
            const auto sender = PendingIntentKey(vm, intent);
            const auto sender_root = vm.ProtectReferences(std::array{sender});
            auto entry = ReadGeofence(vm, call.arguments[1].ref, intent, sender);
            entry.expires_at = CallAndroidMethod(vm, sanitized, "getExpireAt", "()J").AsLong();
            auto& fences = context->location_registrations.geofences;
            const auto now = context->uptime_millis.load();
            std::erase_if(fences, [now](const auto& fence) { return fence.expires_at < now; });
            if (entry.expires_at < now) return VmValue::Void();
            const auto old =
                std::ranges::find_if(fences, [&](const auto& fence) { return SameGeofence(fence, entry); });
            if (old == fences.end())
                fences.push_back(entry);
            else
                *old = entry;
            return VmValue::Void();
        });
    builder.VirtualMethod(
        "removeGeofence", "(Landroid/location/Geofence;Landroid/app/PendingIntent;Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            CheckGeofenceResolution(context);
            const auto intent = call.arguments[1].ref;
            if (!intent.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid pending intent: null"};
            CheckPackage(call.vm, context, call.arguments[2].ref);
            const auto sender = PendingIntentKey(call.vm, intent);
            const auto sender_root = call.vm.ProtectReferences(std::array{sender});
            const auto fence = call.arguments[0].ref;
            const auto entry = fence.IsValid() ? ReadGeofence(call.vm, fence, intent, sender) : Geofence{};
            const auto now = context->uptime_millis.load();
            std::erase_if(context->location_registrations.geofences, [&](const auto& registered) {
                if (registered.expires_at < now) return true;
                return fence.IsValid() ? SameGeofence(registered, entry) : registered.sender == sender;
            });
            return VmValue::Void();
        });

    builder.VirtualMethod("addGpsStatusListener", "(Landroid/location/IGpsStatusListener;Ljava/lang/String;)Z",
                          [context](dx::IntrinsicContext& call) {
                              CheckResolution(context, "gps");
                              // API19 AppOps.checkOperation uses MODE_ALLOWED for location when there is no override.
                              const auto listener =
                                  dx::IntrinsicCall(call).NonNullRef(0, "listener is null in addGpsStatusListener");
                              try {
                                  const auto binder =
                                      CallAndroidMethod(call.vm, listener, "asBinder", "()Landroid/os/IBinder;").ref;
                                  context->location_registrations.gps_listeners.try_emplace(binder, listener);
                              } catch (const dx::VmJavaThrow& error) {
                                  if (error.descriptor != "Landroid/os/RemoteException;") throw;
                                  return VmValue::Int(0);
                              }
                              return VmValue::Int(1);
                          });
    builder.VirtualMethod(
        "removeGpsStatusListener", "(Landroid/location/IGpsStatusListener;)V", [context](dx::IntrinsicContext& call) {
            if (const auto listener = call.arguments[0].ref; listener.IsValid()) {
                try {
                    const auto binder = CallAndroidMethod(call.vm, listener, "asBinder", "()Landroid/os/IBinder;").ref;
                    context->location_registrations.gps_listeners.erase(binder);
                } catch (const dx::VmJavaThrow& error) {
                    const auto type = call.vm.Linker().ResolveDescriptor(error.descriptor);
                    if (!call.vm.Linker().IsAssignable(call.vm.Linker().ResolveDescriptor("Ljava/lang/Exception;"),
                                                       type))
                        throw;
                }
            }
            return VmValue::Void();
        });
    builder.VirtualMethod(
        "sendExtraCommand", "(Ljava/lang/String;Ljava/lang/String;Landroid/os/Bundle;)Z",
        [context](dx::IntrinsicContext& call) {
            const auto provider = call.vm.StringUtf8(dx::IntrinsicCall(call).NonNullRef(0, "provider"));
            CheckResolution(context, provider);
            if (!Requested(context, "android.permission.ACCESS_LOCATION_EXTRA_COMMANDS"))
                throw dx::VmJavaThrow{"Ljava/lang/SecurityException;",
                                      "Requires ACCESS_LOCATION_EXTRA_COMMANDS permission"};
            const auto command = call.arguments[1].ref;
            return VmValue::Int(provider == "gps" && command.IsValid() &&
                                GpsCommand(call.vm, call.vm.StringUtf8(command), call.arguments[2].ref));
        });

    builder.VirtualMethod("geocoderIsPresent", "()Z", [context](dx::IntrinsicContext&) {
        return VmValue::Int(context->device_state.location.geocoder_present);
    });
    const auto no_geocoder = [](dx::IntrinsicContext&) { return VmValue::Ref(VmObjectRef{}); };
    builder.VirtualMethod("getFromLocation", "(DDILandroid/location/GeocoderParams;Ljava/util/List;)Ljava/lang/String;",
                          no_geocoder);
    builder.VirtualMethod(
        "getFromLocationName",
        "(Ljava/lang/String;DDDDILandroid/location/GeocoderParams;Ljava/util/List;)Ljava/lang/String;", no_geocoder);

    const auto mock = [context](dx::IntrinsicContext&) -> VmValue {
        const auto* requirement = context->device_state.location.allow_mock_location
                                      ? "Requires ACCESS_MOCK_LOCATION permission"
                                      : "Requires ACCESS_MOCK_LOCATION secure setting";
        throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", requirement};
    };
    for (const auto& [name, signature] : std::array{
             std::pair{"addTestProvider", "(Ljava/lang/String;Lcom/android/internal/location/ProviderProperties;)V"},
             std::pair{"removeTestProvider", "(Ljava/lang/String;)V"},
             std::pair{"setTestProviderLocation", "(Ljava/lang/String;Landroid/location/Location;)V"},
             std::pair{"clearTestProviderLocation", "(Ljava/lang/String;)V"},
             std::pair{"setTestProviderEnabled", "(Ljava/lang/String;Z)V"},
             std::pair{"clearTestProviderEnabled", "(Ljava/lang/String;)V"},
             std::pair{"setTestProviderStatus", "(Ljava/lang/String;ILandroid/os/Bundle;J)V"},
             std::pair{"clearTestProviderStatus", "(Ljava/lang/String;)V"},
         })
        builder.VirtualMethod(name, signature, mock);
    builder.VirtualMethod("reportLocation", "(Landroid/location/Location;Z)V", [](dx::IntrinsicContext&) -> VmValue {
        throw dx::VmJavaThrow{
            "Ljava/lang/SecurityException;",
            "need INSTALL_LOCATION_PROVIDER permission, or UID of a currently bound location provider"};
    });
    builder.VirtualMethod("sendNiResponse", "(II)Z", [](dx::IntrinsicContext&) -> VmValue {
        throw dx::VmJavaThrow{"Ljava/lang/SecurityException;",
                              "calling sendNiResponse from outside of the system is not allowed"};
    });
    return std::move(builder).Build();
}

Decl Declare_local_UsbService_Class(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalUsbService;",
                                                    "Landroid/hardware/usb/IUsbManager$Stub;");
    builder.VirtualMethod("getDeviceList", "(Landroid/os/Bundle;)V", [](dx::IntrinsicContext& call) {
        static_cast<void>(dx::IntrinsicCall(call).NonNullRef(0, "devices"));
        return dx::VmValue::Void();
    });
    const auto absent = [](dx::IntrinsicContext&) { return dx::VmValue::Ref(dx::VmObjectRef{}); };
    const auto no_permission = [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); };
    builder.VirtualMethod("getCurrentAccessory", "()Landroid/hardware/usb/UsbAccessory;", absent);
    builder.VirtualMethod("hasDevicePermission", "(Landroid/hardware/usb/UsbDevice;)Z", no_permission);
    builder.VirtualMethod("hasAccessoryPermission", "(Landroid/hardware/usb/UsbAccessory;)Z", no_permission);
    builder.VirtualMethod("openDevice", "(Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;", absent);
    builder.VirtualMethod("openAccessory", "(Landroid/hardware/usb/UsbAccessory;)Landroid/os/ParcelFileDescriptor;",
                          absent);
    DenyServiceMethod(builder, "setCurrentFunction", "(Ljava/lang/String;Z)V", "MANAGE_USB");
    DenyServiceMethod(builder, "setMassStorageBackingFile", "(Ljava/lang/String;)V", "MANAGE_USB");
    for (const auto& [name, descriptor] : std::array{
             std::pair{"requestAccessoryPermission",
                       "(Landroid/hardware/usb/UsbAccessory;Ljava/lang/String;Landroid/app/PendingIntent;)V"},
             std::pair{"requestDevicePermission",
                       "(Landroid/hardware/usb/UsbDevice;Ljava/lang/String;Landroid/app/PendingIntent;)V"},
         }) {
        const auto method = std::string("android.hardware.usb.IUsbManager.") + name + descriptor;
        builder.VirtualMethod(name, descriptor, [method](dx::IntrinsicContext& call) -> dx::VmValue {
            static_cast<void>(dx::IntrinsicCall(call).NonNullRef(0, "device"));
            static_cast<void>(dx::IntrinsicCall(call).NonNullRef(2, "intent"));
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented(method, 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "USB permission PendingIntent delivery"};
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"allowUsbDebugging", "(ZLjava/lang/String;)V"},
             std::pair{"clearDefaults", "(Ljava/lang/String;I)V"},
             std::pair{"clearUsbDebuggingKeys", "()V"},
             std::pair{"denyUsbDebugging", "()V"},
             std::pair{"grantAccessoryPermission", "(Landroid/hardware/usb/UsbAccessory;I)V"},
             std::pair{"grantDevicePermission", "(Landroid/hardware/usb/UsbDevice;I)V"},
             std::pair{"hasDefaults", "(Ljava/lang/String;I)Z"},
             std::pair{"setAccessoryPackage", "(Landroid/hardware/usb/UsbAccessory;Ljava/lang/String;I)V"},
             std::pair{"setDevicePackage", "(Landroid/hardware/usb/UsbDevice;Ljava/lang/String;I)V"},
         }) {
        DenyServiceMethod(builder, name, descriptor, "MANAGE_USB");
    }
    return std::move(builder).Build();
}

namespace {
struct SettingsPath {
    std::string table;
    std::optional<std::string> name;
};

SettingsPath ReadSettingsPath(dx::Interpreter& vm, dx::VmObjectRef uri) {
    const auto text = CallAndroidMethod(vm, uri, "toString", "()Ljava/lang/String;").ref;
    const auto value = vm.StringUtf8(text);
    constexpr std::string_view prefix = "content://settings/";
    if (!value.starts_with(prefix))
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Unknown settings URI: " + value};
    const auto table_end = value.find('/', prefix.size());
    auto table = value.substr(prefix.size(), table_end - prefix.size());
    if (table != "system" && table != "secure" && table != "global")
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Unknown settings table: " + table};
    SettingsPath path{std::move(table), std::nullopt};
    if (table_end != std::string::npos) {
        const auto name = CallAndroidMethod(vm, uri, "getLastPathSegment", "()Ljava/lang/String;").ref;
        if (name.IsValid()) path.name = vm.StringUtf8(name);
    }
    return path;
}

void CheckSettingsWriter(const Context& context, const SettingsPath& path) {
    if (path.table != "system")
        throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "WRITE_SECURE_SETTINGS is required"};
    RequireServicePermission(context, "WRITE_SETTINGS");
}

PreferenceMap ReadSettingsTable(const Context& context, std::string_view table) {
    PreferenceMap result;
    if (table == "secure") {
        for (const auto& [key, value] : context->secure_settings)
            result.emplace(key, value);
        for (const auto* key : {"location_mode", "location_providers_allowed", "allow_mock_location"})
            if (const auto value = ReadLocationSecureSetting(context->device_state.location, key))
                result.insert_or_assign(key, std::string(*value));
    } else if (const auto store = context->preferences.find("__android.settings." + std::string(table));
               store != context->preferences.end()) {
        result = store->second;
    }
    if (table == "global") {
        const auto& network = context->device_state.network;
        result.insert_or_assign("wifi_on", network.wifi_enabled);
        result.insert_or_assign("mobile_data", network.mobile_data_enabled);
    }
    return result;
}

std::string SettingsValueString(const PreferenceValue& value) {
    return std::visit(
        [](const auto& item) -> std::string {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, std::string>)
                return item;
            else if constexpr (std::is_same_v<T, bool>)
                return item ? "1" : "0";
            else
                return std::to_string(item);
        },
        value);
}

SettingsPath SelectSettingsRows(dx::IntrinsicContext& call, SettingsPath path, std::size_t selection) {
    const auto where = call.arguments[selection].ref;
    if (!where.IsValid() || call.vm.StringUtf8(where).empty()) return path;
    auto clause = call.vm.StringUtf8(where);
    std::erase(clause, ' ');
    const auto args = call.arguments[selection + 1].ref;
    if (!path.name && clause == "name=?" && args.IsValid() && call.vm.Model().ArrayLength(args) == 1) {
        const auto name = call.vm.Model().GetObjectElement(args, 0);
        if (name.IsValid()) path.name = call.vm.StringUtf8(name);
        return path;
    }
    if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.settings.selection", 0);
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "Settings selection requires a name predicate"};
}

bool WriteSystemSetting(dx::IntrinsicContext& call, const Context& context, const std::string& name,
                        dx::VmObjectRef value, dx::VmObjectRef uri) {
    auto& store = context->preferences["__android.settings.system"];
    if (value.IsValid()) {
        const auto text = call.vm.StringUtf8(value);
        const auto old = store.find(name);
        if (old != store.end() && SettingsValueString(old->second) == text) return false;
        store.insert_or_assign(name, text);
    } else if (store.erase(name) == 0) {
        return false;
    }
    ++context->settings_system_version;
    const auto root = call.vm.ProtectReferences(std::array{uri});
    NotifyLocalContentChange(call.vm, context, uri, dx::VmObjectRef{}, false);
    return true;
}

dx::VmValue ContentValue(dx::Interpreter& vm, dx::VmObjectRef values, const char* key) {
    return CallAndroidMethod(vm, values, "getAsString", "(Ljava/lang/String;)Ljava/lang/String;",
                             {dx::VmValue::Ref(vm.NewStringUtf8(key))});
}
}  // namespace

Decl Declare_local_SettingsProvider_Class(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalSettingsProvider;",
                                                    "Landroid/os/Binder;", {"Landroid/content/IContentProvider;"});
    builder.Constructor("()V", [](dx::IntrinsicContext& call) {
        const auto parent = call.vm.Linker().ResolveDescriptor("Landroid/os/Binder;");
        const auto init = call.vm.Linker().FindDirectMethod(parent, "<init>", "()V");
        if (!init) throw std::logic_error("Binder constructor is unavailable");
        const auto result = call.vm.Call(*init, std::array{dx::VmValue::Ref(call.receiver)});
        if (result.exception.IsValid()) call.vm.SetPendingException(result.exception);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("asBinder", "()Landroid/os/IBinder;",
                          [](dx::IntrinsicContext& call) { return dx::VmValue::Ref(call.receiver); });
    builder.VirtualMethod(
        "call", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Landroid/os/Bundle;)Landroid/os/Bundle;",
        [context](dx::IntrinsicContext& call) {
            auto& vm = call.vm;
            dx::IntrinsicCall args(call);
            const auto command = vm.StringUtf8(args.NonNullRef(1, "method"));
            const auto key = args.NonNullRef(2, "name");
            const auto name = vm.StringUtf8(key);
            const auto extras = call.arguments[3].ref;
            if (extras.IsValid()) {
                const auto user = CallAndroidMethod(vm, extras, "getInt", "(Ljava/lang/String;I)I",
                                                    {MakeString(call, "_user"), dx::VmValue::Int(0)})
                                      .AsInt();
                if (user != 0)
                    throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "settings access outside the current user"};
            }
            if (command.starts_with("PUT_")) {
                const SettingsPath path{command.substr(4), name};
                CheckSettingsWriter(context, path);
                const auto value_ref =
                    CallAndroidMethod(vm, extras, "getString", "(Ljava/lang/String;)Ljava/lang/String;",
                                      {MakeString(call, "value")})
                        .ref;
                const auto value_root = vm.ProtectReferences(std::array{value_ref});
                const auto type = vm.Linker().ResolveDescriptor("Landroid/provider/Settings$System;");
                const auto method =
                    vm.Linker().FindDirectMethod(type, "getUriFor", "(Ljava/lang/String;)Landroid/net/Uri;");
                const auto result = vm.Call(*method, std::array{dx::VmValue::Ref(key)});
                if (result.exception.IsValid()) throw dx::VmJavaThrow{"", "", result.exception};
                WriteSystemSetting(call, context, name, value_ref, result.value.ref);
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }

            if (command != "GET_system" && command != "GET_secure" && command != "GET_global") {
                if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.settings_provider.call", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", command};
            }
            const auto table = ReadSettingsTable(context, command.substr(4));
            const auto found = table.find(name);
            const auto value = found == table.end() ? dx::VmValue::Ref(dx::VmObjectRef{})
                                                    : MakeString(call, SettingsValueString(found->second));
            const auto value_root = vm.ProtectReferences(std::array{value.ref});
            const auto result = NewPlatformObject(vm, "Landroid/os/Bundle;", "()V", {});
            const auto result_root = vm.ProtectReferences(std::array{result});
            static_cast<void>(CallAndroidMethod(vm, result, "putString", "(Ljava/lang/String;Ljava/lang/String;)V",
                                                {MakeString(call, "value"), value}));
            return dx::VmValue::Ref(result);
        });
    const auto parse = [](dx::IntrinsicContext& call, std::size_t index) {
        return ReadSettingsPath(call.vm, dx::IntrinsicCall(call).NonNullRef(index, "uri"));
    };
    builder.VirtualMethod("getType", "(Landroid/net/Uri;)Ljava/lang/String;", [parse](dx::IntrinsicContext& call) {
        const auto path = parse(call, 0);
        return MakeString(call,
                          std::string(path.name ? "vnd.android.cursor.item/" : "vnd.android.cursor.dir/") + path.table);
    });
    builder.VirtualMethod(
        "query",
        "(Ljava/lang/String;Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;"
        "Ljava/lang/String;Landroid/os/ICancellationSignal;)Landroid/database/Cursor;",
        [context, parse](dx::IntrinsicContext& call) {
            auto& vm = call.vm;
            const auto path = SelectSettingsRows(call, parse(call, 1), 3);
            auto columns = call.arguments[2].ref;
            if (!columns.IsValid()) {
                columns = NewServiceArray(vm, "[Ljava/lang/String;", 3);
                const auto root = vm.ProtectReferences(std::array{columns});
                vm.Model().SetObjectElement(columns, 0, vm.NewStringUtf8("_id"));
                vm.Model().SetObjectElement(columns, 1, vm.NewStringUtf8("name"));
                vm.Model().SetObjectElement(columns, 2, vm.NewStringUtf8("value"));
            }
            const auto columns_root = vm.ProtectReferences(std::array{columns});
            const auto cursor = NewPlatformObject(vm, "Landroid/database/MatrixCursor;", "([Ljava/lang/String;)V",
                                                  {dx::VmValue::Ref(columns)});
            const auto cursor_root = vm.ProtectReferences(std::array{cursor});
            const auto table = ReadSettingsTable(context, path.table);
            int id = 0;
            for (const auto& [name, value] : table) {
                ++id;
                if (path.name && name != *path.name) continue;
                const auto row =
                    CallAndroidMethod(vm, cursor, "newRow", "()Landroid/database/MatrixCursor$RowBuilder;").ref;
                const auto row_root = vm.ProtectReferences(std::array{row});
                for (JniSize i = 0; i < vm.Model().ArrayLength(columns); ++i) {
                    const auto column = vm.StringUtf8(vm.Model().GetObjectElement(columns, i));
                    std::string cell;
                    if (column == "_id")
                        cell = std::to_string(id);
                    else if (column == "name")
                        cell = name;
                    else if (column == "value")
                        cell = SettingsValueString(value);
                    else
                        throw dx::VmJavaThrow{"Landroid/database/sqlite/SQLiteException;", "no such column: " + column};
                    static_cast<void>(CallAndroidMethod(
                        vm, row, "add", "(Ljava/lang/Object;)Landroid/database/MatrixCursor$RowBuilder;",
                        {MakeString(call, cell)}));
                }
            }
            return dx::VmValue::Ref(cursor);
        });
    const auto insert = [context](dx::IntrinsicContext& call, const SettingsPath& path, dx::VmObjectRef values) {
        const auto name = ContentValue(call.vm, values, "name").ref;
        if (!name.IsValid())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Settings insert requires a name"};
        const auto name_root = call.vm.ProtectReferences(std::array{name});
        const auto uri = NewPlatformObject(call.vm, "Landroid/net/Uri$Builder;", "()V", {});
        const auto uri_root = call.vm.ProtectReferences(std::array{uri});
        static_cast<void>(CallAndroidMethod(call.vm, uri, "scheme", "(Ljava/lang/String;)Landroid/net/Uri$Builder;",
                                            {MakeString(call, "content")}));
        static_cast<void>(CallAndroidMethod(call.vm, uri, "authority", "(Ljava/lang/String;)Landroid/net/Uri$Builder;",
                                            {MakeString(call, "settings")}));
        static_cast<void>(CallAndroidMethod(call.vm, uri, "appendPath", "(Ljava/lang/String;)Landroid/net/Uri$Builder;",
                                            {MakeString(call, path.table)}));
        static_cast<void>(CallAndroidMethod(call.vm, uri, "appendPath", "(Ljava/lang/String;)Landroid/net/Uri$Builder;",
                                            {dx::VmValue::Ref(name)}));
        const auto row = CallAndroidMethod(call.vm, uri, "build", "()Landroid/net/Uri;").ref;
        const auto row_root = call.vm.ProtectReferences(std::array{row});
        const auto key = call.vm.StringUtf8(name);
        WriteSystemSetting(call, context, key, ContentValue(call.vm, values, "value").ref, row);
        return dx::VmValue::Ref(row);
    };
    builder.VirtualMethod("insert",
                          "(Ljava/lang/String;Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;",
                          [context, parse, insert](dx::IntrinsicContext& call) {
                              const auto path = parse(call, 1);
                              CheckSettingsWriter(context, path);
                              return insert(call, path, dx::IntrinsicCall(call).NonNullRef(2, "values"));
                          });
    builder.VirtualMethod("bulkInsert", "(Ljava/lang/String;Landroid/net/Uri;[Landroid/content/ContentValues;)I",
                          [context, parse, insert](dx::IntrinsicContext& call) {
                              const auto path = parse(call, 1);
                              CheckSettingsWriter(context, path);
                              const auto values = dx::IntrinsicCall(call).NonNullRef(2, "values");
                              const auto count = call.vm.Model().ArrayLength(values);
                              for (JniSize i = 0; i < count; ++i)
                                  static_cast<void>(insert(call, path, call.vm.Model().GetObjectElement(values, i)));
                              return dx::VmValue::Int(count);
                          });
    const auto modify = [context, parse](dx::IntrinsicContext& call, bool update) {
        auto path = parse(call, 1);
        CheckSettingsWriter(context, path);
        path = SelectSettingsRows(call, std::move(path), update ? 3 : 2);
        const auto rows = ReadSettingsTable(context, path.table);
        const auto value = update ? ContentValue(call.vm, dx::IntrinsicCall(call).NonNullRef(2, "values"), "value").ref
                                  : dx::VmObjectRef{};
        const auto root = call.vm.ProtectReferences(std::array{value});
        int count = 0;
        for (const auto& [key, _] : rows) {
            if (path.name && key != *path.name) continue;
            WriteSystemSetting(call, context, key, value, call.arguments[1].ref);
            ++count;
        }
        return dx::VmValue::Int(count);
    };
    builder.VirtualMethod("update",
                          "(Ljava/lang/String;Landroid/net/Uri;Landroid/content/ContentValues;"
                          "Ljava/lang/String;[Ljava/lang/String;)I",
                          [modify](dx::IntrinsicContext& call) { return modify(call, true); });
    builder.VirtualMethod("delete", "(Ljava/lang/String;Landroid/net/Uri;Ljava/lang/String;[Ljava/lang/String;)I",
                          [modify](dx::IntrinsicContext& call) { return modify(call, false); });
    builder.VirtualMethod(
        "createCancellationSignal", "()Landroid/os/ICancellationSignal;", [](dx::IntrinsicContext& call) {
            auto& vm = call.vm;
            const auto method =
                vm.Linker().FindDirectMethod(vm.Linker().ResolveDescriptor("Landroid/os/CancellationSignal;"),
                                             "createTransport", "()Landroid/os/ICancellationSignal;");
            const auto result = vm.Call(*method, {});
            if (result.exception.IsValid()) throw dx::VmJavaThrow{"", "", result.exception};
            return result.value;
        });
    for (const auto& [name, descriptor] : std::array{
             std::pair{"canonicalize", "(Ljava/lang/String;Landroid/net/Uri;)Landroid/net/Uri;"},
             std::pair{"uncanonicalize", "(Ljava/lang/String;Landroid/net/Uri;)Landroid/net/Uri;"},
             std::pair{"getStreamTypes", "(Landroid/net/Uri;Ljava/lang/String;)[Ljava/lang/String;"},
         }) {
        builder.VirtualMethod(name, descriptor,
                              [](dx::IntrinsicContext&) { return dx::VmValue::Ref(dx::VmObjectRef{}); });
    }
    for (
        const auto& [name, descriptor] : std::array{
            std::pair{
                "openAssetFile",
                "(Ljava/lang/String;Landroid/net/Uri;Ljava/lang/String;Landroid/os/ICancellationSignal;)Landroid/content/res/AssetFileDescriptor;"},
            std::pair{
                "openFile",
                "(Ljava/lang/String;Landroid/net/Uri;Ljava/lang/String;Landroid/os/ICancellationSignal;)Landroid/os/ParcelFileDescriptor;"},
            std::pair{
                "openTypedAssetFile",
                "(Ljava/lang/String;Landroid/net/Uri;Ljava/lang/String;Landroid/os/Bundle;Landroid/os/ICancellationSignal;)Landroid/content/res/AssetFileDescriptor;"},
        }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{"Ljava/io/FileNotFoundException;", "Settings rows have no file data"};
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"applyBatch",
                       "(Ljava/lang/String;Ljava/util/ArrayList;)[Landroid/content/ContentProviderResult;"},
         }) {
        RejectUnsupportedServiceMethod(builder, "android.content.IContentProvider", name, descriptor);
    }
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {

Decl Declare_local_SmsService_Class(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalSmsService;",
                                                    "Lcom/android/internal/telephony/ISms$Stub;");
    for (
        const auto& [name, descriptor] : std::array{
            std::pair{
                "sendData",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I[BLandroid/app/PendingIntent;Landroid/app/PendingIntent;)V"},
            std::pair{
                "sendText",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Landroid/app/PendingIntent;Landroid/app/PendingIntent;)V"},
            std::pair{
                "sendMultipartText",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/util/List;Ljava/util/List;Ljava/util/List;)V"},
        }) {
        const auto signature = std::string("com.android.internal.telephony.ISms.") + name + descriptor;
        builder.VirtualMethod(name, descriptor, [context, signature](dx::IntrinsicContext& call) -> dx::VmValue {
            RequireServicePermission(context, "SEND_SMS");
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented(signature, 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "No local SMS transport"};
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"copyMessageToIccEf", "(Ljava/lang/String;I[B[B)Z"},
             std::pair{"updateMessageOnIccEf", "(Ljava/lang/String;II[B)Z"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "RECEIVE_SMS");
            RequireServicePermission(context, "SEND_SMS");
            return dx::VmValue::Int(0);  // No ICC file handler.
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"disableCellBroadcast", "(I)Z"},
             std::pair{"disableCellBroadcastRange", "(II)Z"},
             std::pair{"enableCellBroadcast", "(I)Z"},
             std::pair{"enableCellBroadcastRange", "(II)Z"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "RECEIVE_SMS");
            return dx::VmValue::Int(0);  // No radio subscriptions.
        });
    }
    builder.VirtualMethod("getAllMessagesFromIccEf", "(Ljava/lang/String;)Ljava/util/List;",
                          [context](dx::IntrinsicContext& call) {
                              RequireServicePermission(context, "RECEIVE_SMS");
                              return dx::VmValue::Ref(NewPlatformObject(call.vm, "Ljava/util/ArrayList;", "()V", {}));
                          });
    builder.VirtualMethod("isImsSmsSupported", "()Z", [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.VirtualMethod("getImsSmsFormat", "()Ljava/lang/String;",
                          [](dx::IntrinsicContext& call) { return MakeString(call, "unknown"); });
    builder.VirtualMethod("getPremiumSmsPermission", "(Ljava/lang/String;)I", [context](dx::IntrinsicContext& call) {
        const auto package = dx::IntrinsicCall(call).NonNullRef(0, "package");
        if (call.vm.StringUtf8(package) != context->package_name)
            throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "SMS policy package UID mismatch"};
        return dx::VmValue::Int(0);  // PREMIUM_SMS_PERMISSION_UNKNOWN.
    });
    DenyServiceMethod(builder, "setPremiumSmsPermission", "(Ljava/lang/String;I)V", "MODIFY_PHONE_STATE");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_telephony_TelephonyManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_local_PhoneSubscriberService_Class(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalPhoneSubscriberService;",
                                                    "Lcom/android/internal/telephony/IPhoneSubInfo$Stub;");
    builder.VirtualMethod("getDeviceId", "()Ljava/lang/String;", [context](dx::IntrinsicContext& call) {
        RequireServicePermission(context, "READ_PHONE_STATE");
        return MakeString(call, context->device_id);
    });
    builder.VirtualMethod("getDeviceSvn", "()Ljava/lang/String;", [context](dx::IntrinsicContext& call) {
        RequireServicePermission(context, "READ_PHONE_STATE");
        return MakeString(call, context->device_software_version);
    });
    builder.VirtualMethod("getLine1Number", "()Ljava/lang/String;", [context](dx::IntrinsicContext& call) {
        RequireServicePermission(context, "READ_PHONE_STATE");
        return MakeString(call, context->line_number);
    });
    builder.VirtualMethod("getSubscriberId", "()Ljava/lang/String;", [context](dx::IntrinsicContext&) {
        RequireServicePermission(context, "READ_PHONE_STATE");
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getCompleteVoiceMailNumber", "()Ljava/lang/String;"},
         }) {
        DenyServiceMethod(builder, name, descriptor, "CALL_PRIVILEGED");
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getIsimDomain", "()Ljava/lang/String;"},
             std::pair{"getIsimImpi", "()Ljava/lang/String;"},
             std::pair{"getIsimImpu", "()[Ljava/lang/String;"},
         }) {
        DenyServiceMethod(builder, name, descriptor, "READ_PRIVILEGED_PHONE_STATE");
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getGroupIdLevel1", "()Ljava/lang/String;"},
             std::pair{"getIccSerialNumber", "()Ljava/lang/String;"},
             std::pair{"getLine1AlphaTag", "()Ljava/lang/String;"},
             std::pair{"getMsisdn", "()Ljava/lang/String;"},
             std::pair{"getVoiceMailAlphaTag", "()Ljava/lang/String;"},
             std::pair{"getVoiceMailNumber", "()Ljava/lang/String;"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext&) {
            RequireServicePermission(context, "READ_PHONE_STATE");
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    }
    return std::move(builder).Build();
}

Decl Declare_local_TelephonyService_Class(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalTelephonyService;",
                                                    "Lcom/android/internal/telephony/ITelephony$Stub;");
    builder.VirtualMethod("getActivePhoneType", "()I", [context](dx::IntrinsicContext&) {
        return dx::VmValue::Int(context->device_state.telephony.phone_type);
    });
    for (const auto& [name, descriptor] : std::array{
             std::pair{"answerRingingCall", "()V"},
             std::pair{"cancelMissedCallsNotification", "()V"},
             std::pair{"disableApnType", "(Ljava/lang/String;)I"},
             std::pair{"disableDataConnectivity", "()Z"},
             std::pair{"disableLocationUpdates", "()V"},
             std::pair{"enableApnType", "(Ljava/lang/String;)I"},
             std::pair{"enableDataConnectivity", "()Z"},
             std::pair{"enableLocationUpdates", "()V"},
             std::pair{"handlePinMmi", "(Ljava/lang/String;)Z"},
             std::pair{"merge", "()V"},
             std::pair{"mute", "(Z)V"},
             std::pair{"playDtmfTone", "(CZ)V"},
             std::pair{"setCellInfoListRate", "(I)V"},
             std::pair{"setRadio", "(Z)Z"},
             std::pair{"setRadioPower", "(Z)Z"},
             std::pair{"silenceRinger", "()V"},
             std::pair{"stopDtmfTone", "()V"},
             std::pair{"supplyPin", "(Ljava/lang/String;)Z"},
             std::pair{"supplyPinReportResult", "(Ljava/lang/String;)[I"},
             std::pair{"supplyPuk", "(Ljava/lang/String;Ljava/lang/String;)Z"},
             std::pair{"supplyPukReportResult", "(Ljava/lang/String;Ljava/lang/String;)[I"},
             std::pair{"swap", "()V"},
             std::pair{"toggleHold", "()V"},
             std::pair{"toggleRadioOnOff", "()V"},
             std::pair{"updateServiceLocation", "()V"},
         }) {
        DenyServiceMethod(builder, name, descriptor, "MODIFY_PHONE_STATE");
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"call", "(Ljava/lang/String;Ljava/lang/String;)V"},
             std::pair{"endCall", "()Z"},
         }) {
        const auto signature = std::string("com.android.internal.telephony.ITelephony.") + name + descriptor;
        builder.VirtualMethod(name, descriptor, [context, signature](dx::IntrinsicContext& call) -> dx::VmValue {
            RequireServicePermission(context, "CALL_PHONE");
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented(signature, 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", signature};
        });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"dial", "(Ljava/lang/String;)V"},
         }) {
        RejectUnsupportedServiceMethod(builder, "com.android.internal.telephony.ITelephony", name, descriptor);
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getCdmaEriIconIndex", "()I"},
             std::pair{"getCdmaEriIconMode", "()I"},
             std::pair{"getVoiceMessageCount", "()I"},
             std::pair{"getLteOnCdmaMode", "()I"},
         }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext&) { return dx::VmValue::Int(-1); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getCdmaEriText", "()Ljava/lang/String;"},
         }) {
        builder.VirtualMethod(name, descriptor,
                              [](dx::IntrinsicContext&) { return dx::VmValue::Ref(dx::VmObjectRef{}); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getDataActivity", "()I"},
             std::pair{"getDataNetworkType", "()I"},
             std::pair{"getNetworkType", "()I"},
             std::pair{"getVoiceNetworkType", "()I"},
             std::pair{"hasIccCard", "()Z"},
             std::pair{"isDataConnectivityPossible", "()Z"},
             std::pair{"isOffhook", "()Z"},
             std::pair{"isRadioOn", "()Z"},
             std::pair{"isRinging", "()Z"},
             std::pair{"isSimPinEnabled", "()Z"},
             std::pair{"needsOtaServiceProvisioning", "()Z"},
             std::pair{"showCallScreen", "()Z"},
             std::pair{"showCallScreenWithDialpad", "(Z)Z"},
         }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"isIdle", "()Z"},
         }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); });
    }
    for (const auto& [name, descriptor] : std::array{
             std::pair{"getAllCellInfo", "()Ljava/util/List;"},
             std::pair{"getNeighboringCellInfo", "(Ljava/lang/String;)Ljava/util/List;"},
         }) {
        builder.VirtualMethod(name, descriptor, [context](dx::IntrinsicContext& call) {
            RequireServicePermission(context, "ACCESS_COARSE_LOCATION");
            return dx::VmValue::Ref(NewPlatformObject(call.vm, "Ljava/util/ArrayList;", "()V", {}));
        });
    }

    builder.VirtualMethod("getCallState", "()I", [context](dx::IntrinsicContext&) {
        return dx::VmValue::Int(context->device_state.telephony.call_state);
    });
    builder.VirtualMethod("getDataState", "()I", [context](dx::IntrinsicContext&) {
        return dx::VmValue::Int(context->device_state.telephony.data_state);
    });
    builder.VirtualMethod("getCellLocation", "()Landroid/os/Bundle;", [context](dx::IntrinsicContext& call) {
        RequireServicePermission(context, "ACCESS_COARSE_LOCATION");
        return dx::VmValue::Ref(NewAndroidBundle(call.vm));
    });
    builder.VirtualMethod(
        "addListener", "(Lcom/android/internal/telephony/ITelephonyListener;)V", [context](dx::IntrinsicContext& call) {
            context->device_state.telephony.call_listeners.insert(dx::IntrinsicCall(call).NonNullRef(0, "listener"));
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("removeListener", "(Lcom/android/internal/telephony/ITelephonyListener;)V",
                          [context](dx::IntrinsicContext& call) {
                              context->device_state.telephony.call_listeners.erase(call.arguments[0].ref);
                              return dx::VmValue::Void();
                          });
    return std::move(builder).Build();
}

Decl Declare_local_TelephonyRegistry_Class(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalTelephonyRegistry;",
                                                    "Lcom/android/internal/telephony/ITelephonyRegistry$Stub;");
    builder.VirtualMethod("listen", "(Ljava/lang/String;Lcom/android/internal/telephony/IPhoneStateListener;IZ)V",
                          [context](dx::IntrinsicContext& call) {
                              const auto callback = dx::IntrinsicCall(call).NonNullRef(1, "callback");
                              const auto events = call.arguments[2].AsInt();
                              if (events == 0) {
                                  context->telephony_listeners.erase(callback);
                                  return dx::VmValue::Void();
                              }
                              constexpr int kLocationEvents = 0x410;
                              constexpr int kPhoneStateEvents = 0xec;
                              if (events & kLocationEvents) RequireServicePermission(context, "ACCESS_COARSE_LOCATION");
                              if (events & kPhoneStateEvents) RequireServicePermission(context, "READ_PHONE_STATE");
                              context->telephony_listeners.insert_or_assign(callback, events);
                              if (!call.arguments[3].AsInt()) return dx::VmValue::Void();

                              auto& vm = call.vm;
                              const auto dispatch = [&](int event, const char* name, const char* signature,
                                                        std::vector<dx::VmValue> arguments) {
                                  const auto record = context->telephony_listeners.find(callback);
                                  if (record != context->telephony_listeners.end() && (record->second & event))
                                      static_cast<void>(
                                          CallAndroidMethod(vm, callback, name, signature, std::move(arguments)));
                              };
                              if (events & 1) {
                                  const auto service =
                                      NewPlatformObject(vm, "Landroid/telephony/ServiceState;", "()V", {});
                                  static_cast<void>(CallAndroidMethod(vm, service, "setStateOutOfService", "()V"));
                                  dispatch(1, "onServiceStateChanged", "(Landroid/telephony/ServiceState;)V",
                                           {dx::VmValue::Ref(service)});
                              }
                              const auto zero = dx::VmValue::Int(0);
                              dispatch(2, "onSignalStrengthChanged", "(I)V", {dx::VmValue::Int(-1)});
                              dispatch(4, "onMessageWaitingIndicatorChanged", "(Z)V", {zero});
                              dispatch(8, "onCallForwardingIndicatorChanged", "(Z)V", {zero});
                              if (events & 16)
                                  dispatch(16, "onCellLocationChanged", "(Landroid/os/Bundle;)V",
                                           {dx::VmValue::Ref(NewAndroidBundle(vm))});
                              const auto& state = context->device_state.telephony;
                              if (events & 32)
                                  dispatch(32, "onCallStateChanged", "(ILjava/lang/String;)V",
                                           {dx::VmValue::Int(state.call_state), MakeString(call, "")});
                              dispatch(64, "onDataConnectionStateChanged", "(II)V",
                                       {dx::VmValue::Int(state.data_state), zero});
                              dispatch(128, "onDataActivity", "(I)V", {zero});
                              if (events & 256)
                                  dispatch(256, "onSignalStrengthsChanged", "(Landroid/telephony/SignalStrength;)V",
                                           {dx::VmValue::Ref(NewPlatformObject(vm, "Landroid/telephony/SignalStrength;",
                                                                               "()V", {}))});
                              dispatch(512, "onOtaspChanged", "(I)V", {zero});
                              dispatch(1024, "onCellInfoChanged", "(Ljava/util/List;)V",
                                       {dx::VmValue::Ref(dx::VmObjectRef{})});
                              return dx::VmValue::Void();
                          });
    for (
        const auto& [name, descriptor] : std::array{
            std::pair{"notifyCallForwardingChanged", "(Z)V"},
            std::pair{"notifyCallState", "(ILjava/lang/String;)V"},
            std::pair{"notifyCellInfo", "(Ljava/util/List;)V"},
            std::pair{"notifyCellLocation", "(Landroid/os/Bundle;)V"},
            std::pair{"notifyDataActivity", "(I)V"},
            std::pair{
                "notifyDataConnection",
                "(IZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Landroid/net/LinkProperties;Landroid/net/LinkCapabilities;IZ)V"},
            std::pair{"notifyDataConnectionFailed", "(Ljava/lang/String;Ljava/lang/String;)V"},
            std::pair{"notifyMessageWaitingChanged", "(Z)V"},
            std::pair{"notifyOtaspChanged", "(I)V"},
            std::pair{"notifyServiceState", "(Landroid/telephony/ServiceState;)V"},
            std::pair{"notifySignalStrength", "(Landroid/telephony/SignalStrength;)V"},
        }) {
        builder.VirtualMethod(name, descriptor, [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    }
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
