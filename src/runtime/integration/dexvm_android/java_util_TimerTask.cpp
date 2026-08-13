#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_util_TimerTask(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Ljava/util/TimerTask;");
    builder.Super("Ljava/lang/Object;");
    builder.Implements("Ljava/lang/Runnable;");
    builder.Virtual("<init>", "()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
