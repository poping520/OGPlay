#include "dexvm/resource_fixture.h"
#include "boot_dex.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"

#include <cstring>

#include "../runtime/resources_test_support.h"
#include "ogplay/core/text.h"
#include "ogplay/runtime/vfs/vfs.h"
#include <fstream>

namespace ogplay::test {
std::vector<std::byte> ReadTestSystemFont(std::string_view name) {
    std::ifstream stream(std::filesystem::path(OGPLAY_SOURCE_DIR) / "data/android/19/fonts" / name,
                         std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("test font is missing: " + std::string(name));
    std::vector<std::byte> bytes(static_cast<std::size_t>(stream.tellg()));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("failed to read test font");
    return bytes;
}

void AttachViewTestContext(runtime::dexvm::Interpreter& vm, runtime::dexvm::VmObjectRef owner) {
    using namespace runtime::dexvm;
    const auto roots = vm.ProtectReferences(std::array{owner});
    const auto wrapper = vm.Linker().ResolveDescriptor("Landroid/content/ContextWrapper;");
    for (unsigned depth = 0; owner.IsValid() && depth < 16; ++depth) {
        const auto type = vm.Model().ObjectClass(owner);
        if (!vm.Linker().IsAssignable(wrapper, type)) return;
        const auto field = vm.Linker().FindFieldRecursive(type, "mBase", "Landroid/content/Context;");
        if (!field) throw std::runtime_error("fixture ContextWrapper has no base field");
        const VmObjectRef base(vm.Model().InstanceSlots(owner)[vm.Linker().Field(*field).slot].bits);
        if (base.IsValid()) {
            owner = base;
            continue;
        }
        const auto context = vm.NewIntrinsicInstance("Landroid/content/Context;");
        const auto slot = vm.Linker().FindVtableIndex(type, "attachBaseContext", "(Landroid/content/Context;)V");
        if (!slot) throw std::runtime_error("fixture ContextWrapper has no attachBaseContext");
        const auto result =
            vm.Call(vm.Linker().Class(type).vtable[*slot], std::array{VmValue::Ref(owner), VmValue::Ref(context)});
        if (result.exception.IsValid()) throw std::runtime_error(result.exception_message);
        return;
    }
    if (owner.IsValid()) throw std::runtime_error("fixture ContextWrapper cycle or depth limit");
}

ResourceFixtureTable ReadResourceFixture(std::span<const std::byte> input) {
    namespace res = runtime::resources;
    auto bytes = std::make_shared<const std::vector<std::byte>>(input.begin(), input.end());
    res::ResTable table;
    table.add(std::make_shared<res::ResTableData>(res::Source{bytes, *bytes, "fixture", {}}), 1);
    ResourceFixtureTable result;
    table.forEachVariant([&](const res::ResourceVariant& v) {
        const auto name = table.getResourceName(v.id).value();
        result.package_id = v.id >> 24;
        result.package_name = name.package;
        auto& e = result.entries.emplace_back(ResourceFixtureEntry{.resource_id = v.id,
                                                                   .type_name = name.type,
                                                                   .entry_name = name.name,
                                                                   .value_type = v.value.type,
                                                                   .value_data = v.value.data,
                                                                   .is_complex = v.complex,
                                                                   .parent = v.parent,
                                                                   .locale = v.config.localeName(),
                                                                   .orientation = v.config.orientation,
                                                                   .density = v.config.density,
                                                                   .sdk_version = v.config.sdkVersion,
                                                                   .screen_layout = v.config.screenLayout,
                                                                   .smallest_width_dp = v.config.smallestScreenWidthDp,
                                                                   .screen_width_dp = v.config.screenWidthDp,
                                                                   .screen_height_dp = v.config.screenHeightDp});
        std::replace(e.locale.begin(), e.locale.end(), '_', '-');
        if (v.value.type == 3) e.string_value = table.strings(1).stringAt(v.value.data);
        for (const auto& [id, value] : v.bag)
            e.bag.push_back({id, value.type, value.data,
                             value.type == 3 ? std::optional(table.strings(1).stringAt(value.data)) : std::nullopt});
    });
    return result;
}

ResourceFixtureTable& ResourceFixture(runtime::DexVmAndroidContext& context) {
    struct Fixture {
        std::weak_ptr<const void> owner;
        ResourceFixtureTable table;
    };

    static std::unordered_map<const void*, Fixture> fixtures;
    std::erase_if(fixtures, [](const auto& row) { return row.second.owner.expired(); });
    auto& fixture = fixtures[&context];
    fixture.owner = context.skia_objects;
    return fixture.table;
}

void MountResourceFixture(runtime::DexVmAndroidContext& context, runtime::dexvm::Interpreter& vm) {
    if (!context.vfs || (context.apk_bytes.empty() && ogplay::test::ResourceFixture(context).entries.empty())) return;
    namespace res = runtime::resources;
    std::vector<std::pair<std::string, std::vector<std::byte>>> entries;
    loader::ApkArchive archive;
    if (!context.apk_bytes.empty()) {
        try {
            archive = loader::ParseApkArchive(context.apk_bytes);
        } catch (const std::runtime_error&) {
            return;
        }
    }
    if (!ogplay::test::ResourceFixture(context).entries.empty()) {
        for (const auto& entry : archive.entries)
            if (entry.name != "resources.arsc")
                entries.emplace_back(entry.name, loader::ReadApkEntry(context.apk_bytes, archive, entry.name));
        std::vector<std::u16string> types, strings;
        std::vector<resources::Entry> rows;
        if (!ogplay::test::ResourceFixture(context).global_string_pool.empty()) {
            res::ResStringPool pool(ogplay::test::ResourceFixture(context).global_string_pool);
            for (std::size_t i = 0; i < pool.size(); ++i)
                strings.push_back(pool.string16At(i));
        }
        const auto value = [&](std::uint8_t type, std::uint32_t data, const std::optional<std::string>& text) {
            if (text) {
                type = 3;
                const auto decoded = vm.Model().StringValue(vm.NewStringUtf8(*text));
                auto found = std::ranges::find(strings, decoded);
                if (found == strings.end()) {
                    strings.push_back(decoded);
                    found = strings.end() - 1;
                }
                data = static_cast<std::uint32_t>(found - strings.begin());
            }
            return res::Value{type, data};
        };
        for (const auto& row : ogplay::test::ResourceFixture(context).entries) {
            const auto type = (row.resource_id >> 16) & 255U;
            if (!type) continue;
            if (types.size() < type) types.resize(type);
            types[type - 1] = vm.Model().StringValue(vm.NewStringUtf8(row.type_name));
            auto& item = rows.emplace_back(resources::Entry{
                .id = row.resource_id,
                .name = row.entry_name,
                .value = value(row.value_type, row.value_data, row.string_value),
                .complex = row.is_complex,
                .parent = row.parent,
                .config = {.orientation = row.orientation,
                           .density = row.density,
                           .sdkVersion = row.sdk_version,
                           .screenLayout = row.screen_layout,
                           .smallestScreenWidthDp = row.smallest_width_dp,
                           .screenWidthDp = row.screen_width_dp,
                           .screenHeightDp = row.screen_height_dp},
                .flags = row.changing_configurations,
            });
            for (const auto& child : row.bag)
                item.bag.emplace_back(child.name, value(child.value_type, child.value_data, child.string_value));
            if (row.locale.size() >= 2) std::copy_n(row.locale.begin(), 2, item.config.language);
            if (row.locale.size() >= 5) std::copy_n(row.locale.begin() + 3, 2, item.config.country);
        }
        if (!rows.empty()) {
            auto package = rows.front().id >> 24;
            std::set<std::uint32_t> existing, keys;
            for (const auto& row : rows) {
                existing.insert(row.id);
                for (const auto& [id, unused] : row.bag) {
                    static_cast<void>(unused);
                    if (id >> 24 == package && ((id >> 16) & 255U) != 0) keys.insert(id);
                }
            }
            for (const auto id : keys) {
                if (existing.contains(id)) continue;
                const auto type = (id >> 16) & 255U;
                if (types.size() < type) types.resize(type);
                if (types[type - 1].empty()) types[type - 1] = u"attr";
                rows.push_back({.id = id, .name = "attr" + std::to_string(id), .value = {}});
            }
            auto name = ogplay::test::ResourceFixture(context).package_name.empty()
                            ? context.package_name
                            : ogplay::test::ResourceFixture(context).package_name;
            if (name.empty()) name = "fixture";
            entries.emplace_back("resources.arsc", resources::ResourceTable(rows, package, name, strings, types));
            context.apk_bytes = resources::Zip(entries);
        }
    }
    if (context.apk_bytes.empty()) return;
    if (context.sealed_apk_bytes && *context.sealed_apk_bytes == context.apk_bytes) return;
    if (context.sealed_apk_bytes) context.package_resource_path += ".fixture";
    if (context.package_resource_path.empty()) context.package_resource_path = "/data/app/fixture-1.apk";
    const auto bytes = std::make_shared<const std::vector<std::byte>>(context.apk_bytes);
    context.sealed_apk_bytes = bytes;
    context.vfs->PutFile(context.package_resource_path, *bytes, false);
    if (const auto found = context.singletons.find("resources"); found != context.singletons.end()) {
        const auto resourceClass = vm.Model().ObjectClass(found->second);
        const auto field =
            vm.Linker().FindFieldRecursive(resourceClass, "mAssets", "Landroid/content/res/AssetManager;");
        const auto assets =
            runtime::dexvm::VmObjectRef(vm.Model().InstanceSlots(found->second)[vm.Linker().Field(*field).slot].bits);
        const auto type = vm.Model().ObjectClass(assets);
        const auto slot = vm.Linker().FindVtableIndex(type, "addAssetPath", "(Ljava/lang/String;)I");
        const auto result =
            vm.Call(vm.Linker().Class(type).vtable[*slot],
                    std::array{runtime::dexvm::VmValue::Ref(assets),
                               runtime::dexvm::VmValue::Ref(vm.NewStringUtf8(context.package_resource_path))});
        if (result.exception.IsValid() || !result.value.AsInt())
            throw std::runtime_error("cannot extend fixture resource APK: " + result.exception_message);
        const auto make = vm.Linker().FindVtableIndex(type, "makeStringBlocks", "(Z)V");
        const auto strings = vm.Call(vm.Linker().Class(type).vtable[*make],
                                     std::array{runtime::dexvm::VmValue::Ref(assets), runtime::dexvm::VmValue::Int(1)});
        if (strings.exception.IsValid()) throw std::runtime_error(strings.exception_message);
        // Fixture mutation models replacing an installed APK. Drop ROM drawable caches
        // so the same path/string index can be reused with different fixture density.
        for (const auto name : {"mDrawableCache", "mColorDrawableCache"}) {
            const auto cacheField =
                vm.Linker().FindFieldRecursive(resourceClass, name, "Landroid/util/LongSparseArray;");
            if (!cacheField) continue;
            const runtime::dexvm::VmObjectRef cache(
                vm.Model().InstanceSlots(found->second)[vm.Linker().Field(*cacheField).slot].bits);
            if (!cache.IsValid()) continue;
            const auto clear = vm.Linker().FindVtableIndex(vm.Model().ObjectClass(cache), "clear", "()V");
            const auto cleared = vm.Call(vm.Linker().Class(vm.Model().ObjectClass(cache)).vtable[*clear],
                                         std::array{runtime::dexvm::VmValue::Ref(cache)});
            if (cleared.exception.IsValid()) throw std::runtime_error(cleared.exception_message);
        }
    }
}

void BindBootDexPlatformNatives(runtime::dexvm::DexClassLinker& linker) {
    const auto context = std::make_shared<runtime::DexVmAndroidContext>();
    BindBootDexArchive(*context);
    for (const auto& declaration : runtime::AndroidIntrinsicCatalog(context)) {
        if (!linker.FindClass(declaration.descriptor)) linker.RegisterIntrinsics(std::array{declaration});
    }
}
}  // namespace ogplay::test
