#include "ogplay/loader/apk_signatures.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace ogplay::loader {
namespace {

struct DerValue {
    std::span<const std::byte> encoded;
    std::span<const std::byte> body;
};

[[noreturn]] void InvalidDer() {
    throw std::runtime_error("malformed or unsupported APK PKCS#7 SignedData DER");
}

bool HasTag(std::span<const std::byte> bytes, unsigned tag) {
    return !bytes.empty() && bytes.front() == std::byte{static_cast<unsigned char>(tag)};
}

DerValue ReadDer(std::span<const std::byte>& input, unsigned tag) {
    if (input.size() < 2 || !HasTag(input, tag)) InvalidDer();
    std::size_t length = std::to_integer<unsigned>(input[1]);
    std::size_t header = 2;
    if (length >= 0x80) {
        const auto count = length & 0x7f;
        if (count == 0 || count > sizeof(length) || count > input.size() - header || input[header] == std::byte{0})
            InvalidDer();
        length = 0;
        for (std::size_t i = 0; i < count; ++i)
            length = (length << 8) | std::to_integer<unsigned>(input[header++]);
        if (length < 0x80) InvalidDer();
    }
    if (length > input.size() - header) InvalidDer();
    const DerValue result{input.first(header + length), input.subspan(header, length)};
    input = input.subspan(header + length);
    return result;
}

void RequireEnd(std::span<const std::byte> input) {
    if (!input.empty()) InvalidDer();
}

void ReadAlgorithm(std::span<const std::byte>& input) {
    auto algorithm = ReadDer(input, 0x30).body;
    if (ReadDer(algorithm, 0x06).body.empty()) InvalidDer();
    // Algorithm parameters are opaque here, but must be a complete DER value.
    if (!algorithm.empty()) ReadDer(algorithm, std::to_integer<unsigned>(algorithm.front()));
    RequireEnd(algorithm);
}

std::vector<std::byte> ReadSignerCertificate(std::span<const std::byte> block) {
    auto content = ReadDer(block, 0x30).body;
    RequireEnd(block);
    constexpr std::array signed_data_oid{std::byte{0x2a}, std::byte{0x86}, std::byte{0x48},
                                         std::byte{0x86}, std::byte{0xf7}, std::byte{0x0d},
                                         std::byte{0x01}, std::byte{0x07}, std::byte{0x02}};
    if (!std::ranges::equal(ReadDer(content, 0x06).body, signed_data_oid)) InvalidDer();
    auto wrapped = ReadDer(content, 0xa0).body;
    RequireEnd(content);
    auto signed_data = ReadDer(wrapped, 0x30).body;
    RequireEnd(wrapped);
    if (!std::ranges::equal(ReadDer(signed_data, 0x02).body, std::array{std::byte{1}})) InvalidDer();
    auto digest_algorithms = ReadDer(signed_data, 0x31).body;
    ReadAlgorithm(digest_algorithms);
    RequireEnd(digest_algorithms);
    auto data = ReadDer(signed_data, 0x30).body;
    if (ReadDer(data, 0x06).body.empty()) InvalidDer();
    if (HasTag(data, 0xa0)) {
        auto payload = ReadDer(data, 0xa0).body;
        ReadDer(payload, 0x04);
        RequireEnd(payload);
    }
    RequireEnd(data);

    auto certificates = ReadDer(signed_data, 0xa0).body;
    const auto certificate = ReadDer(certificates, 0x30);
    if (!certificates.empty())
        throw std::runtime_error("APK signature certificate sets with multiple entries are unsupported");
    if (HasTag(signed_data, 0xa1)) ReadDer(signed_data, 0xa1);
    auto signers = ReadDer(signed_data, 0x31).body;
    RequireEnd(signed_data);
    auto signer = ReadDer(signers, 0x30).body;
    if (!signers.empty()) throw std::runtime_error("APK signature blocks with multiple signers are unsupported");
    if (!std::ranges::equal(ReadDer(signer, 0x02).body, std::array{std::byte{1}})) InvalidDer();
    auto signer_id = ReadDer(signer, 0x30).body;
    const auto signer_issuer = ReadDer(signer_id, 0x30).encoded;
    const auto signer_serial = ReadDer(signer_id, 0x02).body;
    RequireEnd(signer_id);
    ReadAlgorithm(signer);
    if (HasTag(signer, 0xa0)) ReadDer(signer, 0xa0);
    ReadAlgorithm(signer);
    if (ReadDer(signer, 0x04).body.empty()) InvalidDer();
    if (HasTag(signer, 0xa1)) ReadDer(signer, 0xa1);
    RequireEnd(signer);

    auto cert = certificate.body;
    auto tbs = ReadDer(cert, 0x30).body;
    ReadAlgorithm(cert);
    if (ReadDer(cert, 0x03).body.empty()) InvalidDer();
    RequireEnd(cert);
    if (HasTag(tbs, 0xa0)) {
        auto version = ReadDer(tbs, 0xa0).body;
        if (ReadDer(version, 0x02).body.size() != 1) InvalidDer();
        RequireEnd(version);
    }
    const auto serial = ReadDer(tbs, 0x02).body;
    ReadAlgorithm(tbs);
    const auto issuer = ReadDer(tbs, 0x30).encoded;
    ReadDer(tbs, 0x30);  // validity
    ReadDer(tbs, 0x30);  // subject
    auto key = ReadDer(tbs, 0x30).body;
    ReadAlgorithm(key);
    if (ReadDer(key, 0x03).body.empty()) InvalidDer();
    RequireEnd(key);
    for (const unsigned tag : {0x81U, 0x82U, 0xa3U})
        if (HasTag(tbs, tag)) ReadDer(tbs, tag);
    RequireEnd(tbs);
    // JarUtils finds the signer by issuer and serial before building its chain.
    // Exact DER Name equality covers the supported, unambiguous form.
    if (serial.empty() || !std::ranges::equal(serial, signer_serial) || !std::ranges::equal(issuer, signer_issuer))
        throw std::runtime_error("APK signature certificate does not match the signer issuer and serial");
    return {certificate.encoded.begin(), certificate.encoded.end()};
}

std::string UpperJarName(std::string name) {
    for (auto& c : name)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return name;
}

}  // namespace

std::vector<std::vector<std::byte>> ReadApkSignatureCertificates(std::span<const std::byte> bytes,
                                                                 const ApkArchive& archive) {
    const ApkEntry* block{};
    std::string signature_file;
    for (const auto& entry : archive.entries) {
        const auto name = UpperJarName(entry.name);
        if (!name.starts_with("META-INF/") ||
            !(name.ends_with(".RSA") || name.ends_with(".DSA") || name.ends_with(".EC")))
            continue;
        if (block) throw std::runtime_error("APKs with multiple JAR signature blocks are unsupported");
        block = &entry;
        signature_file = name.substr(0, name.rfind('.')) + ".SF";
    }
    if (!block) throw std::runtime_error("APK has no META-INF JAR signature block");
    auto certificate = ReadSignerCertificate(ReadApkEntry(bytes, archive, block->name));
    const auto has_entry = [&](std::string_view name) {
        return std::ranges::any_of(archive.entries,
                                   [&](const auto& entry) { return UpperJarName(entry.name) == name; });
    };
    if (!has_entry(signature_file) || !has_entry("META-INF/MANIFEST.MF"))
        throw std::runtime_error("APK JAR signature block lacks its .SF file or manifest");
    std::vector<std::vector<std::byte>> certificates;
    certificates.push_back(std::move(certificate));
    return certificates;
}

}  // namespace ogplay::loader
