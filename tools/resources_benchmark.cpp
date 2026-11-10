// Host-only resource-core benchmark and streaming inspection. No VM startup.
#include "ogplay/runtime/resources/resources.h"
#include "ogplay/core/json.h"
#include "ogplay/hal/clock.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <malloc/malloc.h>
#endif
#endif
namespace res = ogplay::runtime::resources;

namespace {
class Mapping {
public:
    res::Bytes bytes;

    explicit Mapping(const char* path) {
#if defined(_WIN32)
        file_ = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) throw std::runtime_error("open mapping failed");
        LARGE_INTEGER length;
        if (!GetFileSizeEx(file_, &length)) throw std::runtime_error("stat mapping failed");
        size_ = static_cast<std::size_t>(length.QuadPart);
        map_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        data_ = MapViewOfFile(map_, FILE_MAP_READ, 0, 0, 0);
        if (!data_) throw std::runtime_error("map failed");
#else
        file_ = ::open(path, O_RDONLY);
        if (file_ < 0) throw std::runtime_error("open mapping failed");
        struct stat info{};
        if (fstat(file_, &info)) throw std::runtime_error("stat mapping failed");
        size_ = static_cast<std::size_t>(info.st_size);
        data_ = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, file_, 0);
        if (data_ == MAP_FAILED) throw std::runtime_error("map failed");
#endif
        bytes = {static_cast<const std::byte*>(data_), size_};
    }

    ~Mapping() {
#if defined(_WIN32)
        if (data_) UnmapViewOfFile(data_);
        if (map_) CloseHandle(map_);
        if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
#else
        if (data_ != MAP_FAILED) munmap(data_, size_);
        if (file_ >= 0) ::close(file_);
#endif
    }

private:
    void* data_{};
    std::size_t size_{};
#if defined(_WIN32)
    HANDLE file_{INVALID_HANDLE_VALUE}, map_{};
#else
    int file_{-1};
#endif
};

std::uint64_t resident() {
#if defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        throw std::runtime_error("RSS unavailable");
    return info.resident_size;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS info{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info))) throw std::runtime_error("RSS unavailable");
    return info.WorkingSetSize;
#else
    std::uint64_t total = 0, pages = 0;
    std::ifstream("/proc/self/statm") >> total >> pages;
    return pages * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
#endif
}

std::uint64_t allocated() {
#if defined(__APPLE__)
    malloc_statistics_t stats{};
    malloc_zone_statistics(nullptr, &stats);
    return stats.size_in_use;
#else
    return 0;
#endif
}

std::uint64_t now() {
    return ogplay::hal::Clock::SteadyTimestampNs();
}

void dump(const res::ResTable& table, const char* path) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot create inspection output");
    table.forEachVariant([&](const res::ResourceVariant& v) {
        ogplay::core::JsonWriter json;
        auto root = json.Object();
        json.AddUnsignedInteger(root, "id", v.id);
        json.AddString(root, "name", table.getResourceName(v.id)->fullName());
        auto config = json.Array();
        const auto& c = v.config;
        for (std::uint32_t x :
             {c.mcc, c.mnc,
              static_cast<std::uint16_t>(static_cast<unsigned char>(c.language[0]) |
                                         (static_cast<unsigned char>(c.language[1]) << 8)),
              static_cast<std::uint16_t>(static_cast<unsigned char>(c.country[0]) |
                                         (static_cast<unsigned char>(c.country[1]) << 8)),
              static_cast<std::uint16_t>(c.orientation), static_cast<std::uint16_t>(c.touchscreen), c.density,
              static_cast<std::uint16_t>(c.keyboard), static_cast<std::uint16_t>(c.navigation),
              static_cast<std::uint16_t>(c.inputFlags), c.screenWidth, c.screenHeight, c.sdkVersion, c.minorVersion,
              static_cast<std::uint16_t>(c.screenLayout), static_cast<std::uint16_t>(c.uiMode), c.smallestScreenWidthDp,
              c.screenWidthDp, c.screenHeightDp})
            json.Append(config, json.UnsignedInteger(x));
        json.Add(root, "config", config);
        json.AddBool(root, "complex", v.complex);
        json.AddUnsignedInteger(root, "parent", v.parent);
        const auto val = [&](res::Value v) {
            auto a = json.Array();
            json.Append(a, json.UnsignedInteger(v.type));
            json.Append(a, json.UnsignedInteger(v.data));
            if (v.type == 3) json.Append(a, json.String(table.strings(1).stringAt(v.data)));
            return a;
        };
        json.Add(root, "value", val(v.value));
        auto bag = json.Array();
        for (auto [id, value] : v.bag) {
            auto item = json.Array();
            json.Append(item, json.UnsignedInteger(id));
            json.Append(item, val(value));
            json.Append(bag, item);
        }
        json.Add(root, "bag", bag);
        out << json.Serialize(root) << '\n';
    });
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3)
            throw std::runtime_error("usage: resources_benchmark framework-res.apk [variants.jsonl]");
        const auto rss0 = resident(), heap0 = allocated(), start = now();
        auto mapping = std::make_shared<Mapping>(argv[1]);
        const auto mapped = now();
        auto framework = res::sharedFramework([&] { return res::Source{mapping, mapping->bytes, argv[1], {}}; });
        const auto indexed = now(), rss1 = resident(), heap1 = allocated();
        res::AssetManager manager(framework);
        auto& table = manager.resources();
        res::ResTable_config config;
        config.sdkVersion = 19;
        config.density = 240;
        config.orientation = 2;
        config.screenWidthDp = 533;
        config.screenHeightDp = 320;
        config.smallestScreenWidthDp = 320;
        table.setParameters(config);
        auto id = table.identifierForName("android:style/Theme.Holo");
        const auto heapNames = allocated();
        const auto coldStart = now();
        std::optional<res::Theme> coldTheme;
        coldTheme.emplace(table);
        coldTheme->applyStyle(id, true);
        const auto coldNs = now() - coldStart;
        coldTheme.reset();
        const auto heapBags = allocated();
        coldTheme.emplace(table);
        coldTheme->applyStyle(id, true);
        std::vector<std::uint64_t> samples;
        for (unsigned i = 0; i < 1000; ++i) {
            res::Theme theme(table);
            auto begin = now();
            theme.applyStyle(id, true);
            samples.push_back(now() - begin);
        }
        const std::array attrs = {0x01010034U, 0x01010036U, 0x01010048U, 0x0101004dU, 0x01010054U};
        std::vector<std::uint64_t> typedArraySamples;
        for (unsigned i = 0; i < 1000; ++i) {
            const auto begin = now();
            auto styled = res::applyStyle(*coldTheme, 0, id, nullptr, attrs);
            typedArraySamples.push_back(now() - begin);
            if (styled.values.size() != attrs.size() * 6) throw std::runtime_error("bad style benchmark result");
        }
        std::sort(typedArraySamples.begin(), typedArraySamples.end());
        std::sort(samples.begin(), samples.end());
        std::cout << "{\"mapping_ms\":" << static_cast<double>(mapped - start) / 1e6
                  << ",\"index_ms\":" << static_cast<double>(indexed - mapped) / 1e6
                  << ",\"first_total_ms\":" << static_cast<double>(indexed - start) / 1e6 << ",\"rss_before\":" << rss0
                  << ",\"rss_after\":" << rss1
                  << ",\"rss_delta\":" << static_cast<std::int64_t>(rss1) - static_cast<std::int64_t>(rss0)
                  << ",\"heap_index_delta\":" << static_cast<std::int64_t>(heap1) - static_cast<std::int64_t>(heap0)
                  << ",\"heap_names_delta\":" << static_cast<std::int64_t>(heapNames) - static_cast<std::int64_t>(heap1)
                  << ",\"heap_bags_delta\":"
                  << static_cast<std::int64_t>(heapBags) - static_cast<std::int64_t>(heapNames)
                  << ",\"theme_cold_us\":" << static_cast<double>(coldNs) / 1e3
                  << ",\"theme_median_us\":" << static_cast<double>(samples[500]) / 1e3
                  << ",\"theme_p95_us\":" << static_cast<double>(samples[950]) / 1e3
                  << ",\"typed_array_median_us\":" << static_cast<double>(typedArraySamples[500]) / 1e3
                  << ",\"typed_array_p95_us\":" << static_cast<double>(typedArraySamples[950]) / 1e3 << "}\n";
        if (argc == 3) dump(table, argv[2]);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
