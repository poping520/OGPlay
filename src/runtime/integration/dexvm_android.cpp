// android.* intrinsic catalog for the dex_activity lifecycle. Entries are
// evidenced by the linker gap report or a real runtime hit (03 §6):
// anything outside the list stays an explicit, accounted failure. The
// declarations themselves live in the dexvm_android_catalog_*.cpp batches.

#include "dexvm_android_internal.h"

namespace ogplay::runtime {

std::vector<dexvm::IntrinsicClassDecl> AndroidIntrinsicCatalog() {
    std::vector<dexvm::IntrinsicClassDecl> catalog;
    android_intrinsics::AppendCoreClasses(catalog);
    android_intrinsics::AppendIoClasses(catalog);
    android_intrinsics::AppendDeviceClasses(catalog);
    android_intrinsics::AppendGraphicsClasses(catalog);
    android_intrinsics::AppendWidgetClasses(catalog);
    return catalog;
}

}  // namespace ogplay::runtime
