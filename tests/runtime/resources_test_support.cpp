#include "resources_test_support.h"
#include "ogplay/core/byte_order.h"
#include <fstream>
#include <iterator>
#include <cstring>

namespace ogplay::test::resources {
res::Source OwnedSource(std::vector<std::byte> bytes, std::string name) {
    auto data = std::make_shared<const std::vector<std::byte>>(std::move(bytes));
    return {data, *data, std::move(name), {}};
}

res::Source ReadSource(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("fixture missing: " + path.string());
    std::vector<char> c{std::istreambuf_iterator<char>(f), {}};
    std::vector<std::byte> b(c.size());
    std::memcpy(b.data(), c.data(), c.size());
    return OwnedSource(std::move(b), path.string());
}

std::shared_ptr<const res::AssetPath> Framework() {
    return res::sharedFramework([] {
        return ReadSource(std::filesystem::path(OGPLAY_SOURCE_DIR) / "data/android/19/framework/framework-res.apk");
    });
}

res::ResTable Table() {
    res::ResTable t;
    t.add(Framework()->table(), 1);
    t.setParameters(Config());
    return t;
}

std::uint32_t Id(const res::ResTable& table, std::string_view name) {
    auto id = table.identifierForName(name, {}, "android");
    if (!id) throw std::runtime_error("fixture resource missing: " + std::string(name));
    return id;
}

res::ResTable_config Config(std::uint16_t width, std::uint16_t height, std::uint16_t density) {
    res::ResTable_config c;
    c.sdkVersion = 19;
    c.density = density;
    c.orientation = width > height ? 2 : 1;
    c.screenWidth = width;
    c.screenHeight = height;
    c.screenWidthDp = static_cast<std::uint16_t>(width * 160 / density);
    c.screenHeightDp = static_cast<std::uint16_t>(height * 160 / density);
    c.smallestScreenWidthDp = std::min(c.screenWidthDp, c.screenHeightDp);
    c.screenLayout = 2 | 0x10 | 0x40;
    return c;
}

std::vector<std::byte> Zip(std::span<const std::pair<std::string, std::vector<std::byte>>> entries) {
    std::vector<std::byte> b;
    const auto u16 = [&](std::uint16_t x) {
        b.push_back(static_cast<std::byte>(x));
        b.push_back(static_cast<std::byte>(x >> 8));
    };
    const auto u32 = [&](std::uint32_t x) {
        u16(static_cast<std::uint16_t>(x));
        u16(static_cast<std::uint16_t>(x >> 16));
    };

    struct Entry {
        std::string name;
        std::uint32_t size, offset;
    };

    std::vector<Entry> index;
    for (const auto& [name, data] : entries) {
        index.push_back({name, static_cast<std::uint32_t>(data.size()), static_cast<std::uint32_t>(b.size())});
        u32(0x04034b50);
        u16(20);
        for (int i = 0; i < 4; ++i)
            u16(0);
        u32(0);
        u32(static_cast<std::uint32_t>(data.size()));
        u32(static_cast<std::uint32_t>(data.size()));
        u16(static_cast<std::uint16_t>(name.size()));
        u16(0);
        for (char c : name)
            b.push_back(static_cast<std::byte>(c));
        b.insert(b.end(), data.begin(), data.end());
    }
    auto central = b.size();
    for (const auto& e : index) {
        u32(0x02014b50);
        u16(20);
        u16(20);
        for (int i = 0; i < 4; ++i)
            u16(0);
        u32(0);
        u32(e.size);
        u32(e.size);
        u16(static_cast<std::uint16_t>(e.name.size()));
        for (int i = 0; i < 4; ++i)
            u16(0);
        u32(0);
        u32(e.offset);
        for (char c : e.name)
            b.push_back(static_cast<std::byte>(c));
    }
    auto len = b.size() - central;
    u32(0x06054b50);
    u16(0);
    u16(0);
    u16(static_cast<std::uint16_t>(index.size()));
    u16(static_cast<std::uint16_t>(index.size()));
    u32(static_cast<std::uint32_t>(len));
    u32(static_cast<std::uint32_t>(central));
    u16(0);
    return b;
}
}  // namespace ogplay::test::resources

#include "../frontend/apk_fixture.h"
#include "ogplay/core/text.h"

namespace ogplay::test::resources {
namespace {
using Buffer = std::vector<std::byte>;

void u16(Buffer& b, std::uint16_t x) {
    b.push_back(static_cast<std::byte>(x));
    b.push_back(static_cast<std::byte>(x >> 8));
}

void u32(Buffer& b, std::uint32_t x) {
    u16(b, static_cast<std::uint16_t>(x));
    u16(b, static_cast<std::uint16_t>(x >> 16));
}

void patch(Buffer& b, std::size_t off, std::uint32_t x) {
    for (unsigned i = 0; i < 4; ++i)
        b[off + i] = static_cast<std::byte>(x >> (i * 8));
}

void append(Buffer& b, const Buffer& part) {
    b.insert(b.end(), part.begin(), part.end());
}

void header(Buffer& b, std::uint16_t type, std::uint16_t size) {
    u16(b, type);
    u16(b, size);
    u32(b, 0);
}

void finish(Buffer& b) {
    while (b.size() % 4)
        b.push_back(std::byte{});
    patch(b, 4, static_cast<std::uint32_t>(b.size()));
}

void typed(Buffer& b, res::Value v) {
    u16(b, 8);
    b.push_back(std::byte{});
    b.push_back(static_cast<std::byte>(v.type));
    u32(b, v.data);
}
}  // namespace

Buffer StringPool(std::span<const std::u16string> strings, bool utf8, bool sorted) {
    Buffer b;
    header(b, 1, 28);
    u32(b, static_cast<std::uint32_t>(strings.size()));
    u32(b, 0);
    u32(b, (utf8 ? 0x100U : 0) | (sorted ? 1U : 0));
    u32(b, static_cast<std::uint32_t>(28 + strings.size() * 4));
    u32(b, 0);
    Buffer data;
    for (const auto& s : strings) {
        u32(b, static_cast<std::uint32_t>(data.size()));
        if (utf8) {
            auto str = *ogplay::core::Utf16ToUtf8(s, ogplay::core::InvalidUtf16Policy::reject);
            const auto len = [&](std::size_t n) {
                if (n >= 128) data.push_back(static_cast<std::byte>((n >> 8) | 0x80));
                data.push_back(static_cast<std::byte>(n));
            };
            len(s.size());
            len(str.size());
            for (char c : str)
                data.push_back(static_cast<std::byte>(c));
            data.push_back(std::byte{});
        } else {
            if (s.size() >= 0x8000) u16(data, static_cast<std::uint16_t>((s.size() >> 16) | 0x8000));
            u16(data, static_cast<std::uint16_t>(s.size()));
            for (auto c : s)
                u16(data, c);
            u16(data, 0);
        }
    }
    append(b, data);
    finish(b);
    return b;
}

Buffer ResourceTable(std::span<const Entry> entries, std::uint32_t package, std::string_view packageName,
                     std::span<const std::u16string> strings, std::span<const std::u16string> typeNames) {
    constexpr std::u16string_view defaults[] = {u"attr", u"style", u"string", u"integer", u"array", u"^attr-private"};
    std::vector<std::u16string> keys;
    const std::vector<std::u16string> defaultNames(std::begin(defaults), std::end(defaults));
    if (typeNames.empty()) typeNames = defaultNames;
    for (const auto& e : entries)
        keys.emplace_back(e.name.begin(), e.name.end());
    auto types = StringPool(typeNames), names = StringPool(keys);
    Buffer pkg;
    header(pkg, 0x200, 284);
    u32(pkg, package);
    for (std::size_t i = 0; i < 128; ++i)
        u16(pkg, static_cast<std::uint16_t>(i < packageName.size() ? packageName[i] : 0));
    u32(pkg, 284);
    u32(pkg, static_cast<std::uint32_t>(typeNames.size()));
    u32(pkg, static_cast<std::uint32_t>(284 + types.size()));
    u32(pkg, static_cast<std::uint32_t>(keys.size()));
    append(pkg, types);
    append(pkg, names);
    for (std::uint32_t tid = 1; tid <= typeNames.size(); ++tid) {
        std::uint32_t count = 0;
        for (const auto& e : entries)
            if (((e.id >> 16) & 255) == tid) count = std::max(count, (e.id & 65535) + 1);
        if (!count) continue;
        Buffer spec;
        header(spec, 0x202, 16);
        u32(spec, tid);
        u32(spec, count);
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t flags = 0;
            for (const auto& e : entries)
                if (((e.id >> 16) & 255) == tid && (e.id & 65535) == i) flags |= e.flags;
            u32(spec, flags);
        }
        finish(spec);
        append(pkg, spec);
        for (std::size_t key = 0; key < entries.size(); ++key) {
            const auto& e = entries[key];
            if (((e.id >> 16) & 255) != tid) continue;
            Buffer tc;
            header(tc, 0x201, 56);
            u32(tc, tid);
            u32(tc, count);
            u32(tc, 56 + count * 4);
            const auto& c = e.config;
            u32(tc, 36);
            u16(tc, c.mcc);
            u16(tc, c.mnc);
            for (char x : c.language)
                tc.push_back(static_cast<std::byte>(x));
            for (char x : c.country)
                tc.push_back(static_cast<std::byte>(x));
            tc.push_back(static_cast<std::byte>(c.orientation));
            tc.push_back(static_cast<std::byte>(c.touchscreen));
            u16(tc, c.density);
            tc.push_back(static_cast<std::byte>(c.keyboard));
            tc.push_back(static_cast<std::byte>(c.navigation));
            tc.push_back(static_cast<std::byte>(c.inputFlags));
            tc.push_back(std::byte{});
            u16(tc, c.screenWidth);
            u16(tc, c.screenHeight);
            u16(tc, c.sdkVersion);
            u16(tc, c.minorVersion);
            tc.push_back(static_cast<std::byte>(c.screenLayout));
            tc.push_back(static_cast<std::byte>(c.uiMode));
            u16(tc, c.smallestScreenWidthDp);
            u16(tc, c.screenWidthDp);
            u16(tc, c.screenHeightDp);
            for (std::uint32_t i = 0; i < count; ++i)
                u32(tc, i == (e.id & 65535) ? 0 : res::NoIndex);
            u16(tc, e.complex ? 16 : 8);
            u16(tc, e.complex ? 1 : 0);
            u32(tc, static_cast<std::uint32_t>(key));
            if (e.complex) {
                u32(tc, e.parent);
                u32(tc, static_cast<std::uint32_t>(e.bag.size()));
                for (auto [id, v] : e.bag) {
                    u32(tc, id);
                    typed(tc, v);
                }
            } else
                typed(tc, e.value);
            finish(tc);
            append(pkg, tc);
        }
    }
    finish(pkg);
    Buffer table;
    header(table, 2, 12);
    u32(table, 1);
    append(table, StringPool(strings));
    append(table, pkg);
    finish(table);
    return table;
}

res::Source AppApk() {
    auto manifestZip = ogplay::test::MinimalApk();
    auto archive = ogplay::loader::ParseApkArchive(manifestZip);
    auto manifest = ogplay::loader::ReadApkEntry(manifestZip, archive, "AndroidManifest.xml");
    std::vector<Entry> entries = {
        {0x7f010000, "custom", {}},
        {0x7f020000,
         "AppTheme",
         {},
         true,
         0x0103006b,
         {{0x01010098, {1, 0x7f030000}}, {0x01010036, {1, 0x0106000b}}, {0x7f010000, {2, 0x01010036}}}},
        {0x7f030000, "cross_package", {1, 0x0104000a}},
        {0x7f040000, "integer", {0x10, 42}},
        {0x7f050000,
         "mixed",
         {},
         true,
         0,
         {{0x02000000, {1, 0x7f030000}}, {0x02000001, {0x10, 7}}, {0x02000002, {1, 0}}}}};
    auto arsc = ResourceTable(entries);
    std::vector<std::pair<std::string, Buffer>> files = {{"AndroidManifest.xml", std::move(manifest)},
                                                         {"resources.arsc", std::move(arsc)},
                                                         {"assets/shared.txt", {std::byte{'a'}}},
                                                         {"assets/app.txt", {std::byte{'b'}}}};
    return OwnedSource(Zip(files));
}
}  // namespace ogplay::test::resources
