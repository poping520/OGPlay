#pragma once

#include <GLES2/gl2.h>
#include <android/asset_manager.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace sweetshop {

constexpr float kWidth = 1280.0F;
constexpr float kHeight = 720.0F;

struct Color {
    float r = 1.0F, g = 1.0F, b = 1.0F, a = 1.0F;
};

constexpr Color Hex(std::uint32_t rgb, float alpha = 1.0F) {
    return {static_cast<float>((rgb >> 16) & 0xff) / 255.0F, static_cast<float>((rgb >> 8) & 0xff) / 255.0F,
            static_cast<float>(rgb & 0xff) / 255.0F, alpha};
}

inline Color WithAlpha(Color color, float alpha) {
    color.a *= alpha;
    return color;
}

enum class Texture { atlas, text, background, logo, title_zh, title_en, count };

struct Sprite {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    float width = 0, height = 0;
    float density = 1;  // pixels per logical pixel, used by Label
    Texture texture = Texture::atlas;
};
enum class Align { left, center, right };

// Draws premultiplied-alpha quads in a fixed 1280x720 logical space, letterboxed
// into whatever surface size the window has.
class Renderer {
public:
    bool Init(AAssetManager* assets);
    void Shutdown();
    [[nodiscard]] bool Ready() const { return ready_; }

    void BeginFrame(int surface_width, int surface_height);
    void EndFrame();

    [[nodiscard]] const Sprite& Get(const std::string& name) const;
    // Centered sprite; additive blending glows on top of what is below.
    void Draw(const Sprite& sprite, float x, float y, float width, float height, Color color = {},
              float rotation = 0.0F, bool additive = false);
    void DrawFull(Texture texture, float x, float y, float width, float height, Color color = {});
    void Rect(float x, float y, float width, float height, Color color);
    // Stretches a sprite with fixed-size corners; `border` is the corner size in logical pixels.
    void NineSlice(const Sprite& sprite, float x, float y, float width, float height, float border, Color color);
    void Number(int value, float x, float y, float height, Color color = {}, Align align = Align::center);
    void Text(const std::string& glyphs, float x, float y, float height, Color color = {},
              Align align = Align::center);
    // Pre-rendered labels and banners are drawn at their stored density (2x text, 1.5x ribbons).
    void Label(const std::string& name, float x, float y, float scale = 1.0F, Color color = {},
               Align align = Align::center);
    [[nodiscard]] float LabelWidth(const std::string& name, float scale = 1.0F) const {
        const Sprite& sprite = Get(name);
        return sprite.width / sprite.density * scale;
    }
    [[nodiscard]] float TextWidth(std::size_t glyphs, float height) const { return glyphs * height * 0.6F; }

    // Maps a surface pixel to logical coordinates; false when it falls in the letterbox.
    [[nodiscard]] bool ToLogical(float surface_x, float surface_y, float* x, float* y) const;
    [[nodiscard]] Sprite TextureSprite(Texture texture) const;

private:
    struct Vertex {
        float x, y, u, v;
        std::uint8_t r, g, b, a;
    };
    struct Image {
        GLuint id = 0;
        int width = 0, height = 0;
    };

    // `alpha_path` names a grayscale mask for color stored as JPEG.
    bool LoadTexture(AAssetManager* assets, const char* path, Texture slot, bool mipmaps,
                     const char* alpha_path = nullptr);
    bool LoadAtlas(AAssetManager* assets);
    void Use(Texture texture);
    void Quad(const float* xs, const float* ys, float u0, float v0, float u1, float v1, Color color, bool additive);
    void Flush();

    bool ready_ = false;
    GLuint program_ = 0, vertex_buffer_ = 0, index_buffer_ = 0;
    GLint position_ = -1, uv_ = -1, color_ = -1, scale_ = -1, sampler_ = -1;
    Image images_[static_cast<int>(Texture::count)];
    Texture current_ = Texture::atlas;
    std::vector<Vertex> vertices_;
    std::vector<std::uint16_t> indices_;
    std::unordered_map<std::string, Sprite> sprites_;
    Sprite white_;
    float view_x_ = 0, view_y_ = 0, view_scale_ = 1;
    int surface_height_ = 0;
};

}  // namespace sweetshop
