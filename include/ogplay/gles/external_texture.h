#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>

namespace ogplay::gles {
struct ExternalTextureContext final {
    std::uintptr_t display{}, context{};
    bool operator==(const ExternalTextureContext&) const = default;
};
struct ExternalTextureCall final {
    enum class Operation { query, bind, upload, detach };
    Operation operation{Operation::query};
    ExternalTextureContext expected;
    std::uint32_t texture{}, width{}, height{};
    std::span<const std::uint8_t> rgba;
};
class ExternalTextureError final : public std::runtime_error {
public:
    enum class Reason { no_context, wrong_context, unsupported };
    ExternalTextureError(Reason cause, const char* message) : std::runtime_error(message), reason(cause) {}
    const Reason reason;
};
} // namespace ogplay::gles
