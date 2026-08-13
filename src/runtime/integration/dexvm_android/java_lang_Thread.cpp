#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_lang_Thread(const Context& context) {
    return DeclareAndroidClass(context, "Ljava/lang/Thread;");
}

}  // namespace ogplay::runtime::android_intrinsics
