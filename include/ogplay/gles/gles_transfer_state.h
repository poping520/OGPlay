#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>

#include "ogplay/gles/gles_call_preparation.h"

namespace ogplay::gles {

struct GlesTransferStateSnapshot final {
    std::uint32_t pack_alignment{4};
    std::uint32_t unpack_alignment{4};
    std::uint32_t array_buffer{};
    std::uint32_t element_array_buffer{};
    std::size_t query_shapes{};
    std::size_t uniform_shapes{};

    bool operator==(const GlesTransferStateSnapshot&) const = default;
};

class GlesTransferStateError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class GlesTransferState final : public GlesLengthResolver {
public:
    void PixelStore(std::uint32_t pname, std::int32_t alignment);
    void BindBuffer(std::uint32_t target, std::uint32_t buffer);
    void SetQueryElementCount(std::uint32_t pname, std::uint64_t count);
    void SetUniformElementCount(std::uint32_t program, std::int32_t location,
                                std::uint64_t count);
    void ClearUniformElementCounts(std::uint32_t program) noexcept;

    [[nodiscard]] GlesTransferStateSnapshot Snapshot() const noexcept;
    [[nodiscard]] std::optional<GlesLengthResolution> Resolve(
        const GlesLengthRequest& request) const override;

private:
    std::uint32_t pack_alignment_{4};
    std::uint32_t unpack_alignment_{4};
    std::uint32_t array_buffer_{};
    std::uint32_t element_array_buffer_{};
    std::map<std::uint32_t, std::uint64_t> query_counts_;
    std::map<std::pair<std::uint32_t, std::int32_t>, std::uint64_t>
        uniform_counts_;
};

}  // namespace ogplay::gles
