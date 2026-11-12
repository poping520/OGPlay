// API19 ROM resource native bridge. Algorithms live in runtime/resources.
#include "catalog.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <unordered_map>

#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/integration/apk_assets.h"

namespace ogplay::runtime {
namespace dx = dexvm;
namespace res = resources;

namespace {
using V = dx::VmValue;
using Call = dx::IntrinsicContext;
constexpr auto kNull = "Ljava/lang/NullPointerException;";
constexpr auto kState = "Ljava/lang/IllegalStateException;";
constexpr auto kBounds = "Ljava/lang/IndexOutOfBoundsException;";
constexpr auto kFile = "Ljava/io/FileNotFoundException;";
constexpr auto kIo = "Ljava/io/IOException;";
constexpr auto kArgument = "Ljava/lang/IllegalArgumentException;";

template <class T, std::uint32_t Kind>
class Handles {
public:
    using Token = std::uint32_t;

    Token add(std::shared_ptr<T> value) {
        const auto id = next_.fetch_add(1, std::memory_order_relaxed);
        if (id > 0x0fffffff) throw dx::VmJavaThrow{kState, "resource handle budget exhausted"};
        const auto token = Kind | static_cast<Token>(id);
        values_.emplace(token, std::move(value));
        return token;
    }

    std::shared_ptr<T> get(Token token) const {
        if (!token) throw dx::VmJavaThrow{kNull, "resource handle is null"};
        const auto found = values_.find(token);
        if (found == values_.end()) throw dx::VmJavaThrow{kState, "stale or invalid resource handle"};
        return found->second;
    }

    std::shared_ptr<T> remove(Token token) {
        auto value = get(token);
        values_.erase(token);
        return value;
    }

    void retire(Token token) { values_.erase(token); }

private:
    inline static std::atomic<std::uint64_t> next_{1};
    std::unordered_map<Token, std::shared_ptr<T>> values_;
};

std::atomic<std::size_t> managerCount{};

struct Manager {
    std::shared_ptr<ResourceAssetManager> assetManager;
    std::unordered_map<std::int32_t, std::uint32_t> pools;

    explicit Manager(std::shared_ptr<ResourceAssetManager> value) : assetManager(std::move(value)) { ++managerCount; }

    ~Manager() { --managerCount; }
};

struct ThemeHandle {
    std::shared_ptr<Manager> owner;
    res::Theme theme;

    explicit ThemeHandle(std::shared_ptr<Manager> manager)
        : owner(std::move(manager)), theme(owner->assetManager->core.resources()) {}
};

struct AssetHandle {
    std::shared_ptr<Manager> owner;
    std::shared_ptr<res::Asset> asset;
};

struct PoolHandle {
    std::shared_ptr<const res::ResStringPool> owned;
    const res::ResStringPool* pool;
};

struct XmlHandle {
    res::ResXMLTree tree;
    std::uint32_t pool{};
    std::size_t parsers{};
    bool retired{};

    explicit XmlHandle(res::Source source) : tree(std::move(source)) {}
};

struct ParserHandle {
    std::shared_ptr<XmlHandle> document;
    res::ResXMLParser parser;

    explicit ParserHandle(std::shared_ptr<XmlHandle> owner) : document(std::move(owner)), parser(document->tree) {}
};
}  // namespace

struct ResourceNativeState {
    Handles<Manager, 0x10000000> managers;
    Handles<AssetHandle, 0x20000000> assets;
    Handles<ThemeHandle, 0x30000000> themes;
    Handles<XmlHandle, 0x40000000> documents;
    Handles<ParserHandle, 0x50000000> parsers;
    Handles<PoolHandle, 0x60000000> pools;
};

namespace {
ResourceNativeState& State(DexVmAndroidContext& context) {
    if (!context.resource_natives) context.resource_natives = std::make_shared<ResourceNativeState>();
    return *context.resource_natives;
}

std::size_t Field(dx::Interpreter& vm, dx::VmObjectRef object, const char* name, const char* type) {
    if (!object.IsValid()) throw dx::VmJavaThrow{kNull, name};
    const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), name, type);
    if (!field) throw std::logic_error(std::string("missing ROM resource field ") + name);
    return vm.Linker().Field(*field).slot;
}

std::uint32_t ManagerToken(dx::Interpreter& vm, dx::VmObjectRef object) {
    return vm.Model().InstanceSlots(object)[Field(vm, object, "mObject", "I")].bits;
}

std::shared_ptr<Manager> ManagerFor(Call& call, ResourceNativeState& state) {
    return state.managers.get(ManagerToken(call.vm, call.receiver));
}

template <class T>
V Int(T value) {
    return V::Int(static_cast<std::int32_t>(value));
}

std::uint32_t U(Call& call, std::size_t index) {
    return static_cast<std::uint32_t>(call.arguments[index].AsInt());
}

std::string String(Call& call, std::size_t index) {
    return call.vm.StringUtf8(dx::IntrinsicCall(call).NonNullRef(index, "resource name"));
}

std::optional<std::string> OptionalString(Call& call, std::size_t index) {
    if (const auto ref = call.arguments[index].ref; ref.IsValid()) return call.vm.StringUtf8(ref);
    return {};
}

template <class Operation>
V Checked(const char* exception, Operation&& operation) {
    try {
        return operation();
    } catch (const std::runtime_error& e) {
        throw dx::VmJavaThrow{exception, e.what()};
    }
}

void WriteInts(Call& c, dx::VmObjectRef array, std::span<const std::int32_t> values) {
    if (!array.IsValid()) throw dx::VmJavaThrow{kNull, "resource output array"};
    if (values.size() > static_cast<std::size_t>(c.vm.Model().ArrayLength(array)))
        throw dx::VmJavaThrow{kBounds, "resource output array is too small"};
    for (std::size_t i = 0; i < values.size(); ++i)
        c.vm.Model().SetPrimitiveElement(array, static_cast<JniSize>(i), static_cast<std::uint32_t>(values[i]));
}

V IntArray(Call& c, std::span<const std::int32_t> values) {
    const auto array = c.vm.Model().NewPrimitiveArray(c.vm.Linker().ResolveDescriptor("[I"), JniPrimitiveKind::integer,
                                                      static_cast<JniSize>(values.size()));
    WriteInts(c, array, values);
    return V::Ref(array);
}

template <class StringAt>
V Strings(Call& c, std::size_t size, StringAt&& at) {
    const auto array =
        c.vm.Model().NewObjectArray(c.vm.Linker().ResolveDescriptor("[Ljava/lang/String;"),
                                    c.vm.Linker().ResolveDescriptor("Ljava/lang/String;"), static_cast<JniSize>(size));
    const auto root = c.vm.ProtectReferences(std::array{array});
    for (std::size_t i = 0; i < size; ++i)
        c.vm.Model().SetObjectElement(array, static_cast<JniSize>(i), at(i));
    return V::Ref(array);
}

std::vector<std::uint32_t> Attrs(Call& c, std::size_t index) {
    auto array = dx::IntrinsicCall(c).NonNullRef(index, "attribute IDs");
    std::vector<std::uint32_t> attrs(static_cast<std::size_t>(c.vm.Model().ArrayLength(array)));
    for (std::size_t i = 0; i < attrs.size(); ++i)
        attrs[i] = static_cast<std::uint32_t>(c.vm.Model().GetPrimitiveElement(array, static_cast<JniSize>(i)));
    return attrs;
}

V StyleOutput(Call& c, const res::StyleResult& result, std::size_t dataIndex, std::size_t indicesIndex) {
    const auto indices = c.arguments[indicesIndex].ref;
    WriteInts(c, c.arguments[dataIndex].ref, result.values);
    // KitKat permits absent and undersized indices buffers.
    if (indices.IsValid() && static_cast<std::size_t>(c.vm.Model().ArrayLength(indices)) > result.values.size() / 6)
        WriteInts(c, indices, result.indices);
    return Int(1);
}

V OutputValue(Call& c, const res::ResTable& table, const std::optional<res::TypedValue>& value, std::size_t index,
              bool writeDensity) {
    const auto object = dx::IntrinsicCall(c).NonNullRef(index, "TypedValue");
    if (!value) return V::Int(-1);
    const auto slots = value->slots();
    constexpr std::array names{"type", "data", "assetCookie", "resourceId", "changingConfigurations", "density"};
    for (std::size_t i = 0; i < names.size() - (writeDensity ? 0U : 1U); ++i)
        c.vm.Model().InstanceSlots(object)[Field(c.vm, object, names[i], "I")] = {static_cast<std::uint32_t>(slots[i]),
                                                                                  dx::SlotTag::cat1};
    const auto string = Field(c.vm, object, "string", "Ljava/lang/CharSequence;");
    c.vm.Model().InstanceSlots(object)[string] = {0, dx::SlotTag::ref};
    return Int(table.stringBlock(value->cookie));
}

std::shared_ptr<std::vector<std::byte>> CopyBytes(Call& c) {
    const auto data = dx::IntrinsicCall(c).NonNullRef(0, "compiled resource bytes");
    const auto start = c.arguments[1].AsInt(), length = c.arguments[2].AsInt();
    if (start < 0 || length < 0 || start > c.vm.Model().ArrayLength(data) - length)
        throw dx::VmJavaThrow{kBounds, "compiled resource byte array range"};
    return std::make_shared<std::vector<std::byte>>(c.vm.Model().ReadByteRegion(data, start, length));
}

std::unique_ptr<res::Asset> Open(Manager& manager, std::string_view path, std::optional<std::int32_t> cookie) {
    auto& core = manager.assetManager->core;
    auto asset = cookie ? core.openNonAsset(path, *cookie) : core.open(path);
    if (!asset) throw dx::VmJavaThrow{kFile, std::string(path)};
    return asset;
}

V OpenXml(Call& c, ResourceNativeState& state, Manager& manager) {
    return Checked(kFile, [&] {
        const auto path = String(c, 1);
        auto asset = Open(manager, path, c.arguments[0].AsInt());
        res::Source source;
        if (asset->isStored()) {
            auto range = asset->openFd();
            source = {std::move(range.source.owner), asset->storedBuffer(), path, {}};
        } else {
            const auto bytes = std::make_shared<const std::vector<std::byte>>(ReadResourceAsset(*asset, 32U << 20));
            source = {bytes, *bytes, path, {}};
        }
        return Int(state.documents.add(std::make_shared<XmlHandle>(std::move(source))));
    });
}

V OpenFd(Call& c, Manager& manager, std::string_view path, std::optional<std::int32_t> cookie,
         dx::VmObjectRef offsets) {
    using android_intrinsics::CallAndroidMethod;
    if (!offsets.IsValid()) throw dx::VmJavaThrow{kNull, "asset FD offsets"};
    if (c.vm.Model().ArrayLength(offsets) < 2) throw dx::VmJavaThrow{kBounds, "asset FD offsets"};
    const auto range = Open(manager, path, cookie)->openFd();
    if (range.source.isUpToDate && !range.source.isUpToDate()) throw dx::VmJavaThrow{kFile, "asset archive changed"};
    const auto descriptor = c.vm.NewIntrinsicInstance("Ljava/io/FileDescriptor;");
    const auto root = c.vm.ProtectReferences(std::array{descriptor});
    const auto file = c.vm.IO().OpenFile(range.source.name, true, false, false, false, false);
    c.vm.IO().SetDescriptor(descriptor,
                            {dx::IoRuntime::DescriptorKind::vfs_path, range.source.name, 0, false, {}, {}, file});
    c.vm.IO().BindFileStream(descriptor, file, false);
    try {
        static_cast<void>(CallAndroidMethod(c.vm, descriptor, "setInt$", "(I)V", {V::Int(file->handle)}));
        const auto pfd = c.vm.NewIntrinsicInstance("Landroid/os/ParcelFileDescriptor;");
        const auto pfdRoot = c.vm.ProtectReferences(std::array{pfd});
        const auto type = c.vm.Model().ObjectClass(pfd);
        const auto init = c.vm.EnsureClassInitialized(type);
        if (init.exception.IsValid())
            throw dx::VmJavaThrow{c.vm.Linker().Class(init.exception_class).descriptor, init.exception_message,
                                  init.exception};
        const auto ctor = c.vm.Linker().FindDirectMethod(type, "<init>", "(Ljava/io/FileDescriptor;)V");
        if (!ctor) throw std::logic_error("ROM ParcelFileDescriptor constructor is missing");
        const auto outcome = c.vm.Call(*ctor, std::array{V::Ref(pfd), V::Ref(descriptor)});
        if (outcome.exception.IsValid())
            throw dx::VmJavaThrow{c.vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                                  outcome.exception};
        c.vm.Model().SetPrimitiveElement(offsets, 0, range.offset);
        c.vm.Model().SetPrimitiveElement(offsets, 1, range.length);
        return V::Ref(pfd);
    } catch (...) {
        c.vm.IO().CloseDescriptor(descriptor);
        throw;
    }
}

template <class Operation>
void Bind(dx::IntrinsicClassBuilder& builder, const android_intrinsics::Context& context, const char* name,
          const char* signature, std::uint32_t flags, Operation operation) {
    auto handler = [context, operation](Call& call) {
        return Checked(kState, [&] { return operation(call, State(*context)); });
    };
    flags |= dx::kAccNative;
    if (flags & dx::kAccStatic)
        builder.StaticMethod(name, signature, handler, flags);
    else if (flags & dx::kAccPrivate)
        builder.DirectMethod(name, signature, handler, flags);
    else if (flags & dx::kAccFinal)
        builder.FinalMethod(name, signature, handler, flags);
    else
        builder.VirtualMethod(name, signature, handler, flags);
}
}  // namespace

std::shared_ptr<ResourceAssetManager> ResolveResourceAssetManager(dx::Interpreter& vm, DexVmAndroidContext& context,
                                                                  dx::VmObjectRef manager) {
    return State(context).managers.get(ManagerToken(vm, manager))->assetManager;
}

std::shared_ptr<res::Asset> ResolveResourceAsset(DexVmAndroidContext& context, std::uint32_t token) {
    return State(context).assets.get(token)->asset;
}
}  // namespace ogplay::runtime

namespace ogplay::runtime::android_intrinsics {
namespace {
constexpr std::int32_t kScreenLayoutSizeMask = 0x0f;
constexpr std::int32_t kScreenLayoutSizeSmall = 0x01;
constexpr std::int32_t kScreenLayoutSizeNormal = 0x02;
constexpr std::int32_t kScreenLayoutSizeLarge = 0x03;
constexpr std::int32_t kScreenLayoutSizeXlarge = 0x04;
constexpr std::int32_t kScreenLayoutLongMask = 0x30;
constexpr std::int32_t kScreenLayoutLongNo = 0x10;
constexpr std::int32_t kScreenLayoutLongYes = 0x20;
constexpr std::int32_t kScreenLayoutCompatNeeded = 0x10000000;

struct Api19ConfigurationDisplayFacts final {
    std::int32_t width_dp{};
    std::int32_t height_dp{};
    std::int32_t density_dpi{};
    std::int32_t screen_layout{};
};

Api19ConfigurationDisplayFacts Api19ConfigurationFacts(const ogplay::runtime::DexVmAndroidContext& context) {
    if (!std::isfinite(context.ui_density) || context.ui_density <= 0.0F) {
        throw std::invalid_argument("Configuration requires positive density");
    }
    const auto layout_width_dp =
        static_cast<std::int32_t>(std::lround(static_cast<double>(context.surface_width) / context.ui_density));
    const auto layout_height_dp =
        static_cast<std::int32_t>(std::lround(static_cast<double>(context.surface_height) / context.ui_density));
    const auto long_dp = std::max(layout_width_dp, layout_height_dp);
    const auto short_dp = std::min(layout_width_dp, layout_height_dp);

    std::int32_t size{};
    bool is_long{};
    bool compat_needed{};
    if (long_dp < 470) {
        size = kScreenLayoutSizeSmall;
    } else {
        size = long_dp >= 960 && short_dp >= 720   ? kScreenLayoutSizeXlarge
               : long_dp >= 640 && short_dp >= 480 ? kScreenLayoutSizeLarge
                                                   : kScreenLayoutSizeNormal;
        compat_needed = short_dp > 321 || long_dp > 570;
        is_long = ((long_dp * 3) / 5) >= (short_dp - 1);
    }

    auto layout = kScreenLayoutLongYes | kScreenLayoutSizeXlarge;
    if (!is_long) {
        layout = (layout & ~kScreenLayoutLongMask) | kScreenLayoutLongNo;
    }
    if (compat_needed) layout |= kScreenLayoutCompatNeeded;
    if (size < (layout & kScreenLayoutSizeMask)) {
        layout = (layout & ~kScreenLayoutSizeMask) | size;
    }
    const auto density_dpi = static_cast<double>(context.ui_density) * 160.0;
    if (density_dpi > static_cast<double>(INT32_MAX)) {
        throw std::invalid_argument("Configuration density exceeds API 19 range");
    }
    return {
        static_cast<std::int32_t>(static_cast<double>(context.surface_width) / context.ui_density),
        static_cast<std::int32_t>(static_cast<double>(context.surface_height) / context.ui_density),
        static_cast<std::int32_t>(std::lround(density_dpi)),
        layout,
    };
}

res::ResTable_config PlatformConfig(const DexVmAndroidContext& context) {
    const auto display = Api19ConfigurationFacts(context);
    return {
        .orientation = static_cast<std::uint8_t>(context.surface_width <= context.surface_height ? 1 : 2),
        .touchscreen = 3,
        .density = static_cast<std::uint16_t>(display.density_dpi),
        .keyboard = 2,
        .navigation = 1,
        .inputFlags = 9,
        .screenWidth = static_cast<std::uint16_t>(std::max(context.surface_width, context.surface_height)),
        .screenHeight = static_cast<std::uint16_t>(std::min(context.surface_width, context.surface_height)),
        .sdkVersion = 19,
        .screenLayout = static_cast<std::uint8_t>(display.screen_layout | 0x40),
        .smallestScreenWidthDp = static_cast<std::uint16_t>(std::min(display.width_dp, display.height_dp)),
        .screenWidthDp = static_cast<std::uint16_t>(display.width_dp),
        .screenHeightDp = static_cast<std::uint16_t>(display.height_dp),
    };
}

std::shared_ptr<AssetHandle> AssetFor(Call& call, ResourceNativeState& state, const Manager& manager) {
    auto asset = state.assets.get(U(call, 0));
    if (asset->owner.get() != &manager) throw dx::VmJavaThrow{kState, "asset belongs to a different manager"};
    return asset;
}

dx::VmObjectRef Construct(Call& c, const char* descriptor, const char* signature, std::span<const V> arguments = {}) {
    const auto type = c.vm.Linker().ResolveDescriptor(descriptor);
    const auto initialized = c.vm.EnsureClassInitialized(type);
    if (initialized.exception.IsValid())
        throw dx::VmJavaThrow{c.vm.Linker().Class(initialized.exception_class).descriptor,
                              initialized.exception_message, initialized.exception};
    const auto object = c.vm.NewIntrinsicInstance(descriptor);
    const auto root = c.vm.ProtectReferences(std::array{object});
    const auto ctor = c.vm.Linker().FindDirectMethod(type, "<init>", signature);
    if (!ctor) throw std::logic_error(std::string("ROM constructor missing: ") + descriptor);
    std::vector<V> args{V::Ref(object)};
    args.insert(args.end(), arguments.begin(), arguments.end());
    const auto result = c.vm.Call(*ctor, args);
    if (result.exception.IsValid())
        throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                              result.exception};
    return object;
}

constexpr auto kPrivate = dx::kAccPrivate;
constexpr auto kFinal = dx::kAccFinal;
constexpr auto kPublic = dx::kAccPublic;
constexpr auto kStatic = dx::kAccStatic;
}  // namespace

Decl Declare_android_content_res_AssetManager_Natives(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/content/res/AssetManager;");
    b.MergesBootClass();
    const auto bind = [&](const char* name, const char* signature, std::uint32_t flags, auto operation) {
        Bind(b, context, name, signature, flags, operation);
    };
    const auto instance = [&](const char* name, const char* signature, std::uint32_t flags, auto operation) {
        bind(name, signature, flags, [operation](Call& c, ResourceNativeState& state) {
            const auto owner = ManagerFor(c, state);
            std::scoped_lock lock(owner->assetManager->mutex);
            return operation(c, state, owner, owner->assetManager->core);
        });
    };

    bind("init", "()V", kPrivate | kFinal, [context](Call& c, ResourceNativeState& s) {
        if (ManagerToken(c.vm, c.receiver)) throw dx::VmJavaThrow{kState, "AssetManager already initialized"};
        auto manager = std::make_shared<ResourceAssetManager>(FrameworkResourceApk(*context), context->vfs);
        manager->core.setConfiguration(PlatformConfig(*context),
                                       context->locale_language + "-" + context->locale_region);
        const auto token = s.managers.add(std::make_shared<Manager>(std::move(manager)));
        c.vm.Model().InstanceSlots(c.receiver)[Field(c.vm, c.receiver, "mObject", "I")] = {token, dx::SlotTag::cat1};
        return V::Void();
    });
    bind("destroy", "()V", kPrivate | kFinal, [](Call& c, ResourceNativeState& s) {
        const auto manager = s.managers.remove(ManagerToken(c.vm, c.receiver));
        std::scoped_lock lock(manager->assetManager->mutex);
        manager->assetManager->retired = true;
        for (const auto& [cookie, token] : manager->pools)
            s.pools.retire(token);
        return V::Void();
    });
    instance("addAssetPathNative", "(Ljava/lang/String;)I", kPrivate | kFinal,
             [context](Call& c, auto&, auto&, res::AssetManager& manager) {
                 const auto path = OptionalString(c, 0);
                 if (!path) return Int(0);
                 try {
                     return Int(manager.addAssetPath(ReadResourceApk(*context, *path)));
                 } catch (const std::runtime_error&) {
                     return Int(0);
                 }
             });
    instance("isUpToDate", "()Z", kPublic | kFinal,
             [](Call&, auto&, auto&, auto& manager) { return Int(manager.isUpToDate()); });
    instance("getCookieName", "(I)Ljava/lang/String;", kPublic | kFinal, [](Call& c, auto&, auto&, auto& manager) {
        try {
            return V::Ref(c.vm.NewStringUtf8(manager.getCookieName(c.arguments[0].AsInt())));
        } catch (const res::ResourceError& error) {
            throw dx::VmJavaThrow{kBounds, error.what()};
        }
    });
    instance("getLocales", "()[Ljava/lang/String;", kPublic | kFinal, [](Call& c, auto&, auto&, auto& manager) {
        const auto locales = manager.resources().getLocales();
        return Strings(c, locales.size(), [&](std::size_t i) { return c.vm.NewStringUtf8(locales[i]); });
    });
    instance("setLocale", "(Ljava/lang/String;)V", kPublic | kFinal, [](Call& c, auto&, auto&, auto& manager) {
        const auto locale = OptionalString(c, 0).value_or("");
        manager.setConfiguration(manager.resources().parameters(), locale);
        return V::Void();
    });
    instance("setConfiguration", "(IILjava/lang/String;IIIIIIIIIIIIII)V", kPublic | kFinal,
             [context](Call& c, auto&, auto&, auto& manager) {
                 const auto word = [&](std::size_t i) { return static_cast<std::uint16_t>(U(c, i)); };
                 const auto byte = [&](std::size_t i) { return static_cast<std::uint8_t>(U(c, i)); };
                 const auto locale = OptionalString(c, 2);
                 res::ResTable_config config{
                     .mcc = word(0),
                     .mnc = word(1),
                     .orientation = byte(3),
                     .touchscreen = byte(4),
                     .density = word(5),
                     .keyboard = byte(6),
                     .navigation = byte(8),
                     .inputFlags = byte(7),
                     .screenWidth = word(9),
                     .screenHeight = word(10),
                     .sdkVersion = word(16),
                     .screenLayout = byte(14),
                     .uiMode = byte(15),
                     .smallestScreenWidthDp = word(11),
                     .screenWidthDp = word(12),
                     .screenHeightDp = word(13),
                 };
                 // The ROM system singleton starts with an undefined Configuration.
                 if (!config.orientation && !config.screenWidthDp && !config.screenHeightDp) {
                     auto defaults = PlatformConfig(*context);
                     defaults.mcc = config.mcc;
                     defaults.mnc = config.mnc;
                     defaults.uiMode = config.uiMode;
                     config = defaults;
                 }
                 manager.setConfiguration(config, locale);
                 return V::Void();
             });
    instance("list", "(Ljava/lang/String;)[Ljava/lang/String;", kPublic | kFinal,
             [](Call& c, auto&, auto&, auto& manager) {
                 return Checked(kIo, [&] {
                     const auto names = manager.list(String(c, 0));
                     return Strings(c, names.size(), [&](std::size_t i) { return c.vm.NewStringUtf8(names[i]); });
                 });
             });

    const auto open = [&](const char* name, const char* signature, bool nonAsset, bool fd) {
        instance(name, signature, nonAsset && fd ? kPrivate : kPrivate | kFinal,
                 [nonAsset, fd](Call& c, ResourceNativeState& s, auto& owner, auto&) {
                     return Checked(kFile, [&] {
                         const auto arg = nonAsset ? 1U : 0U;
                         const auto path = String(c, arg);
                         const auto cookie = nonAsset ? std::optional{c.arguments[0].AsInt()} : std::nullopt;
                         if (fd) return OpenFd(c, *owner, path, cookie, c.arguments[arg + 1].ref);
                         if (U(c, arg + 1) > 3) throw dx::VmJavaThrow{kArgument, "invalid asset access mode"};
                         auto asset = Open(*owner, path, cookie);
                         return Int(s.assets.add(std::make_shared<AssetHandle>(AssetHandle{owner, std::move(asset)})));
                     });
                 });
    };
    open("openAsset", "(Ljava/lang/String;I)I", false, false);
    open("openNonAssetNative", "(ILjava/lang/String;I)I", true, false);
    open("openAssetFd", "(Ljava/lang/String;[J)Landroid/os/ParcelFileDescriptor;", false, true);
    open("openNonAssetFdNative", "(ILjava/lang/String;[J)Landroid/os/ParcelFileDescriptor;", true, true);
    instance("openXmlAssetNative", "(ILjava/lang/String;)I", kPrivate | kFinal,
             [](Call& c, ResourceNativeState& s, auto& owner, auto&) { return OpenXml(c, s, *owner); });
    instance("destroyAsset", "(I)V", kPrivate | kFinal, [](Call& c, ResourceNativeState& s, auto& owner, auto&) {
        static_cast<void>(AssetFor(c, s, *owner));
        s.assets.remove(U(c, 0));
        return V::Void();
    });
    instance("readAssetChar", "(I)I", kPrivate | kFinal, [](Call& c, ResourceNativeState& s, auto& owner, auto&) {
        auto asset = AssetFor(c, s, *owner)->asset;
        std::byte byte{};
        return Checked(kIo,
                       [&] { return asset->read({&byte, 1}) ? Int(std::to_integer<unsigned>(byte)) : V::Int(-1); });
    });
    instance("readAsset", "(I[BII)I", kPrivate | kFinal, [](Call& c, ResourceNativeState& s, auto& owner, auto&) {
        auto asset = AssetFor(c, s, *owner)->asset;
        const auto array = dx::IntrinsicCall(c).NonNullRef(1, "asset read buffer");
        const auto start = c.arguments[2].AsInt(), length = c.arguments[3].AsInt();
        if (length == 0) return Int(0);
        if (start < 0 || length < 0 || start > c.vm.Model().ArrayLength(array) - length)
            throw dx::VmJavaThrow{kBounds, "asset read range"};
        return Checked(kIo, [&] {
            std::array<std::byte, 65536> bytes;
            int done = 0;
            while (done < length) {
                const auto request = std::span(bytes).first(
                    std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(length - done)));
                const auto read = asset->read(request);
                if (!read) break;
                c.vm.Model().WriteByteRegion(array, start + done, request.first(read));
                done += static_cast<int>(read);
            }
            return V::Int(done ? done : -1);
        });
    });
    instance("seekAsset", "(IJI)J", kPrivate | kFinal, [](Call& c, ResourceNativeState& s, auto& owner, auto&) {
        auto asset = AssetFor(c, s, *owner)->asset;
        const auto whence = c.arguments[2].AsInt();
        try {
            return V::Long(static_cast<std::int64_t>(asset->seek(c.arguments[1].AsLong(), whence > 0   ? 2
                                                                                          : whence < 0 ? 0
                                                                                                       : 1)));
        } catch (const res::ResourceError&) {
            return V::Long(-1);
        }
    });
    instance("getAssetLength", "(I)J", kPrivate | kFinal, [](Call& c, ResourceNativeState& s, auto& owner, auto&) {
        return V::Long(static_cast<std::int64_t>(AssetFor(c, s, *owner)->asset->length()));
    });
    instance("getAssetRemainingLength", "(I)J", kPrivate | kFinal,
             [](Call& c, ResourceNativeState& s, auto& owner, auto&) {
                 return V::Long(static_cast<std::int64_t>(AssetFor(c, s, *owner)->asset->remaining()));
             });
    bind("getGlobalAssetCount", "()I", kPublic | kStatic | kFinal,
         [](Call&, auto&) { return Int(res::Asset::globalCount()); });
    bind("getGlobalAssetManagerCount", "()I", kPublic | kStatic | kFinal,
         [](Call&, auto&) { return Int(managerCount.load()); });
    bind("getAssetAllocations", "()Ljava/lang/String;", kPublic | kStatic | kFinal, [](Call&, auto&) {
        // KitKat returns null when no whole-file asset buffers are allocated.
        return V::Ref(dx::VmObjectRef{});
    });

    instance("getResourceIdentifier", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I", kFinal,
             [](Call& c, auto&, auto&, auto& manager) {
                 const auto name = String(c, 0);
                 const auto type = OptionalString(c, 1).value_or("");
                 const auto package = OptionalString(c, 2).value_or("");
                 return Int(manager.resources().identifierForName(name, type, package));
             });
    const auto resourceName = [&](const char* native, auto part) {
        instance(native, "(I)Ljava/lang/String;", kFinal, [part](Call& c, auto&, auto&, auto& manager) {
            const auto name = manager.resources().getResourceName(U(c, 0));
            return V::Ref(name ? c.vm.NewStringUtf8(part(*name)) : dx::VmObjectRef{});
        });
    };
    resourceName("getResourceName", [](const res::ResourceName& n) { return n.fullName(); });
    resourceName("getResourcePackageName", [](const res::ResourceName& n) { return n.package; });
    resourceName("getResourceTypeName", [](const res::ResourceName& n) { return n.type; });
    resourceName("getResourceEntryName", [](const res::ResourceName& n) { return n.name; });
    instance("loadResourceValue", "(ISLandroid/util/TypedValue;Z)I", kPrivate | kFinal,
             [](Call& c, auto&, auto&, auto& manager) {
                 const auto& table = manager.resources();
                 return OutputValue(
                     c, table,
                     res::loadResourceValue(table, U(c, 0), static_cast<std::uint16_t>(U(c, 1)), U(c, 3) != 0), 2,
                     true);
             });
    instance("loadResourceBagValue", "(IILandroid/util/TypedValue;Z)I", kPrivate | kFinal,
             [](Call& c, auto&, auto&, auto& manager) {
                 const auto& table = manager.resources();
                 return OutputValue(c, table, res::loadResourceBagValue(table, U(c, 0), U(c, 1), U(c, 3) != 0), 2,
                                    false);
             });
    instance("getStringBlockCount", "()I", kPrivate | kFinal,
             [](Call&, auto&, auto&, auto& manager) { return Int(manager.resources().tableCount()); });
    instance("getNativeStringBlock", "(I)I", kPrivate | kFinal,
             [](Call& c, ResourceNativeState& s, auto& owner, auto& manager) {
                 const auto& table = manager.resources();
                 const auto block = U(c, 0);
                 if (block >= table.tableCount()) return Int(0);
                 const auto cookie = table.cookieForBlock(block);
                 if (const auto found = owner->pools.find(cookie); found != owner->pools.end())
                     return Int(found->second);
                 const auto token = s.pools.add(std::make_shared<PoolHandle>(PoolHandle{{}, &table.strings(cookie)}));
                 owner->pools.emplace(cookie, token);
                 return Int(token);
             });

    instance("newTheme", "()I", kPrivate | kFinal, [](Call&, ResourceNativeState& s, auto& owner, auto&) {
        return Int(s.themes.add(std::make_shared<ThemeHandle>(owner)));
    });
    instance("deleteTheme", "(I)V", kPrivate | kFinal, [](Call& c, ResourceNativeState& s, auto& owner, auto&) {
        const auto theme = s.themes.get(U(c, 0));
        if (theme->owner != owner) throw dx::VmJavaThrow{kState, "theme belongs to a different manager"};
        s.themes.remove(U(c, 0));
        return V::Void();
    });
    const auto themeNative = [&](const char* name, const char* signature, auto operation) {
        bind(name, signature, kStatic | kFinal, [operation](Call& c, ResourceNativeState& s) {
            const auto theme = s.themes.get(U(c, 0));
            std::scoped_lock lock(theme->owner->assetManager->mutex);
            if (theme->owner->assetManager->retired) throw dx::VmJavaThrow{kState, "theme manager is retired"};
            return operation(c, s, theme->theme);
        });
    };
    themeNative("applyThemeStyle", "(IIZ)V", [](Call& c, auto&, res::Theme& theme) {
        theme.applyStyle(U(c, 1), U(c, 2) != 0);
        return V::Void();
    });
    bind("copyTheme", "(II)V", kStatic | kFinal, [](Call& c, ResourceNativeState& s) {
        const auto dest = s.themes.get(U(c, 0)), source = s.themes.get(U(c, 1));
        auto& dm = dest->owner->assetManager->mutex;
        auto& sm = source->owner->assetManager->mutex;
        if (&dm == &sm) {
            std::scoped_lock lock(dm);
            if (dest->owner->assetManager->retired || source->owner->assetManager->retired)
                throw dx::VmJavaThrow{kState, "theme manager is retired"};
            dest->theme.setTo(source->theme);
        } else {
            std::scoped_lock lock(dm, sm);
            if (dest->owner->assetManager->retired || source->owner->assetManager->retired)
                throw dx::VmJavaThrow{kState, "theme manager is retired"};
            dest->theme.setTo(source->theme);
        }
        return V::Void();
    });
    themeNative(
        "loadThemeAttributeValue", "(IILandroid/util/TypedValue;Z)I", [](Call& c, auto&, const res::Theme& theme) {
            return OutputValue(c, theme.table(), res::loadThemeAttributeValue(theme, U(c, 1), U(c, 3) != 0), 2, false);
        });
    themeNative("dumpTheme", "(IILjava/lang/String;Ljava/lang/String;)V", [](Call& c, auto&, auto&) {
        GuestLog(c, core::LogLevel::debug,
                 OptionalString(c, 2).value_or("") + " " + OptionalString(c, 3).value_or("") + " theme " +
                     std::to_string(U(c, 0)));
        return V::Void();
    });
    themeNative("applyStyle", "(IIII[I[I[I)Z", [](Call& c, ResourceNativeState& s, const res::Theme& theme) {
        const auto parser = U(c, 3) ? s.parsers.get(U(c, 3)) : nullptr;
        return StyleOutput(c, res::applyStyle(theme, U(c, 1), U(c, 2), parser ? &parser->parser : nullptr, Attrs(c, 4)),
                           5, 6);
    });
    instance("retrieveAttributes", "(I[I[I[I)Z", kFinal, [](Call& c, ResourceNativeState& s, auto&, auto& manager) {
        const auto parser = s.parsers.get(U(c, 0));
        return StyleOutput(c, res::retrieveAttributes(manager.resources(), parser->parser, Attrs(c, 1)), 2, 3);
    });
    instance("getArraySize", "(I)I", kFinal, [](Call& c, auto&, auto&, auto& manager) {
        const auto bag = manager.resources().getBagLocked(U(c, 0));
        return bag ? Int(bag->entries.size()) : V::Int(-1);
    });
    instance("retrieveArray", "(I[I)I", kFinal, [](Call& c, auto&, auto&, auto& manager) {
        const auto& table = manager.resources();
        const auto out = dx::IntrinsicCall(c).NonNullRef(1, "resource array output");
        if (!table.getBagLocked(U(c, 0))) return V::Int(-1);
        const auto values = res::retrieveArray(table, U(c, 0));
        const auto count = std::min(values.size() / 6, static_cast<std::size_t>(c.vm.Model().ArrayLength(out)) / 6);
        WriteInts(c, out, std::span(values).first(count * 6));
        return Int(count);
    });
    instance("getArrayStringResource", "(I)[Ljava/lang/String;", kPrivate | kFinal,
             [](Call& c, auto&, auto&, auto& manager) {
                 const auto& table = manager.resources();
                 if (!table.getBagLocked(U(c, 0))) return V::Ref(dx::VmObjectRef{});
                 const auto values = res::getArrayStringResource(table, U(c, 0));
                 return Strings(c, values.size(), [&](std::size_t i) {
                     return values[i] ? c.vm.Model().NewString(*values[i]) : dx::VmObjectRef{};
                 });
             });
    instance("getArrayStringInfo", "(I)[I", kPrivate | kFinal, [](Call& c, auto&, auto&, auto& manager) {
        const auto& table = manager.resources();
        if (!table.getBagLocked(U(c, 0))) return V::Ref(dx::VmObjectRef{});
        const auto info = res::getArrayStringInfo(table, U(c, 0));
        std::vector<std::int32_t> values;
        values.reserve(info.size() * 2);
        for (const auto& pair : info)
            values.insert(values.end(), pair.begin(), pair.end());
        return IntArray(c, values);
    });
    instance("getArrayIntResource", "(I)[I", kFinal, [](Call& c, auto&, auto&, auto& manager) {
        const auto& table = manager.resources();
        return table.getBagLocked(U(c, 0)) ? IntArray(c, res::getArrayIntResource(table, U(c, 0)))
                                           : V::Ref(dx::VmObjectRef{});
    });
    return std::move(b).Build();
}

Decl Declare_android_content_res_XmlBlock_Natives(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/content/res/XmlBlock;", "Ljava/lang/Object;", {}, kFinal);
    b.MergesBootClass();
    const auto bind = [&](const char* name, const char* signature, bool isPrivate, auto operation) {
        Bind(b, context, name, signature, kStatic | kFinal | (isPrivate ? kPrivate : 0U), operation);
    };
    bind("nativeCreate", "([BII)I", true, [](Call& c, ResourceNativeState& s) {
        const auto bytes = CopyBytes(c);
        return Checked(kArgument, [&] {
            return Int(s.documents.add(std::make_shared<XmlHandle>(res::Source{bytes, *bytes, {}, {}})));
        });
    });
    bind("nativeGetStringBlock", "(I)I", false, [](Call& c, ResourceNativeState& s) {
        const auto document = s.documents.get(U(c, 0));
        if (!document->pool)
            document->pool = s.pools.add(std::make_shared<PoolHandle>(PoolHandle{{}, &document->tree.strings()}));
        return Int(document->pool);
    });
    bind("nativeCreateParseState", "(I)I", true, [](Call& c, ResourceNativeState& s) {
        const auto document = s.documents.get(U(c, 0));
        const auto token = s.parsers.add(std::make_shared<ParserHandle>(document));
        ++document->parsers;
        return Int(token);
    });
    bind("nativeDestroy", "(I)V", true, [](Call& c, ResourceNativeState& s) {
        const auto document = s.documents.remove(U(c, 0));
        document->retired = true;
        if (!document->parsers) s.pools.retire(document->pool);
        return V::Void();
    });
    bind("nativeDestroyParseState", "(I)V", true, [](Call& c, ResourceNativeState& s) {
        const auto document = s.parsers.remove(U(c, 0))->document;
        if (!--document->parsers && document->retired) s.pools.retire(document->pool);
        return V::Void();
    });

    const auto parserMethod = [&](const char* name, bool isPrivate, auto query) {
        bind(name, "(I)I", isPrivate, [query](Call& c, ResourceNativeState& s) {
            return Int(std::invoke(query, s.parsers.get(U(c, 0))->parser));
        });
    };
    parserMethod("nativeNext", false, &res::ResXMLParser::nextXmlBlockEvent);
    parserMethod("nativeGetNamespace", false, &res::ResXMLParser::elementNamespace);
    parserMethod("nativeGetName", false, &res::ResXMLParser::elementName);
    parserMethod("nativeGetText", false, &res::ResXMLParser::text);
    parserMethod("nativeGetLineNumber", true, &res::ResXMLParser::lineNumber);
    parserMethod("nativeGetAttributeCount", true, &res::ResXMLParser::attributeCount);
    parserMethod("nativeGetIdAttribute", true, &res::ResXMLParser::idAttribute);
    parserMethod("nativeGetClassAttribute", true, &res::ResXMLParser::classAttribute);
    parserMethod("nativeGetStyleAttribute", false, &res::ResXMLParser::styleAttribute);

    const auto attributeMethod = [&](const char* name, std::int32_t missing, auto query) {
        bind(name, "(II)I", true, [missing, query](Call& c, ResourceNativeState& s) {
            const auto parser = s.parsers.get(U(c, 0));
            const auto index = U(c, 1);
            return index < parser->parser.attributeCount() ? Int(query(parser->parser.attribute(index)))
                                                           : V::Int(missing);
        });
    };
    attributeMethod("nativeGetAttributeNamespace", -1, [](const auto& a) { return a.namespaceIndex; });
    attributeMethod("nativeGetAttributeName", -1, [](const auto& a) { return a.nameIndex; });
    attributeMethod("nativeGetAttributeResource", 0, [](const auto& a) { return a.resourceId; });
    attributeMethod("nativeGetAttributeDataType", 0, [](const auto& a) { return a.value.type; });
    attributeMethod("nativeGetAttributeData", 0, [](const auto& a) { return a.value.data; });
    attributeMethod("nativeGetAttributeStringValue", -1, [](const auto& a) { return a.rawValue; });
    bind("nativeGetAttributeIndex", "(ILjava/lang/String;Ljava/lang/String;)I", true,
         [](Call& c, ResourceNativeState& s) {
             const auto ns = OptionalString(c, 1);
             const auto index = s.parsers.get(U(c, 0))->parser.indexOfAttribute(ns, String(c, 2));
             return index ? Int(*index) : V::Int(-1);
         });
    return std::move(b).Build();
}

Decl Declare_android_content_res_StringBlock_Natives(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/content/res/StringBlock;");
    b.MergesBootClass();
    const auto bind = [&](const char* name, const char* signature, auto operation) {
        Bind(b, context, name, signature, kStatic | kPrivate, operation);
    };
    bind("nativeCreate", "([BII)I", [](Call& c, ResourceNativeState& s) {
        const auto bytes = CopyBytes(c);
        return Checked(kArgument, [&] {
            const auto pool = std::make_shared<res::ResStringPool>(*bytes, bytes);
            return Int(s.pools.add(std::make_shared<PoolHandle>(PoolHandle{pool, pool.get()})));
        });
    });
    bind("nativeDestroy", "(I)V", [](Call& c, ResourceNativeState& s) {
        if (!s.pools.get(U(c, 0))->owned) throw dx::VmJavaThrow{kState, "string pool is borrowed"};
        s.pools.remove(U(c, 0));
        return V::Void();
    });
    bind("nativeGetSize", "(I)I",
         [](Call& c, ResourceNativeState& s) { return Int(s.pools.get(U(c, 0))->pool->size()); });
    bind("nativeGetString", "(II)Ljava/lang/String;", [](Call& c, ResourceNativeState& s) {
        const auto* pool = s.pools.get(U(c, 0))->pool;
        return Checked(kBounds, [&] { return V::Ref(c.vm.Model().NewString(pool->string16At(U(c, 1)))); });
    });
    bind("nativeGetStyle", "(II)[I", [](Call& c, ResourceNativeState& s) {
        const auto* pool = s.pools.get(U(c, 0))->pool;
        if (U(c, 1) >= pool->size()) return V::Ref(dx::VmObjectRef{});
        const auto spans = pool->styleAt(U(c, 1));
        if (spans.empty()) return V::Ref(dx::VmObjectRef{});
        std::vector<std::int32_t> values;
        values.reserve(spans.size() * 3);
        for (const auto& span : spans) {
            values.push_back(static_cast<std::int32_t>(span.name));
            values.push_back(static_cast<std::int32_t>(span.firstChar));
            values.push_back(static_cast<std::int32_t>(span.lastChar));
        }
        return IntArray(c, values);
    });
    return std::move(b).Build();
}

dx::VmObjectRef CreateApplicationResources(Call& c, const Context& context) {
    const auto assets = Construct(c, "Landroid/content/res/AssetManager;", "()V");
    const auto assetRoot = c.vm.ProtectReferences(std::array{assets});
    if (!context->ApkBytes().empty()) {
        const auto path = c.vm.NewStringUtf8(context->package_resource_path);
        const auto pathRoot = c.vm.ProtectReferences(std::array{path});
        const auto cookie = CallAndroidMethod(c.vm, assets, "addAssetPath", "(Ljava/lang/String;)I", {V::Ref(path)});
        if (cookie.AsInt() == 0) throw dx::VmJavaThrow{kState, "cannot add application APK resource path"};
    }
    const auto metrics = Construct(c, "Landroid/util/DisplayMetrics;", "()V");
    const auto metricsRoot = c.vm.ProtectReferences(std::array{metrics});
    PopulateAndroidDisplayMetrics(c, context, metrics);
    const auto config = Construct(c, "Landroid/content/res/Configuration;", "()V");
    const auto configRoot = c.vm.ProtectReferences(std::array{config});
    const auto display = PlatformConfig(*context);
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, int>>{
             {"touchscreen", display.touchscreen},
             {"keyboard", display.keyboard},
             {"keyboardHidden", 1},
             {"hardKeyboardHidden", 1},
             {"navigation", display.navigation},
             {"navigationHidden", 2},
             {"orientation", display.orientation},
             {"screenWidthDp", display.screenWidthDp},
             {"screenHeightDp", display.screenHeightDp},
             {"smallestScreenWidthDp", display.smallestScreenWidthDp},
             {"densityDpi", display.density},
             {"screenLayout", Api19ConfigurationFacts(*context).screen_layout | 0x40},
         })
        c.vm.Model().InstanceSlots(config)[Field(c.vm, config, name, "I")] = {static_cast<std::uint32_t>(value),
                                                                              dx::SlotTag::cat1};
    c.vm.Model().InstanceSlots(config)[Field(c.vm, config, "fontScale", "F")] = {
        std::bit_cast<std::uint32_t>(context->ui_scaled_density / context->ui_density), dx::SlotTag::cat1};
    const auto resources = Construct(
        c, "Landroid/content/res/Resources;",
        "(Landroid/content/res/AssetManager;Landroid/util/DisplayMetrics;Landroid/content/res/Configuration;)V",
        std::array{V::Ref(assets), V::Ref(metrics), V::Ref(config)});
    context->application_resource_manager = ResolveResourceAssetManager(c.vm, *context, assets);
    return resources;
}

std::vector<std::byte> ReadAndroidApkAsset(dx::Interpreter& vm, const Context& context, dx::VmObjectRef object,
                                           const std::string& name) {
    const auto manager = ResolveResourceAssetManager(vm, *context, object);
    std::scoped_lock lock(manager->mutex);
    auto asset = manager->core.open(name);
    if (!asset) throw dx::VmJavaThrow{kFile, name};
    return ReadResourceAsset(*asset, 32U << 20);
}
}  // namespace ogplay::runtime::android_intrinsics
