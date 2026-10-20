#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include "ogplay/loader/apk_native.h"
#include "ogplay/runtime/bionic/bionic_module_set.h"

#include "ogplay/loader/module_loader.h"

namespace ogplay::runtime {

[[nodiscard]] std::string DumpAndroidBoundarySurfaceJson();

struct AndroidLinkPreflightRequest final {
    std::uint32_t api{};
    std::string root_module;
    std::span<const loader::Elf32ModuleInput> modules;
};

struct AndroidLinkPreflightReport final {
    std::size_t guest_modules{};
    std::size_t boundary_modules{};
    std::size_t relocations{};
};

struct AndroidImportGap final {
    std::string library;
    std::string symbol;
    bool weak{};
};
struct AndroidImportAudit final {
    std::vector<AndroidImportGap> gaps;
    std::size_t strong_missing{}, weak_missing{};
};
// Every APK library is audited as a potential dlopen root. Only its NEEDED
// closure can satisfy imports; unrelated APK exports cannot mask a real gap.
[[nodiscard]] AndroidImportAudit AuditAndroidNativeImports(
    const loader::ApkSelectedNativeLibraries& libraries, const BionicProfile& profile,
    std::span<const BionicModuleSource> system_libraries);

class AndroidLinkPreflightError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] AndroidLinkPreflightReport PreflightAndroidGuestLink(
    const AndroidLinkPreflightRequest& request);

}  // namespace ogplay::runtime
