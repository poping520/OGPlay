#include "gfx.h"

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#include "stb_image.h"

namespace sweetshop {
namespace {

constexpr int kMaxQuads = 2048;

const char* kVertexShader = R"(
attribute vec2 a_position;
attribute vec2 a_uv;
attribute vec4 a_color;
uniform vec2 u_scale;
varying vec2 v_uv;
varying vec4 v_color;
void main() {
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = vec4(a_position.x * u_scale.x - 1.0, 1.0 - a_position.y * u_scale.y, 0.0, 1.0);
}
)";

const char* kFragmentShader = R"(
precision mediump float;
uniform sampler2D u_texture;
varying vec2 v_uv;
varying vec4 v_color;
void main() {
    gl_FragColor = texture2D(u_texture, v_uv) * v_color;
}
)";

GLuint Compile(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[512] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        __android_log_print(ANDROID_LOG_ERROR, "sweetshop", "shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

std::uint8_t Channel(float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F);
}

}  // namespace

bool Renderer::Init(AAssetManager* assets) {
    const GLuint vertex = Compile(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragment = Compile(GL_FRAGMENT_SHADER, kFragmentShader);
    if (vertex == 0 || fragment == 0) return false;
    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glLinkProgram(program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        __android_log_print(ANDROID_LOG_ERROR, "sweetshop", "program link failed");
        return false;
    }
    position_ = glGetAttribLocation(program_, "a_position");
    uv_ = glGetAttribLocation(program_, "a_uv");
    color_ = glGetAttribLocation(program_, "a_color");
    scale_ = glGetUniformLocation(program_, "u_scale");
    sampler_ = glGetUniformLocation(program_, "u_texture");

    if (!LoadTexture(assets, "atlas.png", Texture::atlas, true) ||
        !LoadTexture(assets, "text.png", Texture::text, true) ||
        !LoadTexture(assets, "background.jpg", Texture::background, false) ||
        !LoadTexture(assets, "logo.jpg", Texture::logo, false, "logo_alpha.png") ||
        !LoadTexture(assets, "title_zh.jpg", Texture::title_zh, false, "title_zh_alpha.png") ||
        !LoadTexture(assets, "title_en.jpg", Texture::title_en, false, "title_en_alpha.png") || !LoadAtlas(assets))
        return false;

    vertices_.reserve(kMaxQuads * 4);
    indices_.resize(kMaxQuads * 6);
    for (int quad = 0; quad < kMaxQuads; ++quad) {
        const auto base = static_cast<std::uint16_t>(quad * 4);
        const std::uint16_t pattern[6] = {base, static_cast<std::uint16_t>(base + 1),
                                          static_cast<std::uint16_t>(base + 2), base,
                                          static_cast<std::uint16_t>(base + 2),
                                          static_cast<std::uint16_t>(base + 3)};
        std::memcpy(&indices_[quad * 6], pattern, sizeof(pattern));
    }
    glGenBuffers(1, &vertex_buffer_);
    glGenBuffers(1, &index_buffer_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices_.size() * sizeof(std::uint16_t)),
                 indices_.data(), GL_STATIC_DRAW);
    ready_ = true;
    return true;
}

void Renderer::Shutdown() {
    for (auto& image : images_) {
        if (image.id != 0) glDeleteTextures(1, &image.id);
        image = {};
    }
    if (vertex_buffer_ != 0) glDeleteBuffers(1, &vertex_buffer_);
    if (index_buffer_ != 0) glDeleteBuffers(1, &index_buffer_);
    if (program_ != 0) glDeleteProgram(program_);
    vertex_buffer_ = index_buffer_ = program_ = 0;
    vertices_.clear();
    ready_ = false;
}

namespace {

stbi_uc* Decode(AAssetManager* assets, const char* path, const int channels, int* width, int* height) {
    AAsset* asset = AAssetManager_open(assets, path, AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, "sweetshop", "missing asset %s", path);
        return nullptr;
    }
    const auto* bytes = static_cast<const stbi_uc*>(AAsset_getBuffer(asset));
    const auto length = static_cast<int>(AAsset_getLength(asset));
    int stored = 0;
    stbi_uc* pixels = bytes == nullptr ? nullptr : stbi_load_from_memory(bytes, length, width, height, &stored, channels);
    AAsset_close(asset);
    if (pixels == nullptr) __android_log_print(ANDROID_LOG_ERROR, "sweetshop", "cannot decode %s", path);
    return pixels;
}

}  // namespace

bool Renderer::LoadTexture(AAssetManager* assets, const char* path, Texture slot, bool mipmaps,
                           const char* alpha_path) {
    int width = 0, height = 0;
    stbi_uc* pixels = Decode(assets, path, 4, &width, &height);
    if (pixels == nullptr) return false;
    if (alpha_path != nullptr) {
        int mask_width = 0, mask_height = 0;
        stbi_uc* mask = Decode(assets, alpha_path, 1, &mask_width, &mask_height);
        if (mask == nullptr || mask_width != width || mask_height != height) {
            __android_log_print(ANDROID_LOG_ERROR, "sweetshop", "alpha mask %s does not fit %s", alpha_path, path);
            stbi_image_free(mask);
            stbi_image_free(pixels);
            return false;
        }
        for (int index = 0; index < width * height; ++index) pixels[index * 4 + 3] = mask[index];
        stbi_image_free(mask);
    }
    // Premultiply so filtered edges stay clean and additive draws need no blend switch.
    for (int index = 0; index < width * height; ++index) {
        stbi_uc* pixel = pixels + index * 4;
        const unsigned alpha = pixel[3];
        for (int channel = 0; channel < 3; ++channel) pixel[channel] = static_cast<stbi_uc>((pixel[channel] * alpha + 127) / 255);
    }
    auto& image = images_[static_cast<int>(slot)];
    glGenTextures(1, &image.id);
    glBindTexture(GL_TEXTURE_2D, image.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const bool power_of_two = (width & (width - 1)) == 0 && (height & (height - 1)) == 0;
    if (mipmaps && power_of_two) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    image.width = width;
    image.height = height;
    return true;
}

bool Renderer::LoadAtlas(AAssetManager* assets) {
    AAsset* asset = AAssetManager_open(assets, "atlas.txt", AASSET_MODE_BUFFER);
    if (asset == nullptr) return false;
    const auto length = static_cast<std::size_t>(AAsset_getLength(asset));
    std::string text(static_cast<const char*>(AAsset_getBuffer(asset)), length);
    AAsset_close(asset);
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(start, end - start);
        start = end + 1;
        // name sheet x y width height density; sheet 0 is the color art, 1 the two-ink text.
        char name[64] = {};
        int sheet = 0, x = 0, y = 0, width = 0, height = 0;
        float density = 1;
        if (std::sscanf(line.c_str(), "%63s %d %d %d %d %d %f", name, &sheet, &x, &y, &width, &height, &density) != 7)
            continue;
        Sprite sprite;
        sprite.texture = sheet == 1 ? Texture::text : Texture::atlas;
        sprite.density = density;
        const auto& atlas = images_[static_cast<int>(sprite.texture)];
        sprite.u0 = static_cast<float>(x) / atlas.width;
        sprite.v0 = static_cast<float>(y) / atlas.height;
        sprite.u1 = static_cast<float>(x + width) / atlas.width;
        sprite.v1 = static_cast<float>(y + height) / atlas.height;
        sprite.width = static_cast<float>(width);
        sprite.height = static_cast<float>(height);
        sprites_[name] = sprite;
    }
    const auto circle = sprites_.find("circle");
    if (circle == sprites_.end()) return false;
    // A texel deep inside the white circle doubles as a solid-color brush.
    const float u = (circle->second.u0 + circle->second.u1) / 2, v = (circle->second.v0 + circle->second.v1) / 2;
    white_ = {u, v, u, v, 1, 1, 1, circle->second.texture};
    return true;
}

const Sprite& Renderer::Get(const std::string& name) const {
    const auto found = sprites_.find(name);
    if (found == sprites_.end()) {
        __android_log_print(ANDROID_LOG_ERROR, "sweetshop", "unknown sprite %s", name.c_str());
        return white_;
    }
    return found->second;
}

Sprite Renderer::TextureSprite(Texture texture) const {
    const auto& image = images_[static_cast<int>(texture)];
    return {0, 0, 1, 1, static_cast<float>(image.width), static_cast<float>(image.height)};
}

void Renderer::BeginFrame(const int surface_width, const int surface_height) {
    surface_height_ = surface_height;
    view_scale_ = std::min(surface_width / kWidth, surface_height / kHeight);
    view_x_ = (surface_width - kWidth * view_scale_) / 2;
    view_y_ = (surface_height - kHeight * view_scale_) / 2;
    glViewport(0, 0, surface_width, surface_height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.12F, 0.08F, 0.07F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(static_cast<GLint>(std::lround(view_x_)), static_cast<GLint>(std::lround(view_y_)),
               static_cast<GLsizei>(std::lround(kWidth * view_scale_)),
               static_cast<GLsizei>(std::lround(kHeight * view_scale_)));
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program_);
    glUniform2f(scale_, 2.0F / kWidth, 2.0F / kHeight);
    glUniform1i(sampler_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
    glEnableVertexAttribArray(static_cast<GLuint>(position_));
    glEnableVertexAttribArray(static_cast<GLuint>(uv_));
    glEnableVertexAttribArray(static_cast<GLuint>(color_));
    current_ = Texture::atlas;
    glBindTexture(GL_TEXTURE_2D, images_[static_cast<int>(current_)].id);
}

void Renderer::EndFrame() { Flush(); }

bool Renderer::ToLogical(const float surface_x, const float surface_y, float* x, float* y) const {
    // Input arrives top-down; the viewport origin is bottom-left, so mirror the vertical offset.
    const float top = static_cast<float>(surface_height_) - view_y_ - kHeight * view_scale_;
    *x = (surface_x - view_x_) / view_scale_;
    *y = (surface_y - top) / view_scale_;
    return *x >= 0 && *y >= 0 && *x <= kWidth && *y <= kHeight;
}

void Renderer::Use(const Texture texture) {
    if (texture == current_) return;
    Flush();
    current_ = texture;
    glBindTexture(GL_TEXTURE_2D, images_[static_cast<int>(texture)].id);
}

void Renderer::Quad(const float* xs, const float* ys, float u0, float v0, float u1, float v1, Color color,
                    const bool additive) {
    if (vertices_.size() >= kMaxQuads * 4) Flush();
    const float alpha = std::clamp(color.a, 0.0F, 1.0F);
    const std::uint8_t r = Channel(color.r * alpha), g = Channel(color.g * alpha), b = Channel(color.b * alpha);
    const std::uint8_t a = additive ? 0 : Channel(alpha);
    const float us[4] = {u0, u1, u1, u0};
    const float vs[4] = {v0, v0, v1, v1};
    for (int corner = 0; corner < 4; ++corner) vertices_.push_back({xs[corner], ys[corner], us[corner], vs[corner], r, g, b, a});
}

void Renderer::Flush() {
    if (vertices_.empty()) return;
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices_.size() * sizeof(Vertex)), vertices_.data(),
                 GL_STREAM_DRAW);
    glVertexAttribPointer(static_cast<GLuint>(position_), 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, x)));
    glVertexAttribPointer(static_cast<GLuint>(uv_), 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, u)));
    glVertexAttribPointer(static_cast<GLuint>(color_), 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, r)));
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(vertices_.size() / 4 * 6), GL_UNSIGNED_SHORT, nullptr);
    vertices_.clear();
}

void Renderer::Draw(const Sprite& sprite, const float x, const float y, const float width, const float height,
                    const Color color, const float rotation, const bool additive) {
    Use(sprite.texture);
    const float half_width = width / 2, half_height = height / 2;
    const float local_x[4] = {-half_width, half_width, half_width, -half_width};
    const float local_y[4] = {-half_height, -half_height, half_height, half_height};
    float xs[4], ys[4];
    const float cosine = std::cos(rotation), sine = std::sin(rotation);
    for (int corner = 0; corner < 4; ++corner) {
        xs[corner] = x + local_x[corner] * cosine - local_y[corner] * sine;
        ys[corner] = y + local_x[corner] * sine + local_y[corner] * cosine;
    }
    Quad(xs, ys, sprite.u0, sprite.v0, sprite.u1, sprite.v1, color, additive);
}

void Renderer::DrawFull(const Texture texture, const float x, const float y, const float width, const float height,
                        const Color color) {
    Use(texture);
    const float xs[4] = {x, x + width, x + width, x};
    const float ys[4] = {y, y, y + height, y + height};
    Quad(xs, ys, 0, 0, 1, 1, color, false);
}

void Renderer::Rect(const float x, const float y, const float width, const float height, const Color color) {
    Use(Texture::atlas);
    const float xs[4] = {x, x + width, x + width, x};
    const float ys[4] = {y, y, y + height, y + height};
    Quad(xs, ys, white_.u0, white_.v0, white_.u1, white_.v1, color, false);
}

void Renderer::NineSlice(const Sprite& sprite, const float x, const float y, const float width, const float height,
                         float border, const Color color) {
    Use(sprite.texture);
    border = std::min({border, width / 2, height / 2});
    const float source_border = sprite.width / 3;
    const float du = (sprite.u1 - sprite.u0) * source_border / sprite.width;
    const float dv = (sprite.v1 - sprite.v0) * source_border / sprite.height;
    const float px[4] = {x, x + border, x + width - border, x + width};
    const float py[4] = {y, y + border, y + height - border, y + height};
    const float pu[4] = {sprite.u0, sprite.u0 + du, sprite.u1 - du, sprite.u1};
    const float pv[4] = {sprite.v0, sprite.v0 + dv, sprite.v1 - dv, sprite.v1};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column) {
            const float xs[4] = {px[column], px[column + 1], px[column + 1], px[column]};
            const float ys[4] = {py[row], py[row], py[row + 1], py[row + 1]};
            Quad(xs, ys, pu[column], pv[row], pu[column + 1], pv[row + 1], color, false);
        }
}

void Renderer::Text(const std::string& glyphs, const float x, const float y, const float height, const Color color,
                    const Align align) {
    const float advance = height * 0.6F;
    const float width = advance * static_cast<float>(glyphs.size());
    float left = align == Align::left ? x : align == Align::center ? x - width / 2 : x - width;
    for (const char glyph : glyphs) {
        const auto& sprite = Get(std::string("glyph_") + glyph);
        const float scale = height / sprite.height;
        Draw(sprite, left + advance / 2, y, sprite.width * scale, height, color);
        left += advance;
    }
}

void Renderer::Label(const std::string& name, const float x, const float y, const float scale, const Color color,
                     const Align align) {
    const Sprite& sprite = Get(name);
    const float width = sprite.width / sprite.density * scale, height = sprite.height / sprite.density * scale;
    const float center = align == Align::left ? x + width / 2 : align == Align::right ? x - width / 2 : x;
    Draw(sprite, center, y, width, height, color);
}

void Renderer::Number(const int value, const float x, const float y, const float height, const Color color,
                      const Align align) {
    Text(std::to_string(std::max(0, value)), x, y, height, color, align);
}

}  // namespace sweetshop
