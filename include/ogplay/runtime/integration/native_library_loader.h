#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/loader/apk_native.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"

namespace ogplay::runtime {

using JavaClassLoaderToken = std::uint64_t;
using NativeLibraryHandle = std::uint64_t;

enum class NativeLibraryLoadState : std::uint8_t {
    loading,
    loaded,
    failed,
};

enum class NativeLibraryLoadErrorReason : std::uint8_t {
    invalid_request,
    library_not_found,
    malformed_elf,
    soname_mismatch,
    unresolved_dependency,
    link_failure,
    constructor_failure,
    jni_on_load_failure,
    invalid_jni_version,
    cross_loader_conflict,
    prior_failure,
};

class NativeLibraryLoadError final : public std::runtime_error {
public:
    NativeLibraryLoadError(NativeLibraryLoadErrorReason reason,
                           std::string message);
    [[nodiscard]] NativeLibraryLoadErrorReason Reason() const noexcept;

private:
    NativeLibraryLoadErrorReason reason_;
};

struct NativeLibraryLoadResult final {
    NativeLibraryHandle handle{};
    std::string canonical_path;
    std::size_t module_index{};
    std::vector<std::string> initialized_modules;
    std::optional<std::uint32_t> jni_version;
    bool already_loaded{};
    bool recursive{};
};

struct NativeLibraryRecord final {
    NativeLibraryHandle handle{};
    std::string canonical_path;
    std::string soname;
    JavaClassLoaderToken class_loader{};
    NativeLibraryLoadState state{NativeLibraryLoadState::loading};
    std::size_t module_index{};
    std::optional<std::uint32_t> jni_version;
    std::uint64_t jni_on_load_calls{};
    std::string failure;
};

class NativeLibraryLoader final {
public:
    NativeLibraryLoader(
        AndroidGuestProcess& process,
        const loader::ApkSelectedNativeLibraries& libraries);
    ~NativeLibraryLoader();
    NativeLibraryLoader(const NativeLibraryLoader&) = delete;
    NativeLibraryLoader& operator=(const NativeLibraryLoader&) = delete;

    [[nodiscard]] NativeLibraryLoadResult LoadLibrary(
        std::string_view logical_name, JavaClassLoaderToken class_loader);
    [[nodiscard]] NativeLibraryLoadResult LoadPath(
        std::string_view guest_path, JavaClassLoaderToken class_loader);
    [[nodiscard]] std::vector<NativeLibraryRecord> Records() const;

    [[nodiscard]] static std::string SyntheticGuestPath(
        std::string_view soname);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
