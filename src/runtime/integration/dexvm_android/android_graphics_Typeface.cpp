#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Typeface(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Typeface;", "Ljava/lang/Object;");
    builder.StaticField("SERIF", "Landroid/graphics/Typeface;");
    builder.StaticMethod("defaultFromStyle", "(I)Landroid/graphics/Typeface;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Landroid/graphics/Typeface;"));
        });
    builder.ClassInitializer([](dx::IntrinsicContext& call) {
        auto& vm = call.vm;
        vm.SetIntrinsicStaticRef(
            "Landroid/graphics/Typeface;", "SERIF",
            "Landroid/graphics/Typeface;",
            vm.NewIntrinsicInstance("Landroid/graphics/Typeface;"));
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
