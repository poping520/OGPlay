#include "paint.h"

#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/ports/SkFontMgr_empty.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace ogplay::runtime::raster {
namespace {

struct Family {
    std::initializer_list<std::string_view> names;
    std::array<std::string_view, 4> styles;
};

// AOSP android-4.4.4_r2.0.1 frameworks/base/data/fonts/system_fonts.xml.
// Explicit style slots account for the intrinsic italic styles in the light/thin families.
const Family kFamilies[]{
    {{"sans-serif", "arial", "helvetica", "tahoma", "verdana"},
     {"Roboto-Regular.ttf", "Roboto-Bold.ttf", "Roboto-Italic.ttf", "Roboto-BoldItalic.ttf"}},
    {{"sans-serif-light"}, {"Roboto-Light.ttf", "", "Roboto-LightItalic.ttf", ""}},
    {{"sans-serif-thin"}, {"Roboto-Thin.ttf", "", "Roboto-ThinItalic.ttf", ""}},
    {{"sans-serif-condensed"},
     {"RobotoCondensed-Regular.ttf", "RobotoCondensed-Bold.ttf", "RobotoCondensed-Italic.ttf",
      "RobotoCondensed-BoldItalic.ttf"}},
    {{"serif", "times", "times new roman", "palatino", "georgia", "baskerville", "goudy", "fantasy", "cursive",
      "ITC Stone Serif"},
     {"DroidSerif-Regular.ttf", "DroidSerif-Bold.ttf", "DroidSerif-Italic.ttf", "DroidSerif-BoldItalic.ttf"}},
    {{"Droid Sans"}, {"DroidSans.ttf", "DroidSans-Bold.ttf", "", ""}},
    {{"monospace", "courier", "courier new", "monaco"}, {"DroidSansMono.ttf", "", "", ""}},
};

const SkFontMgr& FontManager() {
    static const auto manager = SkFontMgr_New_Custom_Empty();
    return *manager;
}

sk_sp<SkTypeface> Decode(std::span<const std::byte> bytes) {
    if (bytes.empty()) return {};
    return FontManager().makeFromData(SkData::MakeWithCopy(bytes.data(), bytes.size()));
}

struct SystemFace {
    std::once_flag once;
    sk_sp<SkTypeface> face;

    sk_sp<SkTypeface> Load(const FontSource& source, std::string_view file) {
        if (!source) return SkTypeface::MakeEmpty();
        std::call_once(once, [&] {
            face = Decode(source(file));
            if (!face) throw std::runtime_error("invalid API19 font payload: " + std::string(file));
        });
        return face;
    }
};

int NormalizeStyle(int style) {
    return style >= 0 && style <= 3 ? style : 0;
}

}  // namespace

int SystemFontFamily(std::string_view name) {
    const auto lower = [](char ch) { return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch; };
    for (std::size_t i = 0; i < std::size(kFamilies); ++i)
        for (auto alias : kFamilies[i].names)
            if (std::ranges::equal(name, alias, {}, lower, lower)) return static_cast<int>(i);
    return 0;
}

std::shared_ptr<const Typeface> SystemTypeface(const FontSource& source, int family, int style) {
    static SystemFace faces[std::size(kFamilies)][4];
    style = NormalizeStyle(style);
    const auto& files = kFamilies[family].styles;
    auto actual = static_cast<std::size_t>(style);
    if (files[actual].empty()) actual ^= 2;
    if (files[actual].empty())
        actual = static_cast<std::size_t>(std::ranges::find_if(files, [](auto file) { return !file.empty(); }) -
                                          files.begin());
    return std::make_shared<const Typeface>(
        Typeface{faces[family][actual].Load(source, files[actual]), family, style, static_cast<int>(actual)});
}

sk_sp<SkTypeface> SystemFallback(const FontSource& source) {
    static SystemFace fallback;
    return fallback.Load(source, "DroidSansFallback.ttf");
}

std::shared_ptr<const Typeface> DecodeTypeface(std::span<const std::byte> bytes) {
    auto face = Decode(bytes);
    if (!face) return {};
    const int style = (face->isBold() ? 1 : 0) | (face->isItalic() ? 2 : 0);
    return std::make_shared<const Typeface>(Typeface{std::move(face), -1, style, style});
}

std::shared_ptr<const Typeface> RestyleTypeface(const FontSource& source, const Typeface& face, int style) {
    if (face.family >= 0) return SystemTypeface(source, face.family, style);
    return std::make_shared<const Typeface>(Typeface{face.face, -1, NormalizeStyle(style), face.intrinsic_style});
}

}  // namespace ogplay::runtime::raster
