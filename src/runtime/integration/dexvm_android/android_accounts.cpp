#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
namespace {
constexpr auto kAccounts = "[Landroid/accounts/Account;";

void RequireArgument(dx::IntrinsicContext& call, std::size_t index, const char* name) {
    if (!call.arguments[index].ref.IsValid())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", std::string(name) + " is null"};
}

void RequireAccountUser(dx::IntrinsicContext& call, std::size_t index) {
    if (call.arguments[index].AsInt() != 0)
        throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "account access outside the current user"};
}

dx::VmValue AccountError(dx::IntrinsicContext& call, int code, const char* message) {
    return CallAndroidMethod(call.vm, call.arguments[0].ref, "onError", "(ILjava/lang/String;)V",
                             {dx::VmValue::Int(code), MakeString(call, message)});
}
}  // namespace

Decl Declare_local_AccountService_Class(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalAccountService;",
                                                    "Landroid/accounts/IAccountManager$Stub;");
    builder.VirtualMethod(
        "getAuthenticatorTypes", "()[Landroid/accounts/AuthenticatorDescription;", [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(NewServiceArray(call.vm, "[Landroid/accounts/AuthenticatorDescription;", 0));
        });
    const auto read_accounts = [context](dx::IntrinsicContext& call) {
        RequireServicePermission(context, "GET_ACCOUNTS");
        return dx::VmValue::Ref(NewServiceArray(call.vm, kAccounts, 0));
    };
    builder.VirtualMethod("getAccounts", "(Ljava/lang/String;)[Landroid/accounts/Account;", read_accounts);
    builder.VirtualMethod("getAccountsAsUser", "(Ljava/lang/String;I)[Landroid/accounts/Account;",
                          [read_accounts](dx::IntrinsicContext& call) {
                              RequireAccountUser(call, 1);
                              return read_accounts(call);
                          });
    DenyServiceMethod(builder, "getAccountsByTypeForPackage",
                      "(Ljava/lang/String;Ljava/lang/String;)[Landroid/accounts/Account;", "INTERACT_ACROSS_USERS");
    builder.VirtualMethod("getSharedAccountsAsUser", "(I)[Landroid/accounts/Account;", [](dx::IntrinsicContext& call) {
        RequireAccountUser(call, 0);
        return dx::VmValue::Ref(NewServiceArray(call.vm, kAccounts, 0));
    });
    for (const auto& [name, desc] : std::array{
             std::pair{"getAccountsForPackage", "(Ljava/lang/String;I)[Landroid/accounts/Account;"},
             std::pair{"updateAppPermission", "(Landroid/accounts/Account;Ljava/lang/String;IZ)V"},
             std::pair{"getAuthTokenLabel",
                       "(Landroid/accounts/IAccountManagerResponse;Ljava/lang/String;Ljava/lang/String;)V"},
         }) {
        builder.VirtualMethod(
            name, desc,
            [is_label = std::string_view(name) == "getAuthTokenLabel"](dx::IntrinsicContext& call) -> dx::VmValue {
                if (is_label) {
                    RequireArgument(call, 1, "accountType");
                    RequireArgument(call, 2, "authTokenType");
                }
                throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "Only the system UID may call this method"};
            });
    }

    builder.VirtualMethod("invalidateAuthToken", "(Ljava/lang/String;Ljava/lang/String;)V",
                          [context](dx::IntrinsicContext& call) {
                              RequireArgument(call, 0, "accountType");
                              RequireArgument(call, 1, "authToken");
                              try {
                                  RequireServicePermission(context, "MANAGE_ACCOUNTS");
                              } catch (const dx::VmJavaThrow&) {
                                  RequireServicePermission(context, "USE_CREDENTIALS");
                              }
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod("clearPassword", "(Landroid/accounts/Account;)V", [context](dx::IntrinsicContext& call) {
        RequireArgument(call, 0, "account");
        RequireServicePermission(context, "MANAGE_ACCOUNTS");
        return dx::VmValue::Void();
    });
    for (const auto& [name, desc, extra] : std::array{
             std::tuple{"getPassword", "(Landroid/accounts/Account;)Ljava/lang/String;", ""},
             std::tuple{"getUserData", "(Landroid/accounts/Account;Ljava/lang/String;)Ljava/lang/String;", "key"},
             std::tuple{"addAccountExplicitly", "(Landroid/accounts/Account;Ljava/lang/String;Landroid/os/Bundle;)Z",
                        ""},
             std::tuple{"peekAuthToken", "(Landroid/accounts/Account;Ljava/lang/String;)Ljava/lang/String;",
                        "authTokenType"},
             std::tuple{"setAuthToken", "(Landroid/accounts/Account;Ljava/lang/String;Ljava/lang/String;)V",
                        "authTokenType"},
             std::tuple{"setPassword", "(Landroid/accounts/Account;Ljava/lang/String;)V", ""},
             std::tuple{"setUserData", "(Landroid/accounts/Account;Ljava/lang/String;Ljava/lang/String;)V", "key"},
         }) {
        builder.VirtualMethod(name, desc, [context, extra](dx::IntrinsicContext& call) -> dx::VmValue {
            RequireArgument(call, 0, "account");
            if (*extra) RequireArgument(call, 1, extra);
            RequireServicePermission(context, "AUTHENTICATE_ACCOUNTS");
            // API 19 also requires the caller to share the authenticator UID.
            throw dx::VmJavaThrow{"Ljava/lang/SecurityException;", "No authenticator matches the caller UID"};
        });
    }

    builder.VirtualMethod(
        "getAccountsByFeatures", "(Landroid/accounts/IAccountManagerResponse;Ljava/lang/String;[Ljava/lang/String;)V",
        [read_accounts](dx::IntrinsicContext& call) {
            RequireArgument(call, 0, "response");
            RequireArgument(call, 1, "accountType");
            const auto accounts = read_accounts(call).ref;
            const auto features = call.arguments[2].ref;
            if (features.IsValid() && call.vm.Model().ArrayLength(features) != 0)
                return AccountError(call, 1, "bind failure");
            const auto accounts_root = call.vm.ProtectReferences(std::array{accounts});
            const auto bundle = NewPlatformObject(call.vm, "Landroid/os/Bundle;", "()V", {});
            const auto bundle_root = call.vm.ProtectReferences(std::array{bundle});
            static_cast<void>(CallAndroidMethod(call.vm, bundle, "putParcelableArray",
                                                "(Ljava/lang/String;[Landroid/os/Parcelable;)V",
                                                {MakeString(call, "accounts"), dx::VmValue::Ref(accounts)}));
            return CallAndroidMethod(call.vm, call.arguments[0].ref, "onResult", "(Landroid/os/Bundle;)V",
                                     {dx::VmValue::Ref(bundle)});
        });
    builder.VirtualMethod(
        "getAuthToken",
        "(Landroid/accounts/IAccountManagerResponse;Landroid/accounts/Account;Ljava/lang/String;ZZLandroid/os/Bundle;)V",
        [context](dx::IntrinsicContext& call) {
            RequireArgument(call, 0, "response");
            if (!call.arguments[1].ref.IsValid()) return AccountError(call, 7, "account is null");
            if (!call.arguments[2].ref.IsValid()) return AccountError(call, 7, "authTokenType is null");
            RequireServicePermission(context, "USE_CREDENTIALS");
            return AccountError(call, 1, "bind failure");
        });
    for (
        const auto& [name, desc, permission, argument, extra, has_user] : std::array{
            std::tuple{"hasFeatures",
                       "(Landroid/accounts/IAccountManagerResponse;Landroid/accounts/Account;[Ljava/lang/String;)V",
                       "GET_ACCOUNTS", "account", "features", false},
            std::tuple{"removeAccount", "(Landroid/accounts/IAccountManagerResponse;Landroid/accounts/Account;)V",
                       "MANAGE_ACCOUNTS", "account", "", false},
            std::tuple{
                "addAccount",
                "(Landroid/accounts/IAccountManagerResponse;Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;ZLandroid/os/Bundle;)V",
                "MANAGE_ACCOUNTS", "accountType", "", false},
            std::tuple{
                "updateCredentials",
                "(Landroid/accounts/IAccountManagerResponse;Landroid/accounts/Account;Ljava/lang/String;ZLandroid/os/Bundle;)V",
                "MANAGE_ACCOUNTS", "account", "authTokenType", false},
            std::tuple{"editProperties", "(Landroid/accounts/IAccountManagerResponse;Ljava/lang/String;Z)V",
                       "MANAGE_ACCOUNTS", "accountType", "", false},
            std::tuple{"confirmCredentialsAsUser",
                       "(Landroid/accounts/IAccountManagerResponse;Landroid/accounts/Account;Landroid/os/Bundle;ZI)V",
                       "MANAGE_ACCOUNTS", "account", "", true},
        }) {
        builder.VirtualMethod(name, desc, [context, permission, argument, extra, has_user](dx::IntrinsicContext& call) {
            if (has_user) RequireAccountUser(call, 4);
            RequireArgument(call, 0, "response");
            RequireArgument(call, 1, argument);
            if (*extra) RequireArgument(call, 2, extra);
            RequireServicePermission(context, permission);
            return AccountError(call, 1, "bind failure");
        });
    }
    builder.VirtualMethod("removeSharedAccountAsUser", "(Landroid/accounts/Account;I)Z",
                          [](dx::IntrinsicContext& call) {
                              RequireAccountUser(call, 1);
                              static_cast<void>(dx::IntrinsicCall(call).NonNullRef(0, "account"));
                              return dx::VmValue::Int(0);
                          });
    builder.VirtualMethod(
        "addSharedAccountAsUser", "(Landroid/accounts/Account;I)Z", [](dx::IntrinsicContext& call) -> dx::VmValue {
            RequireAccountUser(call, 1);
            static_cast<void>(dx::IntrinsicCall(call).NonNullRef(0, "account"));
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.account.shared", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "Shared account storage is unavailable"};
        });
    return std::move(builder).Build();
}
}  // namespace ogplay::runtime::android_intrinsics
