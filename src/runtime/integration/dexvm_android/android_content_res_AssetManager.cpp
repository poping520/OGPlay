#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_res_AssetManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/res/AssetManager;", "Ljava/lang/Object;");
    builder.FinalMethod("open", "(Ljava/lang/String;)Ljava/io/InputStream;",
        [context](dx::IntrinsicContext& call) {
            const auto name = call.vm.StringUtf8(call.arguments[0].ref);
            return dx::VmValue::Ref(OpenStream(
                call, context, ReadApkFile(context, "assets/" + name)));
        });
    builder.FinalMethod("open", "(Ljava/lang/String;I)Ljava/io/InputStream;",
        [context](dx::IntrinsicContext& call) {
            const auto name = call.vm.StringUtf8(call.arguments[0].ref);
            return dx::VmValue::Ref(OpenStream(
                call, context, ReadApkFile(context, "assets/" + name)));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
