#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ogplay/gles/egl_lifecycle.h"

namespace ogplay::gles {

struct AngleFrameInfo final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t clear_count{};
    std::uint64_t readback_count{};
    std::uint64_t shader_compile_count{};
    std::uint64_t program_link_count{};
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
    void ClearDepth(float depth);
    void Clear(std::uint32_t mask);
    [[nodiscard]] std::uint32_t CreateShader(std::uint32_t type);
    void ShaderSource(std::uint32_t shader, std::span<const std::string> sources);
    void CompileShader(std::uint32_t shader);
    [[nodiscard]] std::int32_t GetShaderParameter(std::uint32_t shader,
                                                   std::uint32_t parameter);
    void DeleteShader(std::uint32_t shader);
    [[nodiscard]] std::uint32_t CreateProgram();
    void AttachShader(std::uint32_t program, std::uint32_t shader);
    void LinkProgram(std::uint32_t program);
    [[nodiscard]] std::int32_t GetProgramParameter(std::uint32_t program,
                                                    std::uint32_t parameter);
    [[nodiscard]] std::int32_t GetAttribLocation(std::uint32_t program,
                                                  const std::string& name);
    [[nodiscard]] std::int32_t GetUniformLocation(std::uint32_t program,
                                                   const std::string& name);
    void UseProgram(std::uint32_t program);
    void DeleteProgram(std::uint32_t program);
    [[nodiscard]] std::vector<std::uint32_t> GenerateBuffers(std::size_t count);
    void DeleteBuffers(std::span<const std::uint32_t> buffers);
    void BindBuffer(std::uint32_t target, std::uint32_t buffer);
    void BufferData(std::uint32_t target, std::uint32_t byte_size,
                    std::optional<std::span<const std::byte>> data,
                    std::uint32_t usage);
    [[nodiscard]] std::vector<std::uint32_t> GenerateTextures(std::size_t count);
    void DeleteTextures(std::span<const std::uint32_t> textures);
    void ActiveTexture(std::uint32_t texture);
    void BindTexture(std::uint32_t target, std::uint32_t texture);
    void PixelStore(std::uint32_t parameter, std::int32_t value);
    void TextureParameter(std::uint32_t target, std::uint32_t parameter,
                          std::int32_t value);
    void TextureParameterFloat(std::uint32_t target,
                               std::uint32_t parameter, float value);
    void TextureImage2D(std::uint32_t target, std::int32_t level,
                        std::int32_t internal_format, std::int32_t width,
                        std::int32_t height, std::int32_t border,
                        std::uint32_t format, std::uint32_t type,
                        std::optional<std::span<const std::byte>> pixels);
    void CompressedTextureImage2D(std::uint32_t target, std::int32_t level,
                                  std::uint32_t internal_format,
                                  std::int32_t width, std::int32_t height,
                                  std::int32_t border,
                                  std::span<const std::byte> data);
    void CopyTextureImage2D(std::uint32_t target, std::int32_t level,
                            std::uint32_t internal_format, std::int32_t x,
                            std::int32_t y, std::int32_t width,
                            std::int32_t height, std::int32_t border);
    void TextureSubImage2D(std::uint32_t target, std::int32_t level,
                           std::int32_t x_offset, std::int32_t y_offset,
                           std::int32_t width, std::int32_t height,
                           std::uint32_t format, std::uint32_t type,
                           std::span<const std::byte> pixels);
    void GenerateMipmap(std::uint32_t target);
    void SetVertexAttributeEnabled(std::uint32_t index, bool enabled);
    void VertexAttributePointer(std::uint32_t index, std::int32_t size,
                                std::uint32_t type, bool normalized,
                                std::int32_t stride, std::uint32_t offset);
    void Uniform1f(std::int32_t location, float value);
    void Uniform1i(std::int32_t location, std::int32_t value);
    void Uniform4f(std::int32_t location, float x, float y, float z, float w);
    void UniformMatrix3(std::int32_t location, std::int32_t count,
                        bool transpose, std::span<const float> values);
    void SetCapability(std::uint32_t capability, bool enabled);
    void BlendFunction(std::uint32_t source, std::uint32_t destination);
    void ColorMask(bool red, bool green, bool blue, bool alpha);
    void CullFace(std::uint32_t mode);
    void DepthFunction(std::uint32_t function);
    void DepthMask(bool enabled);
    void Finish();
    void FrontFace(std::uint32_t mode);
    void Hint(std::uint32_t target, std::uint32_t mode);
    [[nodiscard]] std::vector<std::int32_t> GetIntegers(
        std::uint32_t parameter, std::size_t count);
    [[nodiscard]] std::string GetString(std::uint32_t parameter);
    [[nodiscard]] std::uint32_t GetError() noexcept;
    void DrawElements(std::uint32_t mode, std::int32_t count,
                      std::uint32_t type, std::uint32_t offset);
    void ReadPixels(std::int32_t x, std::int32_t y,
                    std::int32_t width, std::int32_t height,
                    std::uint32_t format, std::uint32_t type,
                    std::span<std::byte> output);
    void ReadRgba8(std::vector<std::uint8_t>& output);
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
    std::uint64_t shader_compile_count_{};
    std::uint64_t program_link_count_{};
};

}  // namespace ogplay::gles
