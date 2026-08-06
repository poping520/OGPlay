#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

#include "ogplay/gles/angle_backend.h"
#include "ogplay/loader/module_loader.h"

namespace ogplay::runtime {

struct AndroidLinkPreflightRequest final {
    std::uint32_t api{};
    std::string root_module;
    std::span<const loader::Elf32ModuleInput> modules;
    gles::AngleBackend backend;
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t supersample_factor{1};
};

struct AndroidLinkPreflightReport final {
    std::size_t guest_modules{};
    std::size_t boundary_modules{};
    std::size_t relocations{};
};

class AndroidLinkPreflightError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] AndroidLinkPreflightReport PreflightAndroidGuestLink(
    const AndroidLinkPreflightRequest& request);

}  // namespace ogplay::runtime
