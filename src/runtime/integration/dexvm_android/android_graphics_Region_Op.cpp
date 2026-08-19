#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Region_Op(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Region$Op;", "Ljava/lang/Object;");
    builder.StaticField("REPLACE", "Landroid/graphics/Region$Op;");
    builder.ClassInitializer([](dx::IntrinsicContext& call) {
        auto& vm = call.vm;
        vm.SetIntrinsicStaticRef(
            "Landroid/graphics/Region$Op;", "REPLACE",
            "Landroid/graphics/Region$Op;",
            vm.NewIntrinsicInstance("Landroid/graphics/Region$Op;"));
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
