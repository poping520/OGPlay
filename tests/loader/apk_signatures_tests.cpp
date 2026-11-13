#include <doctest/doctest.h>

#include <array>

#include "../runtime/resources_test_support.h"
#include "ogplay/core/sha256.h"
#include "ogplay/loader/apk_signatures.h"

TEST_CASE("APK signature loader extracts the complete DER of a v1 signer") {
    using namespace ogplay::loader;
    const auto apk = ogplay::test::resources::ReadSource(std::filesystem::path{OGPLAY_SOURCE_DIR} /
                                                         "samples/sweetshop/out/ogplay-sweetshop.apk");
    const auto archive = ParseApkArchive(apk.bytes);
    const auto certificates = ReadApkSignatureCertificates(apk.bytes, archive);
    REQUIRE(certificates.size() == 1);
    // openssl pkcs7 -inform DER -print_certs | openssl x509 -outform DER | shasum -a 256
    CHECK(ogplay::core::Sha256(certificates.front()) ==
          "cd334e85f922986744ce0d596094f36bfc091d44f3cd636a1733ec00d40537fb");

    auto block = ReadApkEntry(apk.bytes, archive, "META-INF/ANDROIDD.RSA");
    block.pop_back();  // ContentInfo length now extends beyond the entry.
    const std::array malformed_entries{
        std::pair{std::string("META-INF/TEST.RSA"), std::move(block)},
        std::pair{std::string("META-INF/TEST.SF"), std::vector<std::byte>{}},
        std::pair{std::string("META-INF/MANIFEST.MF"), std::vector<std::byte>{}},
    };
    const auto malformed = ogplay::test::resources::Zip(malformed_entries);
    CHECK_THROWS_WITH_AS(static_cast<void>(ReadApkSignatureCertificates(malformed, ParseApkArchive(malformed))),
                         "malformed or unsupported APK PKCS#7 SignedData DER", std::runtime_error);
    const auto unsigned_apk = ogplay::test::resources::ReadSource(std::filesystem::path{OGPLAY_SOURCE_DIR} /
                                                                  "tests/fixtures/ui/rom_window.apk");
    CHECK_THROWS_WITH_AS(
        static_cast<void>(ReadApkSignatureCertificates(unsigned_apk.bytes, ParseApkArchive(unsigned_apk.bytes))),
        "APK has no META-INF JAR signature block", std::runtime_error);
}
