// Intent handlers keep component targets and typed extras in the session
// context maps; flag/category setters are fluent no-ops.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_Intent(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/content/Intent;");
    builder.Super("Ljava/lang/Object;");
    const auto intent_init = [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    };
    const auto set_flags = [](dx::IntrinsicContext& call) {
        return Self(call);
    };
    builder.Virtual("<init>", "(Ljava/lang/String;)V", intent_init);
    builder.Virtual("<init>", "()V", intent_init);
    builder.Virtual("<init>", "(Ljava/lang/String;Landroid/net/Uri;)V",
        intent_init);
    builder.Virtual("<init>", "(Landroid/content/Context;Ljava/lang/Class;)V",
        [context](dx::IntrinsicContext& call) {
            const auto class_object = call.arguments[1].ref;
            const auto target = call.vm.Model().ClassOfClassObject(class_object);
            context->intent_components[call.receiver.Value()] =
                call.vm.Linker().Class(target).descriptor;
            return dx::VmValue::Void();
        });
    builder.Virtual("setClassName",
        "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
        [context](dx::IntrinsicContext& call) {
            auto dotted = call.vm.StringUtf8(call.arguments[1].ref);
            std::string descriptor = "L";
            for (const auto unit : dotted) {
                descriptor.push_back(unit == '.' ? '/' : unit);
            }
            descriptor.push_back(';');
            context->intent_components[call.receiver.Value()] =
                std::move(descriptor);
            return Self(call);
        });
    builder.Virtual("addFlags", "(I)Landroid/content/Intent;", set_flags);
    builder.Virtual("putExtra",
        "(Ljava/lang/String;I)Landroid/content/Intent;",
        [context](dx::IntrinsicContext& call) {
            context->intent_int_extras[call.receiver.Value()]
                [call.vm.StringUtf8(call.arguments[0].ref)] =
                    call.arguments[1].AsInt();
            return Self(call);
        });
    builder.Virtual("putExtra",
        "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
        [context](dx::IntrinsicContext& call) {
            context->intent_string_extras[call.receiver.Value()]
                [call.vm.StringUtf8(call.arguments[0].ref)] =
                    call.vm.StringUtf8(call.arguments[1].ref);
            return Self(call);
        });
    builder.Virtual("getStringExtra",
        "(Ljava/lang/String;)Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            const auto extras =
                context->intent_string_extras.find(call.receiver.Value());
            if (extras != context->intent_string_extras.end()) {
                const auto found = extras->second.find(
                    call.vm.StringUtf8(call.arguments[0].ref));
                if (found != extras->second.end()) {
                    return MakeString(call, found->second);
                }
            }
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.Virtual("getIntExtra", "(Ljava/lang/String;I)I",
        [context](dx::IntrinsicContext& call) {
            const auto extras =
                context->intent_int_extras.find(call.receiver.Value());
            if (extras != context->intent_int_extras.end()) {
                const auto found = extras->second.find(
                    call.vm.StringUtf8(call.arguments[0].ref));
                if (found != extras->second.end()) {
                    return dx::VmValue::Int(found->second);
                }
            }
            return dx::VmValue::Int(call.arguments[1].AsInt());
        });
    builder.Virtual("addCategory",
        "(Ljava/lang/String;)Landroid/content/Intent;", set_flags);
    builder.Virtual("getAction", "()Ljava/lang/String;",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.Virtual("getExtras", "()Landroid/os/Bundle;",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.Virtual("setFlags", "(I)Landroid/content/Intent;", set_flags);
    builder.Virtual("setDataAndType",
        "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;",
        [](dx::IntrinsicContext& call) { return Self(call); });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
