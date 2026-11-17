// The upstream ZIP handler declares these vtable entries in its read-side class.
// Provide the unused entry points without linking its writer or encoders.
#include "CPP/7zip/Archive/Zip/ZipHandler.h"

namespace NArchive::NZip {

Z7_COM7F_IMF(CHandler::GetFileTimeType(UInt32*)) {
    return E_NOTIMPL;
}

Z7_COM7F_IMF(CHandler::UpdateItems(ISequentialOutStream*, UInt32, IArchiveUpdateCallback*)) {
    return E_NOTIMPL;
}

Z7_COM7F_IMF(CHandler::SetProperties(const wchar_t* const*, const PROPVARIANT*, UInt32)) {
    return E_NOTIMPL;
}

}  // namespace NArchive::NZip

IInArchive* CreateReadOnlyZipHandler() {
    return new NArchive::NZip::CHandler;
}
