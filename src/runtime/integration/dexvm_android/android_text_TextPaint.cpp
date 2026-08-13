#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_text_TextPaint(const Context& context) {
    return DeclareAndroidClass(context, "Landroid/text/TextPaint;");
}

}  // namespace ogplay::runtime::android_intrinsics
