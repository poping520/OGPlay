// Bundle handlers store key/value pairs in the per-session bundle map
// keyed by the receiver object handle.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Bundle(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/os/Bundle;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V",
        [context](dx::IntrinsicContext& call) {
            context->bundles.try_emplace(call.receiver.Value());
            return dx::VmValue::Void();
        });
    builder.Virtual("get", "(Ljava/lang/String;)Ljava/lang/Object;",
        [context](dx::IntrinsicContext& call) {
            const auto bundle = context->bundles.find(call.receiver.Value());
            if (bundle == context->bundles.end()) {
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            const auto value =
                bundle->second.find(call.vm.StringUtf8(call.arguments[0].ref));
            if (value == bundle->second.end()) {
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            if (const auto* text = std::get_if<std::string>(&value->second)) {
                return MakeString(call, *text);
            }
            if (const auto* object =
                    std::get_if<dx::VmObjectRef>(&value->second)) {
                return dx::VmValue::Ref(*object);
            }
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.Virtual("getInt", "(Ljava/lang/String;)I",
        [context](dx::IntrinsicContext& call) {
            const auto& values = context->bundles[call.receiver.Value()];
            const auto found =
                values.find(call.vm.StringUtf8(call.arguments[0].ref));
            const auto* value =
                found == values.end()
                    ? nullptr
                    : std::get_if<std::int32_t>(&found->second);
            return dx::VmValue::Int(value == nullptr ? 0 : *value);
        });
    builder.Virtual("getString", "(Ljava/lang/String;)Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            const auto& values = context->bundles[call.receiver.Value()];
            const auto found =
                values.find(call.vm.StringUtf8(call.arguments[0].ref));
            const auto* value =
                found == values.end()
                    ? nullptr
                    : std::get_if<std::string>(&found->second);
            return value == nullptr ? dx::VmValue::Ref(dx::VmObjectRef{})
                                    : MakeString(call, *value);
        });
    builder.Virtual("putString", "(Ljava/lang/String;Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            context->bundles[call.receiver.Value()]
                            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.vm.StringUtf8(call.arguments[1].ref);
            return dx::VmValue::Void();
        });
    builder.Virtual("putInt", "(Ljava/lang/String;I)V",
        [context](dx::IntrinsicContext& call) {
            context->bundles[call.receiver.Value()]
                            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.arguments[1].AsInt();
            return dx::VmValue::Void();
        });
    builder.Virtual("getLong", "(Ljava/lang/String;)J",
        [context](dx::IntrinsicContext& call) {
            const auto& values = context->bundles[call.receiver.Value()];
            const auto found =
                values.find(call.vm.StringUtf8(call.arguments[0].ref));
            const auto* value =
                found == values.end()
                    ? nullptr
                    : std::get_if<std::int64_t>(&found->second);
            return dx::VmValue::Long(value == nullptr ? 0 : *value);
        });
    builder.Virtual("putLong", "(Ljava/lang/String;J)V",
        [context](dx::IntrinsicContext& call) {
            context->bundles[call.receiver.Value()]
                            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.arguments[1].AsLong();
            return dx::VmValue::Void();
        });
    builder.Virtual("getByteArray", "(Ljava/lang/String;)[B",
        [context](dx::IntrinsicContext& call) {
            const auto& values = context->bundles[call.receiver.Value()];
            const auto found =
                values.find(call.vm.StringUtf8(call.arguments[0].ref));
            const auto* value =
                found == values.end()
                    ? nullptr
                    : std::get_if<dx::VmObjectRef>(&found->second);
            return dx::VmValue::Ref(value == nullptr ? dx::VmObjectRef{}
                                                     : *value);
        });
    builder.Virtual("putByteArray", "(Ljava/lang/String;[B)V",
        [context](dx::IntrinsicContext& call) {
            context->bundles[call.receiver.Value()]
                            [call.vm.StringUtf8(call.arguments[0].ref)] =
                call.arguments[1].ref;
            return dx::VmValue::Void();
        });
    builder.Virtual("containsKey", "(Ljava/lang/String;)Z",
        [context](dx::IntrinsicContext& call) {
            const auto& values = context->bundles[call.receiver.Value()];
            return dx::VmValue::Int(
                values.contains(call.vm.StringUtf8(call.arguments[0].ref))
                    ? 1
                    : 0);
        });
    builder.Virtual("clear", "()V",
        [context](dx::IntrinsicContext& call) {
            context->bundles[call.receiver.Value()].clear();
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
