// android.* intrinsic catalog for the dex_activity lifecycle. Entries are
// evidenced by the linker gap report or a real runtime hit (03 §6):
// anything outside the list stays an explicit, accounted failure. The
// Public declarations live in the per-class Declare_* translation units.

#include "shared.h"

namespace ogplay::runtime::android_intrinsics {

std::vector<Decl> RawAndroidIntrinsicCatalog() {
    std::vector<Decl> catalog;
    AppendCoreClasses(catalog);
    AppendIoClasses(catalog);
    AppendDeviceClasses(catalog);
    AppendGraphicsClasses(catalog);
    AppendWidgetClasses(catalog);
    return catalog;
}

}  // namespace ogplay::runtime::android_intrinsics
