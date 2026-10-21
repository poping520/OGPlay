#include "catalog.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
void AppendJavaRegex(std::vector<IntrinsicClassDecl>& catalog) {
    {
        auto b = IntrinsicClassBuilder::Class("Ljava/util/regex/Pattern;");
        b.GuestNativeStatic("closeImpl", "(J)V", kAccPrivate | kAccStatic | kAccNative);
        b.GuestNativeStatic("compileImpl", "(Ljava/lang/String;I)J", kAccPrivate | kAccStatic | kAccNative);
        catalog.push_back(std::move(b).Build());
    }
    {
        auto b = IntrinsicClassBuilder::Class("Ljava/util/regex/Matcher;");
        b.GuestNativeStatic("closeImpl", "(J)V", kAccPrivate | kAccStatic | kAccNative);
        b.GuestNativeStatic("findImpl", "(JLjava/lang/String;I[I)Z", kAccPrivate | kAccStatic | kAccNative);
        b.GuestNativeStatic("findNextImpl", "(JLjava/lang/String;[I)Z", kAccPrivate | kAccStatic | kAccNative);
        b.GuestNativeStatic("groupCountImpl", "(J)I", kAccPrivate | kAccStatic | kAccNative);
        b.GuestNativeStatic("hitEndImpl", "(J)Z", kAccPrivate | kAccStatic | kAccNative);
        b.GuestNativeStatic("lookingAtImpl", "(JLjava/lang/String;[I)Z", kAccPrivate | kAccStatic | kAccNative);
        b.GuestNativeStatic("matchesImpl", "(JLjava/lang/String;[I)Z", kAccPrivate | kAccStatic | kAccNative);
        b.GuestNativeStatic("openImpl", "(J)J", kAccPrivate | kAccStatic | kAccNative);
        b.GuestNativeStatic("requireEndImpl", "(J)Z", kAccPrivate | kAccStatic | kAccNative);
        b.GuestNativeStatic("setInputImpl", "(JLjava/lang/String;II)V", kAccPrivate | kAccStatic | kAccNative);
        b.GuestNativeStatic("useAnchoringBoundsImpl", "(JZ)V", kAccPrivate | kAccStatic | kAccNative);
        b.GuestNativeStatic("useTransparentBoundsImpl", "(JZ)V", kAccPrivate | kAccStatic | kAccNative);
        catalog.push_back(std::move(b).Build());
    }
}
}  // namespace ogplay::runtime::dexvm::intrinsics
