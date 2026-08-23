#include <set>
#include <string>

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_res_AssetManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/res/AssetManager;", "Ljava/lang/Object;");
    builder.FinalMethod("openFd",
        "(Ljava/lang/String;)Landroid/content/res/AssetFileDescriptor;",
        [context](dx::IntrinsicContext& call) {
            const auto path = "assets/" +
                              call.vm.StringUtf8(call.arguments[0].ref);
            const loader::ApkEntry* entry = nullptr;
            for (const auto& candidate : context->archive.entries) {
                if (candidate.name == path) {
                    entry = &candidate;
                    break;
                }
            }
            if (entry == nullptr) {
                throw dx::VmJavaThrow{
                    "Ljava/io/FileNotFoundException;",
                    "APK asset is unavailable: " + path};
            }
            const auto descriptor = call.vm.NewIntrinsicInstance(
                "Landroid/content/res/AssetFileDescriptor;");
            const auto length =
                static_cast<std::uint64_t>(entry->uncompressed_size);
            const auto slots = call.vm.Model().InstanceSlots(descriptor);
            slots[0] = {static_cast<std::uint32_t>(length),
                        dx::SlotTag::wide_lo};
            slots[1] = {static_cast<std::uint32_t>(length >> 32U),
                        dx::SlotTag::wide_hi};
            return dx::VmValue::Ref(descriptor);
        });
    builder.FinalMethod("list",
        "(Ljava/lang/String;)[Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            const auto requested =
                call.vm.StringUtf8(call.arguments[0].ref);
            const auto prefix = requested.empty()
                                    ? std::string{"assets/"}
                                    : "assets/" + requested + "/";
            std::set<std::string> children;
            for (const auto& entry : context->archive.entries) {
                if (!entry.name.starts_with(prefix)) continue;
                const auto remainder =
                    std::string_view(entry.name).substr(prefix.size());
                if (remainder.empty()) continue;
                const auto slash = remainder.find('/');
                children.emplace(remainder.substr(0, slash));
            }
            auto& vm = call.vm;
            const auto array_class =
                vm.Linker().ResolveDescriptor("[Ljava/lang/String;");
            const auto string_class =
                vm.Linker().ResolveDescriptor("Ljava/lang/String;");
            const auto array = vm.Model().NewObjectArray(
                array_class, string_class,
                static_cast<JniSize>(children.size()));
            JniSize index{};
            for (const auto& child : children) {
                vm.Model().SetObjectElement(
                    array, index++, vm.NewStringUtf8(child));
            }
            return dx::VmValue::Ref(array);
        });
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
