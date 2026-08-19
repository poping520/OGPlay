#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Bitmap_Config(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Bitmap$Config;", "Ljava/lang/Object;");
    builder.StaticField("ARGB_4444", "Landroid/graphics/Bitmap$Config;");
    builder.StaticField("ARGB_8888", "Landroid/graphics/Bitmap$Config;");
    builder.ClassInitializer([](dx::IntrinsicContext& call) {
        auto& vm = call.vm;
        for (const char* name : {"ARGB_4444", "ARGB_8888"}) {
            vm.SetIntrinsicStaticRef(
                "Landroid/graphics/Bitmap$Config;", name,
                "Landroid/graphics/Bitmap$Config;",
                vm.NewIntrinsicInstance("Landroid/graphics/Bitmap$Config;"));
        }
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
