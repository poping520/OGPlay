#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "ogplay/gles/egl_lifecycle.h"

namespace ogplay::gles {

struct AngleFrameInfo final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t clear_count{};
    std::uint64_t readback_count{};
};

class AngleFrame final {
public:
    static AngleFrame CreatePbuffer(AngleBackend backend,
                                    std::uint32_t width,
                                    std::uint32_t height);

    ~AngleFrame();
    AngleFrame(const AngleFrame&) = delete;
    AngleFrame& operator=(const AngleFrame&) = delete;
    AngleFrame(AngleFrame&&) noexcept;
    AngleFrame& operator=(AngleFrame&&) noexcept;

    void Viewport(std::int32_t x, std::int32_t y,
                  std::int32_t width, std::int32_t height);
    void Scissor(std::int32_t x, std::int32_t y,
                 std::int32_t width, std::int32_t height);
    void SetScissorEnabled(bool enabled);
    void ClearColor(float red, float green, float blue, float alpha);
    void Clear(std::uint32_t mask);
    [[nodiscard]] std::vector<std::uint8_t> ReadRgba8();
    [[nodiscard]] AngleFrameInfo Info() const noexcept;

private:
    AngleFrame(std::unique_ptr<EglApi> api, EglLifecycle lifecycle,
               std::uint32_t width, std::uint32_t height) noexcept;
    void RequireNoError(const char* operation) const;

    std::unique_ptr<EglApi> api_;
    EglLifecycle lifecycle_;
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::uint64_t clear_count_{};
    std::uint64_t readback_count_{};
};

}  // namespace ogplay::gles
