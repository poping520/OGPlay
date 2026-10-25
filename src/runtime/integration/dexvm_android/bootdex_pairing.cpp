#include "ogplay/runtime/integration/dexvm_android.h"

#include "ogplay/core/json.h"
#include "ogplay/core/sha256.h"
#include "ogplay/runtime/dexvm/class_linker.h"

#include <algorithm>
#include <stdexcept>

namespace ogplay::runtime {
namespace {
constexpr std::string_view kPairing = "META-INF/ogplay/vm-pairing.json";
constexpr std::string_view kProjection = "META-INF/ogplay/theme-projection.json";

bool JavaMeasuredField(core::JsonValue root) {
    const auto classes = root.Member("classes").value_or(core::JsonValue{});
    for (std::size_t i = 0; i < classes.Size(); ++i) {
        const auto cls = classes.Element(i).value_or(core::JsonValue{});
        if (cls.Member("owner").value_or(core::JsonValue{}).String() != "Landroid/view/View;") continue;
        const auto fields = cls.Member("fields").value_or(core::JsonValue{});
        for (std::size_t j = 0; j < fields.Size(); ++j) {
            const auto field = fields.Element(j).value_or(core::JsonValue{});
            if (field.Member("signature").value_or(core::JsonValue{}).String() == "mMeasuredWidth:I")
                return field.Member("source").value_or(core::JsonValue{}).String() == "rom";
        }
    }
    return false;
}

core::JsonDocument Parse(std::string_view text) {
    core::JsonParseError error;
    auto document = core::JsonDocument::ParseStrict(text, error, 16U * 1024U * 1024U);
    if (!document) throw std::runtime_error("BootDex pairing: invalid JSON: " + error.message);
    return std::move(*document);
}
}  // namespace

std::shared_ptr<DexVmAndroidContext> AssembleAndroidContext(std::vector<std::byte> boot_archive,
                                                            AndroidPlatformServices platform) {
    const auto archive = loader::ParseApkArchive(boot_archive);
    const auto has = [&](std::string_view name) {
        return std::any_of(archive.entries.begin(), archive.entries.end(),
                           [&](const auto& entry) { return entry.name == name; });
    };
    const auto read = [&](std::string_view name) {
        if (!has(name)) throw std::runtime_error("BootDex pairing: missing " + std::string(name));
        return loader::ReadStoredApkEntry(boot_archive, archive, name);
    };
    const auto sources = dexvm::DexClassLinker::BootDexMemberSources(boot_archive);
    const bool java_fields = !sources.empty() && JavaMeasuredField(Parse(sources).Root());
    auto owner = AndroidUiTraversalOwner::Legacy;
    if (has(kPairing)) {
        const auto bytes = read(kPairing);
        const auto document = Parse({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
        const auto root = document.Root();
        if (root.Member("schema").value_or(core::JsonValue{}).Integer() != 1 ||
            root.Member("mode").value_or(core::JsonValue{}).String() != "java-traversal" ||
            root.Member("version").value_or(core::JsonValue{}).Integer() != 1)
            throw std::runtime_error("BootDex pairing: unknown mode/schema/version");
        const auto verify = [&](std::string_view key, std::span<const std::byte> payload) {
            if (root.Member(key).value_or(core::JsonValue{}).String() != core::Sha256(payload))
                throw std::runtime_error("BootDex pairing: identity mismatch: " + std::string(key));
        };
        verify("dex_sha256", read("classes.dex"));
        verify("member_sources_sha256", std::as_bytes(std::span(sources.data(), sources.size())));
        const auto projection = read(kProjection);
        verify("theme_projection_sha256", projection);
        if (!java_fields) throw std::runtime_error("BootDex pairing: JavaTraversal requires ROM View state");
        // Validate the projection's schema/ROM binding as well as its sealed hash.
        ThemeResources checked_projection;
        checked_projection.Load({reinterpret_cast<const char*>(projection.data()), projection.size()});
        owner = AndroidUiTraversalOwner::JavaTraversal;
    } else if (java_fields || has(kProjection)) {
        throw std::runtime_error("BootDex pairing: experimental payload lacks sealed mode; no Legacy fallback");
    }
    const auto context = std::make_shared<DexVmAndroidContext>(owner, platform);
    context->boot_classpath_archive = archive;
    context->boot_classpath_bytes = std::move(boot_archive);
    return context;
}
}  // namespace ogplay::runtime
