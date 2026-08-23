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

struct AngleActiveVariable final {
    std::string name;
    std::int32_t size{};
    std::uint32_t type{};
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

    void BindCurrentOnCallingThread();
    void ReleaseCurrent();

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
    [[nodiscard]] bool IsShader(std::uint32_t shader);
    [[nodiscard]] std::uint32_t CreateProgram();
    void AttachShader(std::uint32_t program, std::uint32_t shader);
    void LinkProgram(std::uint32_t program);
    [[nodiscard]] std::int32_t GetProgramParameter(std::uint32_t program,
                                                    std::uint32_t parameter);
    [[nodiscard]] AngleActiveVariable GetActiveAttribute(
        std::uint32_t program, std::uint32_t index);
    [[nodiscard]] AngleActiveVariable GetActiveUniform(
        std::uint32_t program, std::uint32_t index);
    [[nodiscard]] std::string GetProgramInfoLog(std::uint32_t program);
    [[nodiscard]] std::string GetShaderInfoLog(std::uint32_t shader);
    [[nodiscard]] std::int32_t GetAttribLocation(std::uint32_t program,
                                                  const std::string& name);
    [[nodiscard]] std::int32_t GetUniformLocation(std::uint32_t program,
                                                   const std::string& name);
    void UseProgram(std::uint32_t program);
    void DeleteProgram(std::uint32_t program);
    [[nodiscard]] bool IsProgram(std::uint32_t program);
    [[nodiscard]] std::vector<std::uint32_t> GenerateBuffers(std::size_t count);
    void DeleteBuffers(std::span<const std::uint32_t> buffers);
    void BindBuffer(std::uint32_t target, std::uint32_t buffer);
    void BufferData(std::uint32_t target, std::uint32_t byte_size,
                    std::optional<std::span<const std::byte>> data,
                    std::uint32_t usage);
    void BufferSubData(std::uint32_t target, std::uint32_t offset,
                       std::span<const std::byte> data);
    [[nodiscard]] std::int32_t GetBufferParameter(std::uint32_t target,
                                                   std::uint32_t parameter);
    [[nodiscard]] bool IsBuffer(std::uint32_t buffer);
    [[nodiscard]] std::vector<std::uint32_t> GenerateFramebuffers(
        std::size_t count);
    void DeleteFramebuffers(std::span<const std::uint32_t> framebuffers);
    void BindFramebuffer(std::uint32_t target, std::uint32_t framebuffer);
    [[nodiscard]] std::uint32_t CheckFramebufferStatus(std::uint32_t target);
    [[nodiscard]] bool IsFramebuffer(std::uint32_t framebuffer);
    [[nodiscard]] std::vector<std::uint32_t> GenerateRenderbuffers(
        std::size_t count);
    void DeleteRenderbuffers(std::span<const std::uint32_t> renderbuffers);
    void BindRenderbuffer(std::uint32_t target, std::uint32_t renderbuffer);
    [[nodiscard]] bool IsRenderbuffer(std::uint32_t renderbuffer);
    void RenderbufferStorage(std::uint32_t target,
                             std::uint32_t internal_format,
                             std::int32_t width, std::int32_t height);
    void FramebufferTexture2D(std::uint32_t target,
                              std::uint32_t attachment,
                              std::uint32_t texture_target,
                              std::uint32_t texture, std::int32_t level);
    void FramebufferRenderbuffer(std::uint32_t target,
                                 std::uint32_t attachment,
                                 std::uint32_t renderbuffer_target,
                                 std::uint32_t renderbuffer);
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
    void CompressedTextureSubImage2D(
        std::uint32_t target, std::int32_t level, std::int32_t x_offset,
        std::int32_t y_offset, std::int32_t width, std::int32_t height,
        std::uint32_t format, std::span<const std::byte> data);
    void CopyTextureSubImage2D(std::uint32_t target, std::int32_t level,
                               std::int32_t x_offset, std::int32_t y_offset,
                               std::int32_t x, std::int32_t y,
                               std::int32_t width, std::int32_t height);
    [[nodiscard]] std::int32_t GetTextureParameterInteger(
        std::uint32_t target, std::uint32_t parameter);
    [[nodiscard]] float GetTextureParameterFloat(std::uint32_t target,
                                                  std::uint32_t parameter);
    [[nodiscard]] bool IsTexture(std::uint32_t texture);
    void GenerateMipmap(std::uint32_t target);
    void SetVertexAttributeEnabled(std::uint32_t index, bool enabled);
    void VertexAttributePointer(std::uint32_t index, std::int32_t size,
                                std::uint32_t type, bool normalized,
                                std::int32_t stride, std::uint32_t offset);
    void Uniform1f(std::int32_t location, float value);
    void Uniform1i(std::int32_t location, std::int32_t value);
    void Uniform4f(std::int32_t location, float x, float y, float z, float w);
    void UniformFloats(std::int32_t location, std::int32_t count,
                       std::uint32_t components,
                       std::span<const float> values);
    void UniformIntegers(std::int32_t location, std::int32_t count,
                         std::uint32_t components,
                         std::span<const std::int32_t> values);
    void UniformMatrix3(std::int32_t location, std::int32_t count,
                        bool transpose, std::span<const float> values);
    void UniformMatrix4(std::int32_t location, std::int32_t count,
                        bool transpose, std::span<const float> values);
    void VertexAttribute4f(std::uint32_t index, float x, float y,
                           float z, float w);
    void SetCapability(std::uint32_t capability, bool enabled);
    void BlendColor(float red, float green, float blue, float alpha);
    void BlendEquation(std::uint32_t mode);
    void BlendEquationSeparate(std::uint32_t rgb, std::uint32_t alpha);
    void BlendFunction(std::uint32_t source, std::uint32_t destination);
    void BlendFunctionSeparate(std::uint32_t source_rgb,
                               std::uint32_t destination_rgb,
                               std::uint32_t source_alpha,
                               std::uint32_t destination_alpha);
    void ColorMask(bool red, bool green, bool blue, bool alpha);
    void ClearStencil(std::int32_t value);
    void CullFace(std::uint32_t mode);
    void DepthFunction(std::uint32_t function);
    void DepthMask(bool enabled);
    void DepthRange(float near_value, float far_value);
    void Finish();
    void FrontFace(std::uint32_t mode);
    void Hint(std::uint32_t target, std::uint32_t mode);
    void LineWidth(float width);
    void PolygonOffset(float factor, float units);
    void SampleCoverage(float value, bool invert);
    void StencilFunction(std::uint32_t function, std::int32_t reference,
                         std::uint32_t mask);
    void StencilFunctionSeparate(std::uint32_t face, std::uint32_t function,
                                 std::int32_t reference, std::uint32_t mask);
    void StencilMask(std::uint32_t mask);
    void StencilMaskSeparate(std::uint32_t face, std::uint32_t mask);
    void StencilOperation(std::uint32_t stencil_fail,
                          std::uint32_t depth_fail,
                          std::uint32_t depth_pass);
    void StencilOperationSeparate(std::uint32_t face,
                                  std::uint32_t stencil_fail,
                                  std::uint32_t depth_fail,
                                  std::uint32_t depth_pass);
    void Flush();
    [[nodiscard]] std::vector<std::int32_t> GetIntegers(
        std::uint32_t parameter, std::size_t count);
    [[nodiscard]] std::vector<float> GetFloats(std::uint32_t parameter,
                                               std::size_t count);
    [[nodiscard]] std::string GetString(std::uint32_t parameter);
    [[nodiscard]] std::uint32_t GetError() noexcept;
    void DrawArrays(std::uint32_t mode, std::int32_t first,
                    std::int32_t count);
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
