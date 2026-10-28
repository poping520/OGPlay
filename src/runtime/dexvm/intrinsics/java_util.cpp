// java.util ordinary behavior executes the pinned BootDex Java classes.
#include "catalog.h"

namespace ogplay::runtime::dexvm::intrinsics {
void AppendJavaUtilPlatform(std::vector<IntrinsicClassDecl>&, const CoreIntrinsicServices&) {}
}  // namespace ogplay::runtime::dexvm::intrinsics
