#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_text_TextWatcher(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/text/TextWatcher;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
