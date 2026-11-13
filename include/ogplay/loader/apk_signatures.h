#pragma once

#include "ogplay/loader/apk.h"

namespace ogplay::loader {

// Extracts API19-compatible certificate bytes for the supported single-signer,
// single-certificate JAR signature form. Throws std::runtime_error on missing or
// unsupported signature data. Does not verify the APK's signatures or digests.
[[nodiscard]] std::vector<std::vector<std::byte>> ReadApkSignatureCertificates(std::span<const std::byte> bytes,
                                                                               const ApkArchive& archive);

}  // namespace ogplay::loader
