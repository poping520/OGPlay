#include "ogplay/runtime/integration/dexvm_android.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace ogplay::runtime {
namespace {
bool HasEntry(const loader::ApkArchive& archive, const std::string_view name) {
    return std::any_of(archive.entries.begin(), archive.entries.end(),
                       [name](const loader::ApkEntry& entry) { return entry.name == name; });
}
}  // namespace

std::vector<BootClasspathJar> ReadBootClasspath(const std::filesystem::path& framework_directory) {
    std::vector<BootClasspathJar> jars;
    jars.reserve(kBootClasspathJars.size());
    for (const auto name : kBootClasspathJars) {
        const auto path = framework_directory / std::filesystem::path(std::string(name));
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("boot classpath archive is missing: " + path.string());
        const std::vector<char> raw{std::istreambuf_iterator<char>(input), {}};
        BootClasspathJar jar;
        jar.guest_path = std::string(kBootClasspathDirectory) + std::string(name);
        jar.bytes.resize(raw.size());
        std::memcpy(jar.bytes.data(), raw.data(), raw.size());
        jars.push_back(std::move(jar));
    }
    return jars;
}

std::shared_ptr<DexVmAndroidContext> AssembleAndroidContext(std::vector<BootClasspathJar> boot_classpath) {
    if (boot_classpath.empty()) throw std::runtime_error("boot classpath is empty");
    for (auto& jar : boot_classpath) {
        jar.archive = loader::ParseApkArchive(jar.bytes);
        if (!HasEntry(jar.archive, "classes.dex"))
            throw std::runtime_error("boot classpath archive has no classes.dex: " + jar.guest_path);
    }
    auto context = std::make_shared<DexVmAndroidContext>();
    context->boot_classpath = std::move(boot_classpath);
    return context;
}

std::vector<std::vector<std::uint8_t>> BootClasspathDexFiles(const std::span<const BootClasspathJar> jars) {
    std::vector<std::vector<std::uint8_t>> files;
    for (const auto& jar : jars) {
        for (std::size_t index = 1;; ++index) {
            const auto name = index == 1 ? std::string("classes.dex") : "classes" + std::to_string(index) + ".dex";
            if (!HasEntry(jar.archive, name)) break;
            const auto dex = loader::ReadApkEntry(jar.bytes, jar.archive, name);
            auto& bytes = files.emplace_back(dex.size());
            std::memcpy(bytes.data(), dex.data(), dex.size());
        }
    }
    return files;
}

}  // namespace ogplay::runtime
