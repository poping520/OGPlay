#include "catalog.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace ogplay::runtime::android_intrinsics {

namespace {

constexpr auto kPlayLicensingService = "Lorg/ogplay/framework/LocalPlayLicensingService;";
constexpr auto kPlayLicensingResponse = "Lorg/ogplay/framework/LocalPlayLicensingResponse;";
constexpr GooglePlaySystemPackage kSystemPackages[]{
    {"com.google.android.gsf", 10001, 19, "4.4.4", "com.google.uid.shared", 19,
     "/system/app/GoogleServicesFramework.apk", false},
    {kPlayStorePackage, 10002, 80290013, "4.9.13", "", 19, "/system/priv-app/Phonesky.apk", true},
};
constexpr GooglePlayService kServices[]{
    {"com.android.vending.licensing.LicensingService", "com.android.vending.licensing.ILicensingService",
     kPlayLicensingService, "com.android.vending.licensing.ILicensingService"},
    {"com.android.vending.billing.InAppBillingService", "com.android.vending.billing.InAppBillingService.BIND",
     "Lorg/ogplay/framework/LocalPlayBillingService;", "com.android.vending.billing.IInAppBillingService"},
    {"com.android.vending.billing.MarketBillingService", "com.android.vending.billing.MarketBillingService.BIND",
     "Lorg/ogplay/framework/LocalMarketBillingService;", "com.android.vending.billing.IMarketBillingService"},
};
constexpr auto kPlayLicensingAction = "com.android.vending.licensing.ILicensingService";
constexpr auto kPlayLicensingListener = "com.android.vending.licensing.ILicenseResultListener";
constexpr auto kBinderTransactSignature = "(ILandroid/os/Parcel;Landroid/os/Parcel;I)Z";
// Not a Play response code: the client's validator treats it as an invalid
// response and takes its normal dontAllow path without a signature check.
constexpr std::int32_t kPlayLicensingInvalidResponse = 999;

dx::VmValue CallBinderOnTransact(dx::IntrinsicContext& call) {
    auto& vm = call.vm;
    const auto binder = vm.Linker().ResolveDescriptor("Landroid/os/Binder;");
    const auto slot = vm.Linker().FindVtableIndex(binder, "onTransact", kBinderTransactSignature);
    if (!slot) throw std::logic_error("Binder.onTransact is unavailable");
    const auto outcome = vm.Call(vm.Linker().Class(binder).vtable[*slot],
                                 std::array{dx::VmValue::Ref(call.receiver), call.arguments[0], call.arguments[1],
                                            call.arguments[2], call.arguments[3]});
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
    return outcome.value;
}

dx::VmObjectRef ObtainParcel(dx::Interpreter& vm) {
    const auto method = vm.Linker().FindDirectMethod(vm.Linker().ResolveDescriptor("Landroid/os/Parcel;"), "obtain",
                                                     "()Landroid/os/Parcel;");
    if (!method) throw std::logic_error("Parcel.obtain is unavailable");
    const auto outcome = vm.Call(*method, {});
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
    return outcome.value.ref;
}

}  // namespace

std::span<const GooglePlaySystemPackage> GooglePlaySystemPackages(const Context& context) {
    if (!context->google_play) return {};
    return kSystemPackages;
}

const GooglePlaySystemPackage* FindGooglePlaySystemPackage(const Context& context, std::string_view name) {
    for (const auto& package : GooglePlaySystemPackages(context))
        if (package.name == name) return &package;
    return nullptr;
}

const GooglePlayService* MatchGooglePlayService(dx::Interpreter& vm, dx::VmObjectRef intent) {
    const auto component = CallAndroidMethod(vm, intent, "getComponent", "()Landroid/content/ComponentName;").ref;
    const auto root = vm.ProtectReferences(std::array{component});
    if (component.IsValid()) {
        const auto package = CallAndroidMethod(vm, component, "getPackageName", "()Ljava/lang/String;").ref;
        if (vm.StringUtf8(package) != kPlayStorePackage) return nullptr;
        const auto name = vm.StringUtf8(CallAndroidMethod(vm, component, "getClassName", "()Ljava/lang/String;").ref);
        for (const auto& service : kServices)
            if (service.name == name) return &service;
        return nullptr;
    }
    const auto action = CallAndroidMethod(vm, intent, "getAction", "()Ljava/lang/String;").ref;
    const auto action_name = action.IsValid() ? vm.StringUtf8(action) : "";
    const auto package = CallAndroidMethod(vm, intent, "getPackage", "()Ljava/lang/String;").ref;
    if (package.IsValid() && vm.StringUtf8(package) != kPlayStorePackage) return nullptr;
    for (const auto& [name, signature] :
         {std::pair{"getData", "()Landroid/net/Uri;"}, std::pair{"getType", "()Ljava/lang/String;"},
          std::pair{"getCategories", "()Ljava/util/Set;"}, std::pair{"getSelector", "()Landroid/content/Intent;"}}) {
        if (CallAndroidMethod(vm, intent, name, signature).ref.IsValid()) return nullptr;
    }
    for (const auto& service : kServices)
        if (service.action == action_name) return &service;
    return nullptr;
}

bool IsGooglePlayStoreIntent(dx::Interpreter& vm, dx::VmObjectRef intent, bool launch) {
    const auto data = CallAndroidMethod(vm, intent, "getData", "()Landroid/net/Uri;").ref;
    if (!data.IsValid()) return false;
    const auto root = vm.ProtectReferences(std::array{data});
    const auto scheme = CallAndroidMethod(vm, data, "getScheme", "()Ljava/lang/String;").ref;
    if (!scheme.IsValid() || vm.StringUtf8(scheme) != "market") return false;
    const auto component = CallAndroidMethod(vm, intent, "getComponent", "()Landroid/content/ComponentName;").ref;
    const auto component_root = vm.ProtectReferences(std::array{component});
    if (component.IsValid()) {
        const auto package = CallAndroidMethod(vm, component, "getPackageName", "()Ljava/lang/String;").ref;
        if (vm.StringUtf8(package) != kPlayStorePackage) return false;
        if (launch) return true;
        return vm.StringUtf8(CallAndroidMethod(vm, component, "getClassName", "()Ljava/lang/String;").ref) ==
               kPlayStoreActivity;
    }
    const auto package = CallAndroidMethod(vm, intent, "getPackage", "()Ljava/lang/String;").ref;
    if (package.IsValid() && vm.StringUtf8(package) != kPlayStorePackage) return false;
    if (launch && package.IsValid()) return true;
    const auto action = CallAndroidMethod(vm, intent, "getAction", "()Ljava/lang/String;").ref;
    if (!action.IsValid() || vm.StringUtf8(action) != "android.intent.action.VIEW") return false;
    if (launch) return true;
    if (CallAndroidMethod(vm, intent, "getType", "()Ljava/lang/String;").ref.IsValid() ||
        CallAndroidMethod(vm, intent, "getSelector", "()Landroid/content/Intent;").ref.IsValid())
        return false;
    const auto categories = CallAndroidMethod(vm, intent, "getCategories", "()Ljava/util/Set;").ref;
    const auto categories_root = vm.ProtectReferences(std::array{categories});
    if (categories.IsValid()) {
        const auto iterator = CallAndroidMethod(vm, categories, "iterator", "()Ljava/util/Iterator;").ref;
        const auto iterator_root = vm.ProtectReferences(std::array{iterator});
        while (CallAndroidMethod(vm, iterator, "hasNext", "()Z").AsInt()) {
            const auto category = vm.StringUtf8(CallAndroidMethod(vm, iterator, "next", "()Ljava/lang/Object;").ref);
            if (category != "android.intent.category.DEFAULT" && category != "android.intent.category.BROWSABLE")
                return false;
        }
    }
    return true;
}

Decl Declare_local_PlayLicensingService_Class(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(kPlayLicensingService, "Landroid/os/Binder;");
    builder.VirtualMethod("onTransact", kBinderTransactSignature, [context](dx::IntrinsicContext& call) {
        auto& vm = call.vm;
        // TRANSACTION_checkLicense: the generated Stub.Proxy writes the nonce,
        // packageName and the result listener after the interface token.
        if (call.arguments[0].AsInt() != 1) return CallBinderOnTransact(call);
        const auto data = dx::IntrinsicCall(call).NonNullRef(1, "data");
        const auto token = vm.NewStringUtf8(kPlayLicensingAction);
        static_cast<void>(
            CallAndroidMethod(vm, data, "enforceInterface", "(Ljava/lang/String;)V", {dx::VmValue::Ref(token)}));
        static_cast<void>(CallAndroidMethod(vm, data, "readLong", "()J"));
        static_cast<void>(CallAndroidMethod(vm, data, "readString", "()Ljava/lang/String;"));
        const auto listener = CallAndroidMethod(vm, data, "readStrongBinder", "()Landroid/os/IBinder;").ref;
        if (!listener.IsValid()) return dx::VmValue::Int(1);
        const auto listener_root = vm.ProtectReferences(std::array{listener});
        const auto response =
            NewPlatformObject(vm, kPlayLicensingResponse, "(Landroid/os/IBinder;)V", {dx::VmValue::Ref(listener)});
        const auto root = vm.ProtectReferences(std::array{response});
        auto& handler = context->singletons["play_licensing_handler"];
        if (!handler.IsValid()) {
            const auto looper = EnsureMainLooper(vm, context);
            handler =
                NewPlatformObject(vm, "Landroid/os/Handler;", "(Landroid/os/Looper;)V", {dx::VmValue::Ref(looper)});
        }
        return CallAndroidMethod(vm, handler, "post", "(Ljava/lang/Runnable;)Z", {dx::VmValue::Ref(response)});
    });
    return std::move(builder).Build();
}

Decl Declare_local_PlayLicensingResponse_Class(const Context&) {
    auto builder =
        dx::IntrinsicClassBuilder::Class(kPlayLicensingResponse, "Ljava/lang/Object;", {"Ljava/lang/Runnable;"});
    const auto listener = builder.BoundInstanceField("listener", "Landroid/os/IBinder;", dx::kAccPrivate);
    builder.Constructor("(Landroid/os/IBinder;)V", [=](dx::IntrinsicContext& call) {
        dx::IntrinsicCall fields(call);
        fields.SetRef(listener, call.arguments[0].ref);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("run", "()V", [=](dx::IntrinsicContext& call) {
        auto& vm = call.vm;
        dx::IntrinsicCall fields(call);
        const auto target = fields.GetRef(listener);
        if (!target.IsValid()) return dx::VmValue::Void();
        const auto parcel = ObtainParcel(vm);
        const auto roots = vm.ProtectReferences(std::array{target, parcel});
        const auto recycle = [&] { static_cast<void>(CallAndroidMethod(vm, parcel, "recycle", "()V")); };
        try {
            const auto token = vm.NewStringUtf8(kPlayLicensingListener);
            static_cast<void>(CallAndroidMethod(vm, parcel, "writeInterfaceToken", "(Ljava/lang/String;)V",
                                                {dx::VmValue::Ref(token)}));
            static_cast<void>(
                CallAndroidMethod(vm, parcel, "writeInt", "(I)V", {dx::VmValue::Int(kPlayLicensingInvalidResponse)}));
            const auto empty = vm.NewStringUtf8("");
            const auto empty_root = vm.ProtectReferences(std::array{empty});
            static_cast<void>(
                CallAndroidMethod(vm, parcel, "writeString", "(Ljava/lang/String;)V", {dx::VmValue::Ref(empty)}));
            static_cast<void>(
                CallAndroidMethod(vm, parcel, "writeString", "(Ljava/lang/String;)V", {dx::VmValue::Ref(empty)}));
            const auto delivered = CallAndroidMethod(vm, target, "transact", kBinderTransactSignature,
                                                     {dx::VmValue::Int(1), dx::VmValue::Ref(parcel),
                                                      dx::VmValue::Ref(dx::VmObjectRef{}), dx::VmValue::Int(1)});
            if (auto* logger = vm.Log()) {
                logger->Write(
                    delivered.AsInt() ? core::LogLevel::info : core::LogLevel::warn, "runtime.android.play_licensing",
                    delivered.AsInt() ? "delivered fixed LVL failure response" : "LVL listener declined response", {},
                    {{"response_code", std::to_string(kPlayLicensingInvalidResponse)}},
                    {.mode = core::RateLimitMode::first_and_count});
            }
        } catch (const dx::VmJavaThrow& error) {
            if (auto* logger = vm.Log())
                logger->Write(core::LogLevel::warn, "runtime.android.play_licensing", error.message);
        } catch (...) {
            recycle();
            throw;
        }
        recycle();
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

dx::VmObjectRef ConstructGooglePlayBinder(dx::Interpreter& vm, const GooglePlayService& service) {
    const auto binder = vm.NewIntrinsicInstance(service.binder_type);
    const auto root = vm.ProtectReferences(std::array{binder});
    const auto parent = vm.Linker().ResolveDescriptor("Landroid/os/Binder;");
    const auto init = vm.Linker().FindDirectMethod(parent, "<init>", "()V");
    if (!init) throw std::logic_error("Binder constructor is unavailable");
    const auto outcome = vm.Call(*init, std::array{dx::VmValue::Ref(binder)});
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
    // API19 Binder.queryLocalInterface assumes mDescriptor is non-null. Attach
    // no local interface so the guest's generated proxy sends the transaction
    // to onTransact below instead of casting a local object.
    static_cast<void>(CallAndroidMethod(
        vm, binder, "attachInterface", "(Landroid/os/IInterface;Ljava/lang/String;)V",
        {dx::VmValue::Ref(dx::VmObjectRef{}), dx::VmValue::Ref(vm.NewStringUtf8(service.descriptor))}));
    return binder;
}

dx::VmObjectRef NewGooglePlayComponentName(dx::Interpreter& vm, std::string_view name) {
    const auto package = vm.NewStringUtf8(kPlayStorePackage);
    const auto package_root = vm.ProtectReferences(std::array{package});
    const auto service = vm.NewStringUtf8(name);
    const auto service_root = vm.ProtectReferences(std::array{service});
    return NewPlatformObject(vm, "Landroid/content/ComponentName;", "(Ljava/lang/String;Ljava/lang/String;)V",
                             {dx::VmValue::Ref(package), dx::VmValue::Ref(service)});
}

namespace {

Decl DeclareBillingService(const GooglePlayService& service, bool legacy) {
    auto builder = dx::IntrinsicClassBuilder::Class(std::string(service.binder_type), "Landroid/os/Binder;");
    builder.VirtualMethod("onTransact", kBinderTransactSignature, [service, legacy](dx::IntrinsicContext& call) {
        const auto transaction = call.arguments[0].AsInt();
        if (transaction < 1 || transaction > (legacy ? 1 : 5)) return CallBinderOnTransact(call);
        auto& vm = call.vm;
        const auto data = dx::IntrinsicCall(call).NonNullRef(1, "data");
        const auto reply = dx::IntrinsicCall(call).NonNullRef(2, "reply");
        const auto token = vm.NewStringUtf8(service.descriptor);
        static_cast<void>(
            CallAndroidMethod(vm, data, "enforceInterface", "(Ljava/lang/String;)V", {dx::VmValue::Ref(token)}));
        static_cast<void>(CallAndroidMethod(vm, reply, "writeNoException", "()V"));
        const bool bundle_result = legacy || (transaction >= 2 && transaction <= 4);
        static_cast<void>(CallAndroidMethod(vm, reply, "writeInt", "(I)V", {dx::VmValue::Int(bundle_result ? 1 : 3)}));
        if (bundle_result) {
            const auto bundle = NewPlatformObject(vm, "Landroid/os/Bundle;", "()V", {});
            const auto root = vm.ProtectReferences(std::array{bundle});
            const auto key = vm.NewStringUtf8("RESPONSE_CODE");
            static_cast<void>(CallAndroidMethod(vm, bundle, "putInt", "(Ljava/lang/String;I)V",
                                                {dx::VmValue::Ref(key), dx::VmValue::Int(3)}));
            static_cast<void>(CallAndroidMethod(vm, bundle, "writeToParcel", "(Landroid/os/Parcel;I)V",
                                                {dx::VmValue::Ref(reply), dx::VmValue::Int(0)}));
        }
        if (auto* logger = vm.Log())
            logger->Write(
                core::LogLevel::info, "runtime.android.google_play", "billing unavailable", {},
                {{"component", std::string(service.name)},
                 {"method", legacy ? "sendBillingRequest"
                                   : std::array{"isBillingSupported", "getSkuDetails", "getBuyIntent", "getPurchases",
                                                "consumePurchase"}[static_cast<std::size_t>(transaction - 1)]},
                 {"response_code", "3"}},
                {.mode = core::RateLimitMode::first_and_count});
        return dx::VmValue::Int(1);
    });
    return std::move(builder).Build();
}

}  // namespace

Decl Declare_local_PlayBillingService_Class(const Context&) {
    return DeclareBillingService(kServices[1], false);
}

Decl Declare_local_MarketBillingService_Class(const Context&) {
    return DeclareBillingService(kServices[2], true);
}

}  // namespace ogplay::runtime::android_intrinsics
