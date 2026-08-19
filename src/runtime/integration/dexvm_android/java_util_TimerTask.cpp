#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_util_TimerTask(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/util/TimerTask;", "Ljava/lang/Object;", {"Ljava/lang/Runnable;"});
    builder.Constructor("()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
