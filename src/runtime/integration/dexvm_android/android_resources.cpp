#include <bit>
// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_content_res_AssetFileDescriptor.cpp ----
#include <cstdint>
#include <utility>

#include "catalog.h"
#include "ogplay/runtime/integration/host_image_decode.h"
#include "ogplay/core/json.h"
#include <limits>
#include <mutex>
#include <set>
#include <variant>
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/integration/apk_assets.h"
#include "ogplay/runtime/integration/bitmap_pixels.h"

namespace ogplay::runtime::android_intrinsics {

namespace {

// The sealed BootDex archive owns both Java code and its resource-ID mapping.
// One cache per catalog/context; no APK lookup or host filesystem fallback.
struct SystemResourceCache {
    std::once_flag loaded;
    std::unordered_map<std::uint32_t, std::variant<std::string, std::int32_t>> values;
    std::string error;
};

const std::variant<std::string, std::int32_t>* FindSystemResource(const Context& context, SystemResourceCache& cache,
                                                                  const std::uint32_t id) {
    if ((id >> 24U) != 1U) return nullptr;
    std::call_once(cache.loaded, [&] {
        try {
            constexpr std::string_view name = "META-INF/ogplay/system-resources.json";
            std::size_t matches = 0;
            for (const auto& entry : context->boot_classpath_archive.entries) {
                if (entry.name != name) continue;
                ++matches;
                if (entry.compression_method != 0 || entry.uncompressed_size > 65536U)
                    throw std::runtime_error("invalid BootDex system resource entry size/encoding");
            }
            if (matches != 1) throw std::runtime_error("BootDex system resource entry missing or duplicated");
            const auto bytes =
                loader::ReadStoredApkEntry(context->boot_classpath_bytes, context->boot_classpath_archive, name);
            const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            core::JsonParseError error;
            const auto document = core::JsonDocument::ParseStrict(text, error, 65536U, 8U);
            if (!document) throw std::runtime_error("invalid BootDex system resource JSON: " + error.message);
            const auto member = [](const core::JsonValue object, const std::string_view key) {
                return object.Member(key).value_or(core::JsonValue{});
            };
            const auto root = document->Root();
            const auto entries = member(root, "resources");
            if (!root.IsObject() || root.Size() != 3 || member(root, "schema_version").Integer() != 1 ||
                member(root, "api_level").Integer() != 19 || !entries.IsArray() || entries.Size() == 0 ||
                entries.Size() > 256)
                throw std::runtime_error("invalid BootDex system resource schema");
            decltype(cache.values) values;
            std::set<std::string> names;
            for (std::size_t index = 0; index < entries.Size(); ++index) {
                const auto entry = *entries.Element(index);
                const auto resource_id = member(entry, "id").UnsignedInteger();
                const auto resource_name = member(entry, "name").String();
                const auto type = member(entry, "type").String();
                const auto value = member(entry, "value");
                if (!entry.IsObject() || entry.Size() != 4 || !resource_id || (*resource_id >> 24U) != 1U ||
                    !resource_name || resource_name->empty() || !names.emplace(*resource_name).second ||
                    values.contains(static_cast<std::uint32_t>(*resource_id)))
                    throw std::runtime_error("invalid or duplicate BootDex system resource identity");
                const auto key = static_cast<std::uint32_t>(*resource_id);
                if (type == "string" && value.IsString() && value.String()->find('\0') == std::string_view::npos) {
                    values.emplace(key, std::string(*value.String()));
                } else if (type == "integer" && value.IsInteger() && value.Integer() &&
                           *value.Integer() >= std::numeric_limits<std::int32_t>::min() &&
                           *value.Integer() <= std::numeric_limits<std::int32_t>::max()) {
                    values.emplace(key, static_cast<std::int32_t>(*value.Integer()));
                } else {
                    throw std::runtime_error("invalid BootDex system resource type/value");
                }
            }
            cache.values = std::move(values);
        } catch (const std::exception& error) {
            cache.error = error.what();
        }
    });
    if (!cache.error.empty()) throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", cache.error};
    const auto found = cache.values.find(id);
    return found == cache.values.end() ? nullptr : &found->second;
}

void SetWide(dx::IntrinsicContext& call, const dx::VmObjectRef object, const std::size_t slot,
             const std::int64_t value) {
    const auto bits = static_cast<std::uint64_t>(value);
    auto slots = call.vm.Model().InstanceSlots(object);
    slots[slot] = {static_cast<std::uint32_t>(bits), dx::SlotTag::wide_lo};
    slots[slot + 1] = {static_cast<std::uint32_t>(bits >> 32U), dx::SlotTag::wide_hi};
}

std::int64_t GetWide(dx::IntrinsicContext& call, const dx::VmObjectRef object, const std::size_t slot) {
    const auto slots = call.vm.Model().InstanceSlots(object);
    const auto bits =
        static_cast<std::uint64_t>(slots[slot].bits) | (static_cast<std::uint64_t>(slots[slot + 1].bits) << 32U);
    return static_cast<std::int64_t>(bits);
}

void Initialize(dx::IntrinsicContext& call, const dx::VmObjectRef descriptor, const dx::VmObjectRef pfd,
                const std::int64_t start_offset, const std::int64_t length) {
    if (!pfd.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "fd must not be null"};
    }
    if (length < 0 && start_offset != 0) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "startOffset must be 0 when using UNKNOWN_LENGTH"};
    }
    auto slots = call.vm.Model().InstanceSlots(descriptor);
    slots[0] = {pfd.Value(), dx::SlotTag::ref};
    SetWide(call, descriptor, 1, start_offset);
    SetWide(call, descriptor, 3, length);
}

dx::VmObjectRef NewParcelFileDescriptor(dx::IntrinsicContext& call, const dx::VmObjectRef fd) {
    const auto fd_root = call.vm.ProtectReferences(std::array{fd});
    const auto type = call.vm.Linker().ResolveDescriptor("Landroid/os/ParcelFileDescriptor;");
    const auto initialized = call.vm.EnsureClassInitialized(type);
    if (initialized.exception.IsValid())
        throw dx::VmJavaThrow{call.vm.Linker().Class(initialized.exception_class).descriptor,
                              initialized.exception_message, initialized.exception};
    const auto pfd = call.vm.NewIntrinsicInstance("Landroid/os/ParcelFileDescriptor;");
    const auto root = call.vm.ProtectReferences(std::array{pfd});
    const auto constructor = call.vm.Linker().FindDirectMethod(type, "<init>", "(Ljava/io/FileDescriptor;)V");
    if (!constructor) throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "ROM PFD constructor missing");
    const auto outcome = call.vm.Call(*constructor, std::array{dx::VmValue::Ref(pfd), dx::VmValue::Ref(fd)});
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{call.vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
    return pfd;
}

dx::VmObjectRef MakeStoredApkAssetFileDescriptor(dx::IntrinsicContext& call, const Context& context,
                                                 const std::string& path) {
    const loader::ApkEntry* entry = nullptr;
    for (const auto& candidate : context->archive.entries) {
        if (candidate.name == path) {
            entry = &candidate;
            break;
        }
    }
    if (entry == nullptr || entry->compression_method != 0 || entry->compressed_size != entry->uncompressed_size) {
        throw dx::VmJavaThrow{"Ljava/io/FileNotFoundException;", "APK resource cannot provide a descriptor: " + path};
    }
    std::uint64_t data_offset{};
    try {
        data_offset = StoredApkAsset(context, path).start;
    } catch (const std::exception& error) {
        throw dx::VmJavaThrow{"Ljava/io/FileNotFoundException;",
                              "APK resource cannot provide a descriptor: " + path + " (" + error.what() + ")"};
    }
    const auto fd = call.vm.NewIntrinsicInstance("Ljava/io/FileDescriptor;");
    call.vm.IO().SetDescriptor(fd, {dx::IoRuntime::DescriptorKind::apk_entry, path, data_offset, false, {}, {}, {}});
    const auto fd_root = call.vm.ProtectReferences(std::array{fd});
    static_cast<void>(CallAndroidMethod(call.vm, fd, "setInt$", "(I)V", {dx::VmValue::Int(-1)}));
    const auto pfd = NewParcelFileDescriptor(call, fd);
    const auto pfd_root = call.vm.ProtectReferences(std::array{pfd});
    const auto descriptor = call.vm.NewIntrinsicInstance("Landroid/content/res/AssetFileDescriptor;");
    Initialize(call, descriptor, pfd, static_cast<std::int64_t>(data_offset),
               static_cast<std::int64_t>(entry->uncompressed_size));
    return descriptor;
}

}  // namespace

namespace {
template <class T>
dx::VmValue XmlInt(T value) {
    return dx::VmValue::Int(static_cast<std::int32_t>(value));
}

std::uint32_t XmlField(dx::Interpreter& vm, dx::VmObjectRef object, const char* name, const char* type = "I") {
    const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), name, type);
    if (!field) throw std::logic_error("ROM XML field missing");
    return vm.Model().InstanceSlots(object)[vm.Linker().Field(*field).slot].bits;
}

void RegisterXmlGc(dx::Interpreter& vm, const Context& context) {
    if (context->xml_gc_registered) return;
    vm.RegisterIntrinsicStateTable({"android.compiled-xml",
                                    {},
                                    [&vm, context](dx::VmObjectRef owner) {
                                        const auto& type = vm.Linker().Class(vm.Model().ObjectClass(owner)).descriptor;
                                        auto& registry = context->xml_cursors;
                                        if (type == "Landroid/content/res/XmlBlock;") {
                                            XmlDocumentToken token{XmlField(vm, owner, "mNative")};
                                            if (registry.HasDocument(token)) registry.RetireOwner(token);
                                        } else if (type == "Landroid/content/res/XmlBlock$Parser;") {
                                            XmlCursorToken token{XmlField(vm, owner, "mParseState")};
                                            if (registry.HasCursor(token)) registry.Close(token);
                                        } else if (type == "Landroid/content/res/StringBlock;" &&
                                                   XmlField(vm, owner, "mOwnsNative", "Z")) {
                                            XmlStringPoolToken token{XmlField(vm, owner, "mNative")};
                                            if (registry.HasStringPool(token)) registry.DestroyStringPool(token);
                                        }
                                    },
                                    {}});
    context->xml_gc_registered = true;
}

std::vector<std::byte> XmlBytes(dx::IntrinsicContext& call) {
    dx::IntrinsicCall c(call);
    const auto array = c.NonNullRef(0, "data");
    const auto offset = c.Int(1), size = c.Int(2);
    const auto length = static_cast<std::size_t>(call.vm.Model().ArrayLength(array));
    if (offset < 0 || size < 0 || static_cast<std::size_t>(offset) >= length ||
        static_cast<std::size_t>(size) > length - static_cast<std::size_t>(offset))
        throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "compiled XML byte slice"};
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<std::byte>(call.vm.Model().GetPrimitiveElement(
            array, static_cast<std::int32_t>(static_cast<std::size_t>(offset) + i)));
    return result;
}

template <class F>
dx::VmValue XmlChecked(dx::IntrinsicContext& call, const Context& context, F operation) {
    RegisterXmlGc(call.vm, context);
    try {
        return operation();
    } catch (const dx::VmJavaThrow&) {
        throw;
    } catch (const std::out_of_range& error) {
        throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", error.what()};
    } catch (const std::invalid_argument& error) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", error.what()};
    } catch (const std::logic_error& error) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", error.what()};
    } catch (const std::runtime_error& error) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", error.what()};
    }
}

dx::VmValue PlainPoolString(dx::IntrinsicContext& call, const Context& context, XmlStringPoolToken token, int index) {
    const auto pool = context->xml_cursors.StringPool(token);
    if (index < 0 || static_cast<std::size_t>(index) >= pool->StringCount())
        throw std::out_of_range("compiled string pool index");
    // No span can enter a plain-string consumer, even via nativeGetString.
    if (pool->HasStringStyles()) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.xml_block.string_styles", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "compiled string pool spans are unsupported"};
    }
    return dx::VmValue::Ref(call.vm.NewStringUtf8(*pool->String(static_cast<std::uint32_t>(index))));
}
}  // namespace

Decl Declare_android_content_res_XmlBlock(const Context& context) {
    auto builder =
        dx::IntrinsicClassBuilder::Class("Landroid/content/res/XmlBlock;", "Ljava/lang/Object;", {}, dx::kAccFinal);
    const auto bind = [&](const char* name, const char* desc, auto operation) {
        builder.StaticMethod(
            name, desc,
            [context, operation](dx::IntrinsicContext& call) {
                return XmlChecked(call, context, [&] { return operation(call, context->xml_cursors); });
            },
            (std::string_view(name) == "nativeNext" || std::string_view(name) == "nativeGetName" ? 0U
                                                                                                 : dx::kAccPrivate) |
                dx::kAccStatic | dx::kAccFinal | dx::kAccNative);
    };
    bind("nativeCreate", "([BII)I", [](auto& call, auto& registry) {
        const auto bytes = XmlBytes(call);
        return XmlInt(registry.CreateDocument(bytes).value);
    });
    bind("nativeGetStringBlock", "(I)I", [](auto& c, auto& r) {
        return XmlInt(r.DocumentPool(XmlDocumentToken{static_cast<std::uint32_t>(c.arguments[0].AsInt())}).value);
    });
    bind("nativeCreateParseState", "(I)I", [](auto& c, auto& r) {
        return XmlInt(r.CreateCursor(XmlDocumentToken{static_cast<std::uint32_t>(c.arguments[0].AsInt())}).value);
    });
    bind("nativeDestroy", "(I)V", [](auto& c, auto& r) {
        r.RetireOwner(XmlDocumentToken{static_cast<std::uint32_t>(c.arguments[0].AsInt())});
        return dx::VmValue::Void();
    });
    bind("nativeDestroyParseState", "(I)V", [](auto& c, auto& r) {
        r.Close(XmlCursorToken{static_cast<std::uint32_t>(c.arguments[0].AsInt())});
        return dx::VmValue::Void();
    });
    bind("nativeNext", "(I)I", [](auto& c, auto& r) {
        return XmlInt(
            static_cast<int>(r.Next(XmlCursorToken{static_cast<std::uint32_t>(c.arguments[0].AsInt())}).event));
    });
    for (const auto* name :
         {"nativeGetNamespace", "nativeGetName", "nativeGetText", "nativeGetLineNumber", "nativeGetAttributeCount",
          "nativeGetIdAttribute", "nativeGetClassAttribute", "nativeGetStyleAttribute"}) {
        bind(name, "(I)I", [name = std::string_view(name)](auto& c, auto& r) {
            const auto position = r.Current(XmlCursorToken{static_cast<std::uint32_t>(c.arguments[0].AsInt())});
            const auto& node = position.node;
            const bool tag = node && (node->type == 0x0102 || node->type == 0x0103);
            const bool start = node && node->type == 0x0102;
            if (name == "nativeGetLineNumber") return XmlInt(node ? node->line : 0xffffffffU);
            if (name == "nativeGetNamespace") return XmlInt(tag ? node->namespace_index : 0xffffffffU);
            if (name == "nativeGetName") return XmlInt(tag ? node->name_index : 0xffffffffU);
            if (name == "nativeGetText") return XmlInt(node && node->type == 0x0104 ? node->text_index : 0xffffffffU);
            if (name == "nativeGetAttributeCount") return XmlInt(start ? node->attributes.size() : 0);
            const auto index = !start                              ? 0
                               : name == "nativeGetIdAttribute"    ? node->id_index
                               : name == "nativeGetClassAttribute" ? node->class_index
                                                                   : node->style_index;
            if (name == "nativeGetStyleAttribute") {
                if (!index) return XmlInt(0);
                const auto value = node->attributes.at(static_cast<std::size_t>(index - 1)).value;
                return XmlInt(value.type == 1 || value.type == 2 ? value.data : 0);
            }
            return XmlInt(index ? node->attributes.at(static_cast<std::size_t>(index - 1)).raw_value_index
                                : 0xffffffffU);
        });
    }
    for (const auto* name : {"nativeGetAttributeNamespace", "nativeGetAttributeName", "nativeGetAttributeResource",
                             "nativeGetAttributeDataType", "nativeGetAttributeData", "nativeGetAttributeStringValue"}) {
        bind(name, "(II)I", [name = std::string_view(name)](auto& c, auto& r) {
            const auto position = r.Current(XmlCursorToken{static_cast<std::uint32_t>(c.arguments[0].AsInt())});
            const auto index = static_cast<std::uint32_t>(c.arguments[1].AsInt());
            const bool valid =
                position.node && position.node->type == 0x0102 && index < position.node->attributes.size();
            const auto attr = valid ? position.node->attributes[index] : loader::CompiledXmlAttribute{};
            if (name == "nativeGetAttributeNamespace") return XmlInt(valid ? attr.namespace_index : 0xfffffffeU);
            if (name == "nativeGetAttributeName") return XmlInt(valid ? attr.name_index : 0xffffffffU);
            if (name == "nativeGetAttributeStringValue") return XmlInt(valid ? attr.raw_value_index : 0xffffffffU);
            if (name == "nativeGetAttributeResource") return XmlInt(valid ? attr.resource_id : 0);
            if (name == "nativeGetAttributeDataType") return XmlInt(valid ? attr.value.type : 0);
            return XmlInt(valid ? attr.value.data : 0);
        });
    }
    bind("nativeGetAttributeIndex", "(ILjava/lang/String;Ljava/lang/String;)I", [](auto& c, auto& r) {
        const XmlCursorToken token{static_cast<std::uint32_t>(c.arguments[0].AsInt())};
        const auto position = r.Current(token);
        dx::IntrinsicCall args(c);
        const auto name = args.NonNullRef(2, "name");
        if (position.event != loader::BinaryXmlPullEventType::start_tag) return XmlInt(-1);
        const auto ns = args.Ref(1);
        const auto ns_text = ns.IsValid() ? std::optional(c.vm.Model().StringValue(ns)) : std::nullopt;
        const auto doc = r.Document(r.CursorDocument(token));
        const auto wanted = c.vm.Model().StringValue(name);
        // Compare UTF-16 units through the same VM WTF-8 conversion as pool strings.
        for (std::size_t i = 0; i < position.node->attributes.size(); ++i) {
            const auto& attr = position.node->attributes[i];
            const auto attr_ns = doc->String(attr.namespace_index);
            if (attr_ns.has_value() != ns_text.has_value()) continue;
            if (c.vm.Model().StringValue(c.vm.NewStringUtf8(*doc->String(attr.name_index))) != wanted) continue;
            if (!attr_ns || c.vm.Model().StringValue(c.vm.NewStringUtf8(*attr_ns)) == *ns_text) return XmlInt(i);
        }
        return XmlInt(-1);
    });
    return std::move(builder).Build();
}

Decl Declare_android_content_res_StringBlock(const Context& context) {
    auto builder =
        dx::IntrinsicClassBuilder::Class("Landroid/content/res/StringBlock;", "Ljava/lang/Object;", {}, dx::kAccFinal);
    const auto bind = [&](const char* name, const char* desc, auto operation) {
        builder.StaticMethod(
            name, desc,
            [context, operation](dx::IntrinsicContext& call) {
                return XmlChecked(call, context, [&] { return operation(call, context->xml_cursors); });
            },
            dx::kAccPrivate | dx::kAccStatic | dx::kAccNative);
    };
    bind("nativeCreate", "([BII)I", [](auto& c, auto& r) { return XmlInt(r.CreateStringPool(XmlBytes(c)).value); });
    bind("nativeDestroy", "(I)V", [](auto& c, auto& r) {
        r.DestroyStringPool(XmlStringPoolToken{static_cast<std::uint32_t>(c.arguments[0].AsInt())});
        return dx::VmValue::Void();
    });
    bind("nativeGetSize", "(I)I", [](auto& c, auto& r) {
        return XmlInt(
            r.StringPool(XmlStringPoolToken{static_cast<std::uint32_t>(c.arguments[0].AsInt())})->StringCount());
    });
    bind("nativeGetString", "(II)Ljava/lang/String;", [context](auto& c, auto&) {
        return PlainPoolString(c, context, XmlStringPoolToken{static_cast<std::uint32_t>(c.arguments[0].AsInt())},
                               c.arguments[1].AsInt());
    });
    const auto native = builder.BoundInstanceField("mNative", "I", dx::kAccPrivate | dx::kAccFinal);
    const auto use_sparse = builder.BoundInstanceField("mUseSparse", "Z", dx::kAccPrivate | dx::kAccFinal);
    const auto strings = builder.BoundInstanceField("mStrings", "[Ljava/lang/CharSequence;", dx::kAccPrivate);
    const auto sparse_strings =
        builder.BoundInstanceField("mSparseStrings", "Landroid/util/SparseArray;", dx::kAccPrivate);
    builder.VirtualMethod(
        "get", "(I)Ljava/lang/CharSequence;",
        [context, native, use_sparse, strings, sparse_strings](dx::IntrinsicContext& call) {
            return XmlChecked(call, context, [&] {
                dx::IntrinsicCall args(call);
                const XmlStringPoolToken token{static_cast<std::uint32_t>(args.GetInt(native))};
                const auto pool = context->xml_cursors.StringPool(token);
                const auto index = args.Int(0);
                if (index < 0 || static_cast<std::size_t>(index) >= pool->StringCount())
                    throw std::out_of_range("compiled string pool index");
                // Validate lifetime and the plain-pool boundary even on a cache hit.
                if (pool->HasStringStyles()) return PlainPoolString(call, context, token, index);
                auto array = args.GetRef(strings), sparse = args.GetRef(sparse_strings);
                if (!array.IsValid() && !sparse.IsValid()) {
                    if (args.GetInt(use_sparse) && pool->StringCount() > 250) {
                        const auto type = call.vm.Linker().ResolveDescriptor("Landroid/util/SparseArray;");
                        const auto initialized = call.vm.EnsureClassInitialized(type);
                        if (initialized.exception.IsValid())
                            throw dx::VmJavaThrow{call.vm.Linker().Class(initialized.exception_class).descriptor,
                                                  initialized.exception_message, initialized.exception};
                        sparse = call.vm.NewIntrinsicInstance("Landroid/util/SparseArray;");
                        const auto roots = call.vm.ProtectReferences(std::array{sparse});
                        const auto init = call.vm.Linker().FindDirectMethod(type, "<init>", "()V");
                        if (!init) throw std::logic_error("SparseArray constructor missing");
                        const auto outcome = call.vm.Call(*init, std::array{dx::VmValue::Ref(sparse)});
                        if (outcome.exception.IsValid())
                            throw dx::VmJavaThrow{call.vm.Linker().Class(outcome.exception_class).descriptor,
                                                  outcome.exception_message, outcome.exception};
                        args.SetRef(sparse_strings, sparse);
                    } else {
                        array = call.vm.Model().NewObjectArray(
                            call.vm.Linker().ResolveDescriptor("[Ljava/lang/CharSequence;"),
                            call.vm.Linker().ResolveDescriptor("Ljava/lang/CharSequence;"),
                            static_cast<std::int32_t>(pool->StringCount()));
                        args.SetRef(strings, array);
                    }
                }
                const auto cached =
                    array.IsValid()
                        ? call.vm.Model().GetObjectElement(array, index)
                        : CallAndroidMethod(call.vm, sparse, "get", "(I)Ljava/lang/Object;", {XmlInt(index)}).ref;
                if (cached.IsValid()) return dx::VmValue::Ref(cached);
                const auto result = PlainPoolString(call, context, token, index);
                const auto root = call.vm.ProtectReferences(std::array{result.ref});
                if (array.IsValid())
                    call.vm.Model().SetObjectElement(array, index, result.ref);
                else
                    static_cast<void>(
                        CallAndroidMethod(call.vm, sparse, "put", "(ILjava/lang/Object;)V", {XmlInt(index), result}));
                return result;
            });
        });
    return std::move(builder).Build();
}

Decl Declare_android_content_res_AssetFileDescriptor(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/res/AssetFileDescriptor;", "Ljava/lang/Object;",
                                                    {"Landroid/os/Parcelable;", "Ljava/io/Closeable;"});
    const auto wu_mFd =
        builder.BoundInstanceField("mFd", "Landroid/os/ParcelFileDescriptor;", dx::kAccPrivate | dx::kAccFinal);
    const auto wu_mStartOffset = builder.BoundInstanceField("mStartOffset", "J", dx::kAccPrivate | dx::kAccFinal);
    const auto wu_mLength = builder.BoundInstanceField("mLength", "J", dx::kAccPrivate | dx::kAccFinal);
    builder.Constructor("(Landroid/os/ParcelFileDescriptor;JJ)V", [](dx::IntrinsicContext& call) {
        Initialize(call, call.receiver, call.arguments[0].ref, call.arguments[1].AsLong(), call.arguments[2].AsLong());
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("getFileDescriptor", "()Ljava/io/FileDescriptor;", [](dx::IntrinsicContext& call) {
        const auto pfd = dx::VmObjectRef(call.vm.Model().InstanceSlots(call.receiver)[0].bits);
        if (!pfd.IsValid()) return dx::VmValue::Ref(dx::VmObjectRef{});
        return CallAndroidMethod(call.vm, pfd, "getFileDescriptor", "()Ljava/io/FileDescriptor;");
    });
    builder.VirtualMethod("getStartOffset", "()J", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Long(GetWide(call, call.receiver, 1));
    });
    builder.VirtualMethod("getLength", "()J", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Long(GetWide(call, call.receiver, 3));
    });
    builder.VirtualMethod("close", "()V", [](dx::IntrinsicContext& call) {
        const auto pfd = dx::VmObjectRef(call.vm.Model().InstanceSlots(call.receiver)[0].bits);
        return CallAndroidMethod(call.vm, pfd, "close", "()V");
    });
    const auto wu_mExtras =
        builder.BoundInstanceField("mExtras", "Landroid/os/Bundle;", dx::kAccPrivate | dx::kAccFinal);
    // AOSP 4.4 ROM line 74.
    builder.Constructor(
        "(Landroid/os/ParcelFileDescriptor;JJLandroid/os/Bundle;)V",
        [wu_mFd, wu_mStartOffset, wu_mLength, wu_mExtras](dx::IntrinsicContext& c) {
            dx::IntrinsicCall api(c);
            const auto descriptor = api.Ref(0);
            if (!descriptor.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "fd must not be null"};
            if (api.Long(2) < 0 && api.Long(1) != 0)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "startOffset must be 0 when using UNKNOWN_LENGTH"};
            api.SetRef(wu_mFd, descriptor);
            api.SetLong(wu_mStartOffset, api.Long(1));
            api.SetLong(wu_mLength, api.Long(2));
            api.SetRef(wu_mExtras, api.Ref(3));
            return dx::VmValue::Void();
        },
        dx::kAccPublic);

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_res_AssetManager.cpp ----
#include <set>
#include <string>

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {
void ThemeField(dx::Interpreter& vm, dx::VmObjectRef object, const char* name, const char* type, std::uint32_t bits,
                dx::SlotTag tag = dx::SlotTag::cat1) {
    const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), name, type);
    if (!field) throw std::logic_error(std::string("missing ROM theme field: ") + name);
    vm.Model().InstanceSlots(object)[vm.Linker().Field(*field).slot] = {bits, tag};
}

void ThemeLoad(dx::IntrinsicContext& call, const Context& context) {
    if (!context->theme_projection_loaded) {
        constexpr std::string_view entry = "META-INF/ogplay/theme-projection.json";
        const auto count = std::count_if(
            context->boot_classpath_archive.entries.begin(), context->boot_classpath_archive.entries.end(),
            [](const auto& row) { return row.name == "META-INF/ogplay/theme-projection.json"; });
        if (count != 1) throw std::runtime_error("theme projection entry missing or duplicated");
        const auto bytes =
            loader::ReadStoredApkEntry(context->boot_classpath_bytes, context->boot_classpath_archive, entry);
        context->theme_resources.Load(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        context->theme_projection_loaded = true;
    }
    context->theme_resources.SetResourceHooks(
        [owner = context.get()](std::uint32_t id) { return SelectUiResourceEntry(*owner, id); },
        [owner = context.get()](const loader::ArscEntry& entry) {
            static_cast<void>(DecodeLeafBitmap(*owner, entry));
        });
    if (!context->theme_gc_registered) {
        call.vm.RegisterIntrinsicStateTable(
            {"android.theme",
             {},
             [&vm = call.vm, context](dx::VmObjectRef owner) {
                 const auto& type = vm.Linker().Class(vm.Model().ObjectClass(owner)).descriptor;
                 if (type == "Landroid/content/res/Resources$Theme;") {
                     const ThemeToken token{XmlField(vm, owner, "mTheme")};
                     if (context->theme_resources.Has(token)) context->theme_resources.Delete(token);
                     context->theme_owners.erase(token);
                 }
                 if (context->theme_asset_owners.erase(owner)) {
                     for (auto it = context->theme_owners.begin(); it != context->theme_owners.end();) {
                         if (it->second == owner) {
                             if (context->theme_resources.Has(it->first)) context->theme_resources.Delete(it->first);
                             it = context->theme_owners.erase(it);
                         } else
                             ++it;
                     }
                 }
             },
             {}});
        context->theme_gc_registered = true;
    }
}

template <class F>
dx::VmValue ThemeChecked(dx::IntrinsicContext& call, const Context& context, F&& operation) {
    try {
        ThemeLoad(call, context);
        return operation();
    } catch (const dx::VmJavaThrow&) {
        throw;
    } catch (const std::exception& error) {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.theme.unsupported", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", error.what()};
    }
}

void ThemeOutput(dx::IntrinsicContext& call, dx::VmObjectRef target, const ThemeValue& value) {
    if (!target.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "outValue"};
    const auto fields = std::array{"type", "data", "assetCookie", "resourceId", "changingConfigurations", "density"};
    const auto slots = value.Slots();
    for (std::size_t i = 0; i < fields.size(); ++i)
        ThemeField(call.vm, target, fields[i], "I", slots[i]);
    ThemeField(call.vm, target, "string", "Ljava/lang/CharSequence;", 0, dx::SlotTag::ref);
}

std::vector<std::uint32_t> ThemeAttrs(dx::IntrinsicContext& call, dx::VmObjectRef array) {
    if (!array.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "attrs"};
    if (call.vm.Model().PrimitiveArrayKind(array) != JniPrimitiveKind::integer)
        throw std::runtime_error("attribute IDs require int[]");
    const auto count = call.vm.Model().ArrayLength(array);
    if (count > 4096) throw std::runtime_error("attribute request exceeds bounded ABI");
    std::vector<std::uint32_t> result;
    for (std::int32_t i = 0; i < count; ++i)
        result.push_back(static_cast<std::uint32_t>(call.vm.Model().GetPrimitiveElement(array, i)));
    return result;
}

dx::VmValue ThemeArrays(dx::IntrinsicContext& call, const std::vector<ThemeValue>& values, dx::VmObjectRef data,
                        dx::VmObjectRef indices) {
    if (!data.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "outValues"};
    if (call.vm.Model().PrimitiveArrayKind(data) != JniPrimitiveKind::integer ||
        (indices.IsValid() && call.vm.Model().PrimitiveArrayKind(indices) != JniPrimitiveKind::integer))
        throw std::runtime_error("TypedArray outputs require int[]");
    if (call.vm.Model().ArrayLength(data) < static_cast<std::int32_t>(values.size() * 6) ||
        (indices.IsValid() && call.vm.Model().ArrayLength(indices) <= static_cast<std::int32_t>(values.size())))
        throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "TypedArray output capacity"};
    std::int32_t count{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto slots = values[i].Slots();
        for (std::size_t j = 0; j < slots.size(); ++j)
            call.vm.Model().SetPrimitiveElement(data, static_cast<std::int32_t>(i * 6 + j), slots[j]);
        if (indices.IsValid() && values[i].type)
            call.vm.Model().SetPrimitiveElement(indices, ++count, static_cast<std::uint64_t>(i));
    }
    if (indices.IsValid()) call.vm.Model().SetPrimitiveElement(indices, 0, static_cast<std::uint32_t>(count));
    return dx::VmValue::Int(1);
}

void ThemeAssetLive(const Context& context, dx::VmObjectRef assets) {
    if (!context->theme_asset_owners.contains(assets)) throw std::runtime_error("AssetManager owner is not live");
}

void AddThemeAssetMethods(dx::IntrinsicClassBuilder& builder, const Context& context) {
    const auto method = [&](const char* name, const char* desc, bool is_static, auto operation) {
        auto handler = [context, operation](dx::IntrinsicContext& call) {
            return ThemeChecked(call, context, [&] { return operation(call); });
        };
        const auto flags =
            dx::kAccNative | dx::kAccFinal | (std::string_view(name) == "retrieveAttributes" ? 0U : dx::kAccPrivate);
        if (is_static)
            builder.StaticMethod(name, desc, handler, flags | dx::kAccStatic);
        else if (std::string_view(name) == "retrieveAttributes")
            builder.FinalMethod(name, desc, handler, flags);
        else
            builder.DirectMethod(name, desc, handler, flags);
    };
    method("init", "()V", false, [context](dx::IntrinsicContext& c) {
        if (!context->theme_asset_owners.emplace(c.receiver).second)
            throw std::runtime_error("AssetManager already initialized");
        return dx::VmValue::Void();
    });
    method("destroy", "()V", false, [context](dx::IntrinsicContext& c) {
        ThemeAssetLive(context, c.receiver);
        context->theme_asset_owners.erase(c.receiver);
        for (auto it = context->theme_owners.begin(); it != context->theme_owners.end();) {
            if (it->second == c.receiver) {
                context->theme_resources.Delete(it->first);
                it = context->theme_owners.erase(it);
            } else
                ++it;
        }
        return dx::VmValue::Void();
    });
    method("newTheme", "()I", false, [context](dx::IntrinsicContext& c) {
        ThemeAssetLive(context, c.receiver);
        const auto token = context->theme_resources.New();
        context->theme_owners.emplace(token, c.receiver);
        return XmlInt(token.value);
    });
    method("deleteTheme", "(I)V", false, [context](dx::IntrinsicContext& c) {
        const ThemeToken token{static_cast<std::uint32_t>(c.arguments[0].AsInt())};
        const auto it = context->theme_owners.find(token);
        if (it == context->theme_owners.end() || it->second != c.receiver)
            throw std::runtime_error("theme AssetManager owner mismatch");
        context->theme_resources.Delete(token);
        context->theme_owners.erase(token);
        return dx::VmValue::Void();
    });
    method("applyThemeStyle", "(IIZ)V", true, [context](dx::IntrinsicContext& c) {
        context->theme_resources.Apply({static_cast<std::uint32_t>(c.arguments[0].AsInt())},
                                       static_cast<std::uint32_t>(c.arguments[1].AsInt()),
                                       static_cast<std::uint32_t>(c.arguments[2].AsInt()), context->arsc);
        return dx::VmValue::Void();
    });
    method("copyTheme", "(II)V", true, [context](dx::IntrinsicContext& c) {
        context->theme_resources.Copy({static_cast<std::uint32_t>(c.arguments[0].AsInt())},
                                      {static_cast<std::uint32_t>(c.arguments[1].AsInt())});
        return dx::VmValue::Void();
    });
    method("loadThemeAttributeValue", "(IILandroid/util/TypedValue;Z)I", true, [context](dx::IntrinsicContext& c) {
        const auto value = context->theme_resources.Attribute(
            {static_cast<std::uint32_t>(c.arguments[0].AsInt())}, static_cast<std::uint32_t>(c.arguments[1].AsInt()),
            static_cast<std::uint32_t>(c.arguments[3].AsInt()), context->arsc);
        if (!value) return XmlInt(-1);
        ThemeOutput(c, c.arguments[2].ref, *value);
        return XmlInt(value->type == 3 ? value->cookie - 1 : 0);
    });
    method("applyStyle", "(IIII[I[I[I)Z", true, [context](dx::IntrinsicContext& c) {
        const auto attrs = ThemeAttrs(c, c.arguments[4].ref);
        const auto token = static_cast<std::uint32_t>(c.arguments[3].AsInt());
        const auto tag = token ? std::optional{context->xml_cursors.StartTag({token})} : std::nullopt;
        const auto values = context->theme_resources.Styled(
            {static_cast<std::uint32_t>(c.arguments[0].AsInt())}, static_cast<std::uint32_t>(c.arguments[1].AsInt()),
            static_cast<std::uint32_t>(c.arguments[2].AsInt()), tag ? &*tag : nullptr, attrs, context->arsc);
        return ThemeArrays(c, values, c.arguments[5].ref, c.arguments[6].ref);
    });
    method("retrieveAttributes", "(I[I[I[I)Z", false, [context](dx::IntrinsicContext& c) {
        ThemeAssetLive(context, c.receiver);
        const auto attrs = ThemeAttrs(c, c.arguments[1].ref);
        const auto tag = context->xml_cursors.StartTag({static_cast<std::uint32_t>(c.arguments[0].AsInt())});
        return ThemeArrays(c, context->theme_resources.Retrieve(tag, attrs, context->arsc), c.arguments[2].ref,
                           c.arguments[3].ref);
    });
    method("loadResourceValue", "(ISLandroid/util/TypedValue;Z)I", false, [context](dx::IntrinsicContext& c) {
        ThemeAssetLive(context, c.receiver);
        const auto value =
            context->theme_resources.Resource(static_cast<std::uint32_t>(c.arguments[0].AsInt()),
                                              static_cast<std::uint32_t>(c.arguments[3].AsInt()), context->arsc);
        ThemeOutput(c, c.arguments[2].ref, value);
        return XmlInt(value.type == 3 ? value.cookie - 1 : 0);
    });
    method("loadResourceBagValue", "(IILandroid/util/TypedValue;Z)I", false, [context](dx::IntrinsicContext& c) {
        ThemeAssetLive(context, c.receiver);
        const auto value = context->theme_resources.BagValue(
            static_cast<std::uint32_t>(c.arguments[0].AsInt()), static_cast<std::uint32_t>(c.arguments[1].AsInt()),
            static_cast<std::uint32_t>(c.arguments[3].AsInt()), context->arsc);
        if (!value) return XmlInt(-1);
        ThemeOutput(c, c.arguments[2].ref, *value);
        return XmlInt(value->type == 3 ? value->cookie - 1 : 0);
    });
    method("getStringBlockCount", "()I", false, [context](dx::IntrinsicContext& c) {
        ThemeAssetLive(context, c.receiver);
        return XmlInt(context->theme_resources.Strings().empty() ? (context->arsc.global_string_pool.empty() ? 0 : 1)
                                                                 : 2);
    });
    method("getNativeStringBlock", "(I)I", false, [context](dx::IntrinsicContext& c) {
        ThemeAssetLive(context, c.receiver);
        const auto index = c.arguments[0].AsInt();
        if (index == 1 && !context->theme_resources.Strings().empty()) {
            if (!context->rom_string_pool.value)
                context->rom_string_pool = context->xml_cursors.CreateStringPool(context->theme_resources.Strings());
            return XmlInt(context->rom_string_pool.value);
        }
        if (index != 0 || context->arsc.global_string_pool.empty())
            throw std::runtime_error("resource pool index unavailable");
        if (!context->resource_string_pool.value)
            context->resource_string_pool = context->xml_cursors.CreateStringPool(context->arsc.global_string_pool);
        return XmlInt(context->resource_string_pool.value);
    });
    builder.Constructor("()V", [context](dx::IntrinsicContext& c) {
        return ThemeChecked(c, context, [&] {
            ThemeField(c.vm, c.receiver, "mOpen", "Z", 1);
            ThemeField(c.vm, c.receiver, "mNumRefs", "I", 1);
            auto value = c.vm.NewIntrinsicInstance("Landroid/util/TypedValue;");
            const auto root = c.vm.ProtectReferences(std::array{value});
            ThemeField(c.vm, c.receiver, "mValue", "Landroid/util/TypedValue;", value.Value(), dx::SlotTag::ref);
            auto offsets = c.vm.Model().NewPrimitiveArray(c.vm.Linker().ResolveDescriptor("[J"),
                                                          JniPrimitiveKind::long_integer, 2);
            ThemeField(c.vm, c.receiver, "mOffsets", "[J", offsets.Value(), dx::SlotTag::ref);
            if (!context->theme_asset_owners.emplace(c.receiver).second)
                throw std::runtime_error("AssetManager already initialized");
            return dx::VmValue::Void();
        });
    });
    // No synthetic system AssetManager. Construct the single APK pool's genuine
    // StringBlock, whose Java caches and plain-string guard remain authoritative.
    builder.FinalMethod("makeStringBlocks", "(Z)V", [context](dx::IntrinsicContext& c) {
        return ThemeChecked(c, context, [&] {
            ThemeAssetLive(context, c.receiver);
            const auto owner = c.vm.Linker().ResolveDescriptor("Landroid/content/res/StringBlock;");
            const auto count =
                context->theme_resources.Strings().empty() ? (context->arsc.global_string_pool.empty() ? 0 : 1) : 2;
            const auto array = c.vm.Model().NewObjectArray(
                c.vm.Linker().ResolveDescriptor("[Landroid/content/res/StringBlock;"), owner, count);
            const auto roots = c.vm.ProtectReferences(std::array{array});
            for (int index = 0; index < count; ++index) {
                // An owner without an APK ARSC has no cookie1 pool. Keep that
                // slot absent while preserving the sealed ROM cookie2 identity.
                if (index == 0 && context->arsc.global_string_pool.empty()) continue;
                auto& pool = index == 0 ? context->resource_string_pool : context->rom_string_pool;
                if (!pool.value)
                    pool = context->xml_cursors.CreateStringPool(index == 0 ? context->arsc.global_string_pool
                                                                            : context->theme_resources.Strings());
                const auto block = c.vm.NewIntrinsicInstance("Landroid/content/res/StringBlock;");
                const auto block_root = c.vm.ProtectReferences(std::array{block});
                const auto ctor = c.vm.Linker().FindDirectMethod(owner, "<init>", "(IZ)V");
                if (!ctor) throw std::logic_error("StringBlock native-pool constructor missing");
                const auto result =
                    c.vm.Call(*ctor, std::array{dx::VmValue::Ref(block), XmlInt(pool.value), XmlInt(0)});
                if (result.exception.IsValid())
                    throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor,
                                          result.exception_message, result.exception};
                c.vm.Model().SetObjectElement(array, index, block);
            }
            ThemeField(c.vm, c.receiver, "mStringBlocks", "[Landroid/content/res/StringBlock;", array.Value(),
                       dx::SlotTag::ref);
            return dx::VmValue::Void();
        });
    });
}
}  // namespace

void InitializeThemeResourceOwner(dx::IntrinsicContext& c, const Context& context, dx::VmObjectRef resources,
                                  dx::VmObjectRef assets) {
    ThemeLoad(c, context);
    const auto initialized = c.vm.EnsureClassInitialized(c.vm.Model().ObjectClass(resources));
    if (initialized.exception.IsValid())
        throw dx::VmJavaThrow{c.vm.Linker().Class(initialized.exception_class).descriptor,
                              initialized.exception_message, initialized.exception};
    const auto lock = c.vm.NewIntrinsicInstance("Ljava/lang/Object;");
    ThemeField(c.vm, resources, "mAccessLock", "Ljava/lang/Object;", lock.Value(), dx::SlotTag::ref);
    const auto ctor = c.vm.Linker().FindDirectMethod(c.vm.Model().ObjectClass(assets), "<init>", "()V");
    if (!ctor) throw std::logic_error("AssetManager constructor missing");
    const auto result = c.vm.Call(*ctor, std::array{dx::VmValue::Ref(assets)});
    if (result.exception.IsValid())
        throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                              result.exception};
    static_cast<void>(CallAndroidMethod(c.vm, assets, "ensureStringBlocks", "()V"));
    static_cast<void>(CallAndroidMethod(c.vm, resources, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;"));
}

Decl Declare_android_content_res_AssetManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/res/AssetManager;", "Ljava/lang/Object;");
    AddThemeAssetMethods(builder, context);
    // Equivalent APK-only opening boundary. Construction/counts stay in ROM;
    // no AssetManager native declaration is admitted by this adapter.
    const auto open_xml = [context](dx::IntrinsicContext& call, int cookie, dx::VmObjectRef file) {
        if (IsSystemAssets(context, call.receiver) || (cookie != 0 && cookie != 1)) {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.xml_block.system_xml", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "system XML/cookie is unsupported"};
        }
        if (!file.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "fileName"};
        const auto path = call.vm.StringUtf8(file);
        if (!path.starts_with("res/") || path.find("..") != std::string::npos)
            throw dx::VmJavaThrow{"Ljava/io/FileNotFoundException;", "APK XML path must be under res/"};
        std::vector<std::byte> bytes;
        try {
            bytes = loader::ReadApkEntry(context->apk_bytes, context->archive, path);
        } catch (const std::exception& error) {
            throw dx::VmJavaThrow{"Ljava/io/FileNotFoundException;", error.what()};
        }
        const auto type = call.vm.Linker().ResolveDescriptor("Landroid/content/res/XmlBlock;");
        const auto require = [&call](const dx::VmCallOutcome& outcome) {
            if (outcome.exception.IsValid())
                throw dx::VmJavaThrow{call.vm.Linker().Class(call.vm.Model().ObjectClass(outcome.exception)).descriptor,
                                      outcome.exception_message, outcome.exception};
        };
        require(call.vm.EnsureClassInitialized(type));
        auto array = call.vm.Model().NewPrimitiveArray(call.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                                                       static_cast<std::int32_t>(bytes.size()));
        auto block = call.vm.NewIntrinsicInstance("Landroid/content/res/XmlBlock;");
        const auto roots = call.vm.ProtectReferences(std::array{array, block});
        call.vm.Model().FillPrimitiveData(array, bytes);
        const auto constructor = call.vm.Linker().FindDirectMethod(type, "<init>", "([B)V");
        if (!constructor) throw std::logic_error("ROM XmlBlock constructor missing");
        require(call.vm.Call(*constructor, std::array{dx::VmValue::Ref(block), dx::VmValue::Ref(array)}));
        const auto parser = CallAndroidMethod(call.vm, block, "newParser", "()Landroid/content/res/XmlResourceParser;");
        const auto parser_root = call.vm.ProtectReferences(std::array{parser.ref});
        static_cast<void>(CallAndroidMethod(call.vm, block, "close", "()V"));
        return parser;
    };
    {
        builder.FinalMethod(
            "openXmlResourceParser", "(ILjava/lang/String;)Landroid/content/res/XmlResourceParser;",
            [open_xml](dx::IntrinsicContext& c) { return open_xml(c, c.arguments[0].AsInt(), c.arguments[1].ref); });
        builder.FinalMethod("openXmlResourceParser", "(Ljava/lang/String;)Landroid/content/res/XmlResourceParser;",
                            [open_xml](dx::IntrinsicContext& c) { return open_xml(c, 0, c.arguments[0].ref); });
    }
    builder.FinalMethod(
        "openFd", "(Ljava/lang/String;)Landroid/content/res/AssetFileDescriptor;",
        [context](dx::IntrinsicContext& call) {
            if (IsSystemAssets(context, call.receiver))
                throw dx::VmJavaThrow{"Ljava/io/FileNotFoundException;", "system asset is unavailable"};
            const auto path = "assets/" + call.vm.StringUtf8(call.arguments[0].ref);
            const loader::ApkEntry* entry = nullptr;
            for (const auto& candidate : context->archive.entries) {
                if (candidate.name == path) {
                    entry = &candidate;
                    break;
                }
            }
            if (entry == nullptr) {
                throw dx::VmJavaThrow{"Ljava/io/FileNotFoundException;", "APK asset is unavailable: " + path};
            }
            if (entry->compression_method != 0 || entry->compressed_size != entry->uncompressed_size) {
                throw dx::VmJavaThrow{"Ljava/io/FileNotFoundException;", "APK asset is compressed: " + path};
            }
            std::uint64_t data_offset{};
            try {
                data_offset = StoredApkAsset(context, path).start;
            } catch (const std::exception& error) {
                throw dx::VmJavaThrow{"Ljava/io/FileNotFoundException;",
                                      "APK asset cannot provide a descriptor: " + path + " (" + error.what() + ")"};
            }
            const auto fd = call.vm.NewIntrinsicInstance("Ljava/io/FileDescriptor;");
            call.vm.IO().SetDescriptor(
                fd, {dx::IoRuntime::DescriptorKind::apk_entry, path, data_offset, false, {}, {}, {}});
            const auto fd_root = call.vm.ProtectReferences(std::array{fd});
            static_cast<void>(CallAndroidMethod(call.vm, fd, "setInt$", "(I)V", {dx::VmValue::Int(-1)}));
            const auto pfd = NewParcelFileDescriptor(call, fd);
            const auto pfd_root = call.vm.ProtectReferences(std::array{pfd});
            const auto descriptor = call.vm.NewIntrinsicInstance("Landroid/content/res/AssetFileDescriptor;");
            const auto length = static_cast<std::uint64_t>(entry->uncompressed_size);
            Initialize(call, descriptor, pfd, static_cast<std::int64_t>(data_offset),
                       static_cast<std::int64_t>(length));
            return dx::VmValue::Ref(descriptor);
        });
    builder.FinalMethod("list", "(Ljava/lang/String;)[Ljava/lang/String;", [context](dx::IntrinsicContext& call) {
        const auto requested = call.vm.StringUtf8(call.arguments[0].ref);
        const auto prefix = requested.empty() ? std::string{"assets/"} : "assets/" + requested + "/";
        if (IsSystemAssets(context, call.receiver))
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "system asset listing is unavailable"};
        std::set<std::string> children;
        for (const auto& entry : context->archive.entries) {
            if (!entry.name.starts_with(prefix)) continue;
            const auto remainder = std::string_view(entry.name).substr(prefix.size());
            if (remainder.empty()) continue;
            const auto slash = remainder.find('/');
            children.emplace(remainder.substr(0, slash));
        }
        auto& vm = call.vm;
        const auto array_class = vm.Linker().ResolveDescriptor("[Ljava/lang/String;");
        const auto string_class = vm.Linker().ResolveDescriptor("Ljava/lang/String;");
        const auto array = vm.Model().NewObjectArray(array_class, string_class, static_cast<JniSize>(children.size()));
        JniSize index{};
        for (const auto& child : children) {
            vm.Model().SetObjectElement(array, index++, vm.NewStringUtf8(child));
        }
        return dx::VmValue::Ref(array);
    });
    builder.FinalMethod("open", "(Ljava/lang/String;)Ljava/io/InputStream;", [context](dx::IntrinsicContext& call) {
        if (IsSystemAssets(context, call.receiver))
            throw dx::VmJavaThrow{"Ljava/io/FileNotFoundException;", "system asset is unavailable"};
        const auto name = call.vm.StringUtf8(call.arguments[0].ref);
        return dx::VmValue::Ref(OpenStream(call, context, ReadApkFile(context, "assets/" + name)));
    });
    builder.FinalMethod("open", "(Ljava/lang/String;I)Ljava/io/InputStream;", [context](dx::IntrinsicContext& call) {
        if (IsSystemAssets(context, call.receiver))
            throw dx::VmJavaThrow{"Ljava/io/FileNotFoundException;", "system asset is unavailable"};
        const auto name = call.vm.StringUtf8(call.arguments[0].ref);
        return dx::VmValue::Ref(OpenStream(call, context, ReadApkFile(context, "assets/" + name)));
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_content_res_Resources.cpp ----
#include "catalog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

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
    if (context.surface_width == 0U || context.surface_height == 0U || !std::isfinite(context.ui_density) ||
        context.ui_density <= 0.0F) {
        throw std::invalid_argument("Configuration requires positive surface metrics and density");
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

void SetIntField(ogplay::runtime::dexvm::IntrinsicContext& call, const ogplay::runtime::dexvm::VmObjectRef object,
                 const char* name, const std::int32_t value) {
    const auto field = call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(object), name, "I");
    if (!field.has_value()) {
        throw std::logic_error(std::string("integer field missing: ") + name);
    }
    const auto slot = call.vm.Linker().Field(*field).slot;
    call.vm.Model().InstanceSlots(object)[slot] = {static_cast<std::uint32_t>(value),
                                                   ogplay::runtime::dexvm::SlotTag::cat1};
}

std::size_t FieldSlot(ogplay::runtime::dexvm::IntrinsicContext& call, const ogplay::runtime::dexvm::VmObjectRef object,
                      const char* name, const char* descriptor) {
    const auto field = call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(object), name, descriptor);
    if (!field.has_value()) {
        throw std::logic_error(std::string("XML parser field missing: ") + name);
    }
    return call.vm.Linker().Field(*field).slot;
}

void SetRefField(ogplay::runtime::dexvm::IntrinsicContext& call, const ogplay::runtime::dexvm::VmObjectRef object,
                 const char* name, const char* descriptor, const ogplay::runtime::dexvm::VmObjectRef value) {
    call.vm.Model().InstanceSlots(object)[FieldSlot(call, object, name, descriptor)] = {
        value.Value(), ogplay::runtime::dexvm::SlotTag::ref};
}

ogplay::runtime::dexvm::VmObjectRef GetRefField(ogplay::runtime::dexvm::IntrinsicContext& call,
                                                const ogplay::runtime::dexvm::VmObjectRef object, const char* name,
                                                const char* descriptor) {
    return ogplay::runtime::dexvm::VmObjectRef(
        call.vm.Model().InstanceSlots(object)[FieldSlot(call, object, name, descriptor)].bits);
}

std::int32_t GetIntField(ogplay::runtime::dexvm::IntrinsicContext& call,
                         const ogplay::runtime::dexvm::VmObjectRef object, const char* name) {
    return static_cast<std::int32_t>(call.vm.Model().InstanceSlots(object)[FieldSlot(call, object, name, "I")].bits);
}

ogplay::runtime::dexvm::VmObjectRef
ConfigurationSingleton(ogplay::runtime::dexvm::IntrinsicContext& call,
                       const ogplay::runtime::android_intrinsics::Context& context) {
    const auto found = context->singletons.find("configuration");
    if (found != context->singletons.end()) return found->second;

    const auto owner = call.vm.Linker().ResolveDescriptor("Landroid/content/res/Configuration;");
    const auto initialized = call.vm.EnsureClassInitialized(owner);
    if (initialized.exception.IsValid()) {
        throw ogplay::runtime::dexvm::VmJavaThrow{call.vm.Linker().Class(initialized.exception_class).descriptor,
                                                  initialized.exception_message, initialized.exception};
    }
    const auto instance = call.vm.NewIntrinsicInstance("Landroid/content/res/Configuration;");
    const auto root = call.vm.ProtectReferences(std::array{instance});
    const auto constructor = call.vm.Linker().FindDirectMethod(owner, "<init>", "()V");
    if (!constructor.has_value()) {
        throw std::logic_error("Configuration constructor is not linked");
    }
    const auto outcome = call.vm.Call(*constructor, std::array{ogplay::runtime::dexvm::VmValue::Ref(instance)});
    if (outcome.exception.IsValid()) {
        throw ogplay::runtime::dexvm::VmJavaThrow{call.vm.Linker().Class(outcome.exception_class).descriptor,
                                                  outcome.exception_message, outcome.exception};
    }
    context->singletons.emplace("configuration", instance);
    return instance;
}

void RequireOpenXmlParser(ogplay::runtime::dexvm::IntrinsicContext& call) {
    if (GetIntField(call, call.receiver, "mClosed") != 0) {
        throw ogplay::runtime::dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "XML parser is closed"};
    }
}

ogplay::runtime::dexvm::VmObjectRef MakeXmlParser(ogplay::runtime::dexvm::IntrinsicContext& call,
                                                  const std::vector<ogplay::loader::BinaryXmlPullEvent>& events) {
    namespace dx = ogplay::runtime::dexvm;
    const auto parser = call.vm.NewIntrinsicInstance("Landroid/content/res/XmlResourceParser$Impl;");
    const std::array parser_references{parser};
    [[maybe_unused]] const auto parser_roots = call.vm.ProtectReferences(parser_references);
    auto& model = call.vm.Model();
    const auto count = static_cast<ogplay::runtime::JniSize>(events.size());
    const auto types = model.NewPrimitiveArray(call.vm.Linker().ResolveDescriptor("[I"),
                                               ogplay::runtime::JniPrimitiveKind::integer, count);
    SetRefField(call, parser, "mEventTypes", "[I", types);
    const auto names = model.NewObjectArray(call.vm.Linker().ResolveDescriptor("[Ljava/lang/String;"),
                                            call.vm.Linker().ResolveDescriptor("Ljava/lang/String;"), count);
    SetRefField(call, parser, "mNames", "[Ljava/lang/String;", names);
    const auto texts = model.NewObjectArray(call.vm.Linker().ResolveDescriptor("[Ljava/lang/String;"),
                                            call.vm.Linker().ResolveDescriptor("Ljava/lang/String;"), count);
    SetRefField(call, parser, "mTexts", "[Ljava/lang/String;", texts);
    for (std::size_t index = 0; index < events.size(); ++index) {
        const auto array_index = static_cast<ogplay::runtime::JniSize>(index);
        model.SetPrimitiveElement(types, array_index, static_cast<std::uint32_t>(events[index].type));
        if (!events[index].name.empty()) {
            model.SetObjectElement(names, array_index, call.vm.NewStringUtf8(events[index].name));
        }
        if (events[index].type == ogplay::loader::BinaryXmlPullEventType::text) {
            model.SetObjectElement(texts, array_index, call.vm.NewStringUtf8(events[index].text));
        }
    }
    SetIntField(call, parser, "mIndex", 0);
    SetIntField(call, parser, "mClosed", 0);
    return parser;
}

}  // namespace

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_res_Resources_NotFoundException(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/res/Resources$NotFoundException;",
                                                    "Ljava/lang/RuntimeException;");
    builder.Constructor("()V", [](dx::IntrinsicContext& c) {
        c.vm.InitializeThrowable(c.receiver);
        return dx::VmValue::Void();
    });
    builder.Constructor("(Ljava/lang/String;)V", [](dx::IntrinsicContext& call) {
        call.vm.InitializeThrowable(call.receiver, call.arguments[0].ref);
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

Decl Declare_android_content_res_XmlResourceParser(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface(
        "Landroid/content/res/XmlResourceParser;",
        {"Lorg/xmlpull/v1/XmlPullParser;", "Landroid/util/AttributeSet;", "Ljava/lang/AutoCloseable;"});
    builder.UnimplementedVirtual("close", "()V", dx::kAccPublic | dx::kAccAbstract);
    return std::move(builder).Build();
}

Decl Declare_android_content_res_XmlResourceParser_Impl(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/res/XmlResourceParser$Impl;",
                                                    "Ljava/lang/Object;", {"Landroid/content/res/XmlResourceParser;"});
    builder.InstanceField("mEventTypes", "[I", dx::kAccPrivate | dx::kAccFinal)
        .InstanceField("mNames", "[Ljava/lang/String;", dx::kAccPrivate | dx::kAccFinal)
        .InstanceField("mTexts", "[Ljava/lang/String;", dx::kAccPrivate | dx::kAccFinal)
        .InstanceField("mIndex", "I", dx::kAccPrivate)
        .InstanceField("mClosed", "I", dx::kAccPrivate);
    // 返回解析器当前所在的 XML 事件类型。
    builder.FinalMethod("getEventType", "()I", [](dx::IntrinsicContext& call) {
        RequireOpenXmlParser(call);
        const auto types = GetRefField(call, call.receiver, "mEventTypes", "[I");
        return dx::VmValue::Int(static_cast<std::int32_t>(
            call.vm.Model().GetPrimitiveElement(types, GetIntField(call, call.receiver, "mIndex"))));
    });
    // 推进到下一个 XML 事件并返回其类型。
    builder.FinalMethod("next", "()I", [](dx::IntrinsicContext& call) {
        RequireOpenXmlParser(call);
        const auto types = GetRefField(call, call.receiver, "mEventTypes", "[I");
        auto index = GetIntField(call, call.receiver, "mIndex");
        const auto last = call.vm.Model().ArrayLength(types) - 1;
        if (index < last) ++index;
        SetIntField(call, call.receiver, "mIndex", index);
        return dx::VmValue::Int(static_cast<std::int32_t>(call.vm.Model().GetPrimitiveElement(types, index)));
    });
    const auto current_string = [](const char* field) {
        return [field](dx::IntrinsicContext& call) {
            RequireOpenXmlParser(call);
            const auto values = GetRefField(call, call.receiver, field, "[Ljava/lang/String;");
            return dx::VmValue::Ref(
                call.vm.Model().GetObjectElement(values, GetIntField(call, call.receiver, "mIndex")));
        };
    };
    // 返回当前开始或结束标签的名称。
    builder.FinalMethod("getName", "()Ljava/lang/String;", current_string("mNames"));
    // 返回当前文本事件的内容。
    builder.FinalMethod("getText", "()Ljava/lang/String;", current_string("mTexts"));
    // The complete BootDex interface does not expand the bounded AXML reader.
    // Publish unsupported entries so calls use the normal ledger/failure path.
    constexpr std::pair<const char*, const char*> unsupported[] = {
        {"defineEntityReplacementText", "(Ljava/lang/String;Ljava/lang/String;)V"},
        {"getAttributeCount", "()I"},
        {"getAttributeName", "(I)Ljava/lang/String;"},
        {"getAttributeNamespace", "(I)Ljava/lang/String;"},
        {"getAttributePrefix", "(I)Ljava/lang/String;"},
        {"getAttributeType", "(I)Ljava/lang/String;"},
        {"getAttributeValue", "(I)Ljava/lang/String;"},
        {"getAttributeValue", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"},
        {"getColumnNumber", "()I"},
        {"getDepth", "()I"},
        {"getFeature", "(Ljava/lang/String;)Z"},
        {"getInputEncoding", "()Ljava/lang/String;"},
        {"getLineNumber", "()I"},
        {"getNamespace", "()Ljava/lang/String;"},
        {"getNamespace", "(Ljava/lang/String;)Ljava/lang/String;"},
        {"getNamespaceCount", "(I)I"},
        {"getNamespacePrefix", "(I)Ljava/lang/String;"},
        {"getNamespaceUri", "(I)Ljava/lang/String;"},
        {"getPositionDescription", "()Ljava/lang/String;"},
        {"getPrefix", "()Ljava/lang/String;"},
        {"getProperty", "(Ljava/lang/String;)Ljava/lang/Object;"},
        {"getTextCharacters", "([I)[C"},
        {"isAttributeDefault", "(I)Z"},
        {"isEmptyElementTag", "()Z"},
        {"isWhitespace", "()Z"},
        {"nextTag", "()I"},
        {"nextText", "()Ljava/lang/String;"},
        {"nextToken", "()I"},
        {"require", "(ILjava/lang/String;Ljava/lang/String;)V"},
        {"setFeature", "(Ljava/lang/String;Z)V"},
        {"setInput", "(Ljava/io/InputStream;Ljava/lang/String;)V"},
        {"setInput", "(Ljava/io/Reader;)V"},
        {"setProperty", "(Ljava/lang/String;Ljava/lang/Object;)V"},
    };
    for (const auto& [name, signature] : unsupported)
        builder.UnimplementedVirtual(name, signature, dx::kAccPublic | dx::kAccFinal);
    // 关闭解析器并释放其事件数组，重复关闭保持幂等。
    builder.FinalMethod("close", "()V", [](dx::IntrinsicContext& call) {
        SetRefField(call, call.receiver, "mEventTypes", "[I", dx::VmObjectRef{});
        SetRefField(call, call.receiver, "mNames", "[Ljava/lang/String;", dx::VmObjectRef{});
        SetRefField(call, call.receiver, "mTexts", "[Ljava/lang/String;", dx::VmObjectRef{});
        SetIntField(call, call.receiver, "mClosed", 1);
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_res_Resources(const Context& context) {
    const auto system_resources = std::make_shared<SystemResourceCache>();
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/res/Resources;", "Ljava/lang/Object;");
    const auto assets =
        builder.BoundInstanceField("mAssets", "Landroid/content/res/AssetManager;", dx::kAccPrivate | dx::kAccFinal);
    builder.VirtualMethod(
        "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", [context](dx::IntrinsicContext& call) {
            if (IsSystemResources(context, call.receiver))
                throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;",
                                      "system Drawable resources are not available"};
            // Keep the paired ROM path for APK resources and already reviewed ROM skins.
            const auto id = static_cast<std::uint32_t>(call.arguments[0].AsInt());
            const auto density = static_cast<std::uint32_t>(std::lround(context->ui_density * 160.0F));
            if ((id >> 24U) != 1U || context->theme_resources.Drawable(id, density)) {
                const auto value = call.vm.NewIntrinsicInstance("Landroid/util/TypedValue;");
                const auto root = call.vm.ProtectReferences(std::array{value});
                static_cast<void>(CallAndroidMethod(call.vm, call.receiver, "getValue",
                                                    "(ILandroid/util/TypedValue;Z)V",
                                                    {call.arguments[0], dx::VmValue::Ref(value), dx::VmValue::Int(1)}));
                return CallAndroidMethod(call.vm, call.receiver, "loadDrawable",
                                         "(Landroid/util/TypedValue;I)Landroid/graphics/drawable/Drawable;",
                                         {dx::VmValue::Ref(value), call.arguments[0]});
            }
            try {
                return dx::VmValue::Ref(
                    MakeResourceDrawable(call, context, static_cast<std::uint32_t>(call.arguments[0].AsInt())));
            } catch (const dx::VmJavaThrow&) {
                throw;
            } catch (const std::runtime_error& error) {
                throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;", error.what()};
            }
        });
    {
        builder.VirtualMethod(
            "loadColorStateList", "(Landroid/util/TypedValue;I)Landroid/content/res/ColorStateList;",
            [context](dx::IntrinsicContext& call) {
                return ThemeChecked(call, context, [&] {
                    const auto value = call.arguments[0].ref;
                    const auto type = XmlField(call.vm, value, "type");
                    if (type == 3) {
                        const auto id = XmlField(call.vm, value, "resourceId");
                        const auto color = context->theme_resources.Color(id);
                        if (!color || color->xml.empty() || XmlField(call.vm, value, "data") != color->value.data ||
                            XmlField(call.vm, value, "assetCookie") != static_cast<std::uint32_t>(color->value.cookie))
                            throw std::runtime_error("unknown ROM ColorStateList XML");
                        for (const auto* descriptor :
                             {"Landroid/content/res/XmlBlock;", "Landroid/content/res/ColorStateList;"}) {
                            const auto initialized =
                                call.vm.EnsureClassInitialized(call.vm.Linker().ResolveDescriptor(descriptor));
                            if (initialized.exception.IsValid())
                                throw dx::VmJavaThrow{call.vm.Linker().Class(initialized.exception_class).descriptor,
                                                      initialized.exception_message, initialized.exception};
                        }
                        const auto array = call.vm.Model().NewPrimitiveArray(
                            call.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                            static_cast<std::int32_t>(color->xml.size()));
                        const auto block = call.vm.NewIntrinsicInstance("Landroid/content/res/XmlBlock;");
                        const auto roots = call.vm.ProtectReferences(std::array{array, block});
                        call.vm.Model().FillPrimitiveData(array, color->xml);
                        const auto ctor =
                            call.vm.Linker().FindDirectMethod(call.vm.Model().ObjectClass(block), "<init>", "([B)V");
                        const auto built =
                            call.vm.Call(*ctor, std::array{dx::VmValue::Ref(block), dx::VmValue::Ref(array)});
                        if (built.exception.IsValid())
                            throw dx::VmJavaThrow{call.vm.Linker().Class(built.exception_class).descriptor,
                                                  built.exception_message, built.exception};
                        const auto parser =
                            CallAndroidMethod(call.vm, block, "newParser", "()Landroid/content/res/XmlResourceParser;")
                                .ref;
                        const auto parser_root = call.vm.ProtectReferences(std::array{parser});
                        static_cast<void>(CallAndroidMethod(call.vm, block, "close", "()V"));
                        const auto klass = call.vm.Linker().ResolveDescriptor("Landroid/content/res/ColorStateList;");
                        const auto method = call.vm.Linker().FindDirectMethod(
                            klass, "createFromXml",
                            "(Landroid/content/res/Resources;Lorg/xmlpull/v1/XmlPullParser;)Landroid/content/res/ColorStateList;");
                        const auto result = call.vm.Call(
                            *method, std::array{dx::VmValue::Ref(call.receiver), dx::VmValue::Ref(parser)});
                        const auto result_root =
                            call.vm.ProtectReferences(std::array{result.value.ref, result.exception});
                        static_cast<void>(CallAndroidMethod(call.vm, parser, "close", "()V"));
                        if (result.exception.IsValid())
                            throw dx::VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor,
                                                  result.exception_message, result.exception};
                        return result.value;
                    }
                    if (type < 28 || type > 31) throw std::runtime_error("unsupported ColorStateList value");
                    const auto method = call.vm.Linker().FindDirectMethod(
                        call.vm.Linker().ResolveDescriptor("Landroid/content/res/ColorStateList;"), "valueOf",
                        "(I)Landroid/content/res/ColorStateList;");
                    if (!method) throw std::logic_error("ROM ColorStateList.valueOf unavailable");
                    const auto initialized = call.vm.EnsureClassInitialized(
                        call.vm.Linker().ResolveDescriptor("Landroid/content/res/ColorStateList;"));
                    if (initialized.exception.IsValid())
                        throw dx::VmJavaThrow{call.vm.Linker().Class(initialized.exception_class).descriptor,
                                              initialized.exception_message, initialized.exception};
                    const auto result = call.vm.Call(*method, std::array{XmlInt(XmlField(call.vm, value, "data"))});
                    if (result.exception.IsValid())
                        throw dx::VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor,
                                              result.exception_message, result.exception};
                    return result.value;
                });
            });
        builder.VirtualMethod(
            "loadDrawable", "(Landroid/util/TypedValue;I)Landroid/graphics/drawable/Drawable;",
            [context](dx::IntrinsicContext& call) {
                return ThemeChecked(call, context, [&] {
                    const auto value = call.arguments[0].ref;
                    const auto type = XmlField(call.vm, value, "type");
                    const auto data = XmlField(call.vm, value, "data");
                    const bool color = type >= 28 && type <= 31;
                    const auto id = XmlField(call.vm, value, "resourceId");
                    if ((id >> 24) == 1 && type == 1 && data == id)
                        return dx::VmValue::Ref(MakeRomSkin(call, context, id));
                    std::shared_ptr<const ui::UiBitmap> pixels;
                    if (!color) {
                        if (type != 3 || !id || XmlField(call.vm, value, "assetCookie") != 1)
                            throw std::runtime_error("leaf Drawable requires APK bitmap or scalar color");
                        const auto* entry = SelectUiResourceEntry(*context, id);
                        if (entry->value_type != type || entry->value_data != data ||
                            entry->density != XmlField(call.vm, value, "density"))
                            throw std::runtime_error("leaf Drawable resource facts disagree");
                        pixels = DecodeLeafBitmap(*context, *entry);
                    }
                    const auto* descriptor = color ? "Landroid/graphics/drawable/ColorDrawable;"
                                                   : "Landroid/graphics/drawable/BitmapDrawable;";
                    const auto klass = call.vm.Linker().ResolveDescriptor(descriptor);
                    const auto initialized = call.vm.EnsureClassInitialized(klass);
                    if (initialized.exception.IsValid())
                        throw dx::VmJavaThrow{call.vm.Linker().Class(initialized.exception_class).descriptor,
                                              initialized.exception_message, initialized.exception};
                    const auto object = call.vm.NewIntrinsicInstance(descriptor);
                    const auto root = call.vm.ProtectReferences(std::array{object});
                    std::vector<dx::VmValue> args{dx::VmValue::Ref(object)};
                    const char* signature = "(I)V";
                    dx::VmObjectRef bitmap;
                    if (color)
                        args.push_back(XmlInt(data));
                    else {
                        bitmap = call.vm.NewIntrinsicInstance("Landroid/graphics/Bitmap;");
                        std::vector<std::uint32_t> argb;
                        for (std::size_t i = 0; i < pixels->rgba8.size(); i += 4)
                            argb.push_back((static_cast<std::uint32_t>(pixels->rgba8[i + 3]) << 24U) |
                                           (static_cast<std::uint32_t>(pixels->rgba8[i]) << 16U) |
                                           (static_cast<std::uint32_t>(pixels->rgba8[i + 1]) << 8U) |
                                           pixels->rgba8[i + 2]);
                        auto& backing = context->bitmaps[bitmap.Value()];
                        backing.width = pixels->width;
                        backing.height = pixels->height;
                        backing.mutable_pixels = false;
                        backing.pixels =
                            std::make_shared<BitmapPixels>(pixels->width, pixels->height, 5, std::move(argb));
                        const auto density = XmlField(call.vm, value, "density");
                        backing.density = density == 65535 ? 0
                                          : density == 0   ? 160
                                                           : static_cast<std::int32_t>(density);
                        signature = "(Landroid/content/res/Resources;Landroid/graphics/Bitmap;)V";
                        args.push_back(dx::VmValue::Ref(call.receiver));
                        args.push_back(dx::VmValue::Ref(bitmap));
                    }
                    const auto bitmap_root = call.vm.ProtectReferences(std::array{bitmap});
                    const auto ctor = call.vm.Linker().FindDirectMethod(klass, "<init>", signature);
                    if (!ctor) throw std::logic_error("leaf Drawable constructor unavailable");
                    const auto outcome = call.vm.Call(*ctor, args);
                    if (outcome.exception.IsValid())
                        throw dx::VmJavaThrow{call.vm.Linker().Class(outcome.exception_class).descriptor,
                                              outcome.exception_message, outcome.exception};
                    auto& state = context->ui_drawables.at(object.Value());
                    state.resource_id = id;
                    state.leaf_pixels = std::move(pixels);
                    return dx::VmValue::Ref(object);
                });
            });
    }
    builder.StaticMethod("getSystem", "()Landroid/content/res/Resources;", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(ResourceObject(call, context, true));
    });
    builder.FinalMethod("getAssets", "()Landroid/content/res/AssetManager;", [assets](dx::IntrinsicContext& call) {
        const dx::IntrinsicCall fields(call);
        const auto value = fields.GetRef(assets);
        if (!value.IsValid())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Resources has no AssetManager"};
        return dx::VmValue::Ref(value);
    });
    builder.VirtualMethod("getInteger", "(I)I", [context, system_resources](dx::IntrinsicContext& call) {
        const auto id = static_cast<std::uint32_t>(call.arguments[0].AsInt());
        if (const auto* resource = FindSystemResource(context, *system_resources, id);
            resource != nullptr && std::holds_alternative<std::int32_t>(*resource))
            return dx::VmValue::Int(std::get<std::int32_t>(*resource));
        throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;",
                              "unknown system integer resource " + std::to_string(id)};
    });
    const auto metrics =
        builder.BoundInstanceField("mMetrics", "Landroid/util/DisplayMetrics;", dx::kAccPrivate | dx::kAccFinal);
    builder.VirtualMethod(
        "getDisplayMetrics", "()Landroid/util/DisplayMetrics;", [context, metrics](dx::IntrinsicContext& call) {
            const dx::IntrinsicCall fields(call);
            auto value = fields.GetRef(metrics);
            if (!value.IsValid()) {
                const auto owner = call.vm.Linker().ResolveDescriptor("Landroid/util/DisplayMetrics;");
                const auto initialized = call.vm.EnsureClassInitialized(owner);
                if (initialized.exception.IsValid())
                    throw dx::VmJavaThrow{call.vm.Linker().Class(initialized.exception_class).descriptor,
                                          initialized.exception_message, initialized.exception};
                value = call.vm.NewIntrinsicInstance("Landroid/util/DisplayMetrics;");
                const auto root = call.vm.ProtectReferences(std::array{value});
                const auto constructor = call.vm.Linker().FindDirectMethod(owner, "<init>", "()V");
                if (!constructor)
                    throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                                         "DisplayMetrics constructor is not linked");
                const auto outcome = call.vm.Call(*constructor, std::array{dx::VmValue::Ref(value)});
                if (outcome.exception.IsValid())
                    throw dx::VmJavaThrow{call.vm.Linker().Class(outcome.exception_class).descriptor,
                                          outcome.exception_message, outcome.exception};
                fields.SetRef(metrics, value);
            }
            PopulateAndroidDisplayMetrics(call, context, value);
            return dx::VmValue::Ref(value);
        });
    builder.VirtualMethod(
        "getConfiguration", "()Landroid/content/res/Configuration;", [context](dx::IntrinsicContext& call) {
            const auto instance = ConfigurationSingleton(call, context);
            const auto display = Api19ConfigurationFacts(*context);
            // The managed desktop surface exposes touch-style pointer input
            // and an alphabetic host keyboard through the Android input path.
            SetIntField(call, instance, "touchscreen", 3);         // FINGER
            SetIntField(call, instance, "keyboard", 2);            // QWERTY
            SetIntField(call, instance, "keyboardHidden", 1);      // NO
            SetIntField(call, instance, "hardKeyboardHidden", 1);  // NO
            SetIntField(call, instance, "navigation", 1);          // NONAV
            SetIntField(call, instance, "navigationHidden", 2);    // YES
            SetIntField(call, instance, "orientation",
                        context->surface_width <= context->surface_height ? 1  // PORTRAIT
                                                                          : 2  // LANDSCAPE
            );
            SetIntField(call, instance, "screenWidthDp", display.width_dp);
            SetIntField(call, instance, "screenHeightDp", display.height_dp);
            SetIntField(call, instance, "smallestScreenWidthDp", std::min(display.width_dp, display.height_dp));
            SetIntField(call, instance, "densityDpi", display.density_dpi);
            SetIntField(call, instance, "screenLayout", display.screen_layout);
            dx::VmObjectRef locale;
            {
                const auto locale_class = call.vm.Linker().ResolveDescriptor("Ljava/util/Locale;");
                const auto initialized = call.vm.EnsureClassInitialized(locale_class);
                if (initialized.exception.IsValid()) {
                    throw dx::VmJavaThrow{call.vm.Linker().Class(initialized.exception_class).descriptor,
                                          initialized.exception_message, initialized.exception};
                }
                const auto get_default =
                    call.vm.Linker().FindDirectMethod(locale_class, "getDefault", "()Ljava/util/Locale;");
                if (!get_default.has_value()) {
                    throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "Locale.getDefault is not linked");
                }
                const auto outcome = call.vm.Call(*get_default, {});
                if (outcome.exception.IsValid()) {
                    throw dx::VmJavaThrow{call.vm.Linker().Class(outcome.exception_class).descriptor,
                                          outcome.exception_message, outcome.exception};
                }
                locale = outcome.value.ref;
                if (!locale.IsValid()) {
                    throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "Locale.getDefault returned null");
                }
                SetRefField(call, instance, "locale", "Ljava/util/Locale;", locale);
            }
            static_cast<void>(CallAndroidMethod(call.vm, instance, "setLayoutDirection", "(Ljava/util/Locale;)V",
                                                {dx::VmValue::Ref(locale)}));
            return dx::VmValue::Ref(instance);
        });
    builder.VirtualMethod(
        "getIdentifier", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
        [context](dx::IntrinsicContext& call) {
            if (!call.arguments[0].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "name is null"};
            const auto name = call.vm.StringUtf8(call.arguments[0].ref);
            const auto type_default =
                call.arguments[1].ref.IsValid() ? call.vm.StringUtf8(call.arguments[1].ref) : std::string{};
            const auto package_default =
                call.arguments[2].ref.IsValid() ? call.vm.StringUtf8(call.arguments[2].ref) : std::string{};
            // Preserve the independent system Resources boundary.
            if (IsSystemResources(context, call.receiver)) {
                if (type_default == "style")
                    return ThemeChecked(call, context, [&] { return XmlInt(context->theme_resources.ThemeId(name)); });
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "system resource names are unavailable"};
            }
            // Use the same Integer parser as Java callers; only a format failure
            // falls through to ResTable name lookup.
            const auto integer = call.vm.Linker().ResolveDescriptor("Ljava/lang/Integer;");
            const auto require = [&](const dx::VmCallOutcome& outcome) {
                if (outcome.exception.IsValid())
                    throw dx::VmJavaThrow{call.vm.Linker().Class(outcome.exception_class).descriptor,
                                          outcome.exception_message, outcome.exception};
            };
            require(call.vm.EnsureClassInitialized(integer));
            const auto parse = call.vm.Linker().FindDirectMethod(integer, "parseInt", "(Ljava/lang/String;)I");
            if (!parse) throw std::logic_error("Integer.parseInt is not linked");
            const auto number = call.vm.Call(*parse, std::array{call.arguments[0]});
            if (!number.exception.IsValid()) return number.value;
            if (call.vm.Linker().Class(number.exception_class).descriptor != "Ljava/lang/NumberFormatException;")
                require(number);

            const auto unsupported = [&](const char* message) -> dx::VmValue {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.theme.unsupported", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", message};
            };
            std::string_view entry_name = name;
            if (entry_name.starts_with('^')) return unsupported("internal resource ID names are unavailable");
            if (entry_name.starts_with('@')) {
                entry_name.remove_prefix(1);
                if (entry_name.starts_with('*')) entry_name.remove_prefix(1);
            }
            std::string_view package_name = package_default, type_name = type_default;
            if (const auto colon = entry_name.rfind(':'); colon != std::string_view::npos) {
                package_name = entry_name.substr(0, colon);
                entry_name.remove_prefix(colon + 1);
            }
            if (const auto slash = entry_name.rfind('/'); slash != std::string_view::npos) {
                type_name = entry_name.substr(0, slash);
                entry_name.remove_prefix(slash + 1);
            }
            if (package_name.empty() || type_name.empty() || entry_name.empty()) return dx::VmValue::Int(0);
            if (package_name == "android") {
                if (type_name != "style") return unsupported("system resource names are unavailable");
                return ThemeChecked(call, context,
                                    [&] { return XmlInt(context->theme_resources.ThemeId(entry_name)); });
            }
            if (package_name != context->arsc.package_name) return dx::VmValue::Int(0);
            const auto* entry = context->arsc.FindByName(type_name, entry_name);
            return dx::VmValue::Int(entry == nullptr ? 0 : static_cast<std::int32_t>(entry->resource_id));
        });
    builder.VirtualMethod("openRawResource", "(I)Ljava/io/InputStream;", [](dx::IntrinsicContext& call) {
        return CallAndroidMethod(call.vm, call.receiver, "openRawResource",
                                 "(ILandroid/util/TypedValue;)Ljava/io/InputStream;",
                                 {call.arguments[0], dx::VmValue::Ref(dx::VmObjectRef{})});
    });
    builder.VirtualMethod(
        "openRawResource", "(ILandroid/util/TypedValue;)Ljava/io/InputStream;", [context](dx::IntrinsicContext& call) {
            if (IsSystemResources(context, call.receiver))
                throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;",
                                      "system raw resource is unavailable"};
            const auto id = static_cast<std::uint32_t>(call.arguments[0].AsInt());
            try {
                const auto& entry = ResolveUiResourceEntry(*context, id);
                if (entry.value_type != 3 || !entry.string_value)
                    throw std::runtime_error("raw resource is not a file");
                const auto value = call.arguments[1].ref;
                if (value.IsValid()) {
                    SetIntField(call, value, "type", entry.value_type);
                    SetIntField(call, value, "data", static_cast<std::int32_t>(entry.value_data));
                    SetIntField(call, value, "resourceId", static_cast<std::int32_t>(entry.resource_id));
                    SetIntField(call, value, "assetCookie", 1);
                    SetIntField(call, value, "density", entry.density);
                    SetRefField(call, value, "string", "Ljava/lang/CharSequence;",
                                call.vm.NewStringUtf8(*entry.string_value));
                }
                return dx::VmValue::Ref(OpenStream(call, context, ReadApkFile(context, *entry.string_value)));
            } catch (const dx::DexVmError&) {
                throw;
            } catch (const std::bad_alloc&) {
                throw;
            } catch (const std::exception& error) {
                throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;", error.what()};
            }
        });
    builder.VirtualMethod(
        "openRawResourceFd", "(I)Landroid/content/res/AssetFileDescriptor;", [context](dx::IntrinsicContext& call) {
            if (IsSystemResources(context, call.receiver))
                throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;",
                                      "system raw resource is unavailable"};
            const auto resource_id = static_cast<std::uint32_t>(call.arguments[0].AsInt());
            const auto* entry = context->arsc.FindById(resource_id);
            if (entry == nullptr || !entry->string_value.has_value()) {
                throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;",
                                      "resource id has no file entry: " + std::to_string(resource_id)};
            }
            try {
                return dx::VmValue::Ref(MakeStoredApkAssetFileDescriptor(call, context, *entry->string_value));
            } catch (const dx::VmJavaThrow& error) {
                throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;", error.message};
            }
        });
    {
        builder.VirtualMethod(
            "getLayout", "(I)Landroid/content/res/XmlResourceParser;", [context](dx::IntrinsicContext& call) {
                const auto id = static_cast<std::uint32_t>(call.arguments[0].AsInt());
                const auto* entry = context->arsc.FindById(id);
                if (IsSystemResources(context, call.receiver) || !entry || entry->type_name != "layout" ||
                    !entry->string_value)
                    throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;",
                                          "layout XML resource is unavailable: " + std::to_string(id)};
                const auto assets =
                    CallAndroidMethod(call.vm, call.receiver, "getAssets", "()Landroid/content/res/AssetManager;").ref;
                const auto path = call.vm.NewStringUtf8(*entry->string_value);
                const auto roots = call.vm.ProtectReferences(std::array{assets, path});
                return CallAndroidMethod(call.vm, assets, "openXmlResourceParser",
                                         "(Ljava/lang/String;)Landroid/content/res/XmlResourceParser;",
                                         {dx::VmValue::Ref(path)});
            });
    }
    // 按资源 ID 打开编译后的 XML pull parser。
    builder.VirtualMethod(
        "getXml", "(I)Landroid/content/res/XmlResourceParser;", [context](dx::IntrinsicContext& call) {
            if (IsSystemResources(context, call.receiver))
                throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;",
                                      "system XML resource is unavailable"};
            const auto resource_id = static_cast<std::uint32_t>(call.arguments[0].AsInt());
            const auto* entry = context->arsc.FindById(resource_id);
            if (entry == nullptr || !entry->string_value.has_value()) {
                throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;",
                                      "resource id has no XML file entry: " + std::to_string(resource_id)};
            }
            std::vector<std::byte> bytes;
            try {
                bytes = ReadApkFile(context, *entry->string_value);
            } catch (const dx::VmJavaThrow& error) {
                throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;", error.message};
            }
            std::vector<loader::BinaryXmlPullEvent> events;
            try {
                events = loader::ParseBinaryXmlPullEvents(bytes);
            } catch (const std::exception& error) {
                throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;",
                                      "resource XML is invalid: " + std::string(error.what())};
            }
            return dx::VmValue::Ref(MakeXmlParser(call, events));
        });
    builder.VirtualMethod(
        "getText", "(I)Ljava/lang/CharSequence;",
        [context, system_resources](dx::IntrinsicContext& call) -> dx::VmValue {
            const auto id = static_cast<std::uint32_t>(call.arguments[0].AsInt());
            const auto coerce = [&](std::uint8_t type, std::uint32_t data) {
                const auto owner = call.vm.Linker().ResolveDescriptor("Landroid/util/TypedValue;");
                const auto method =
                    call.vm.Linker().FindDirectMethod(owner, "coerceToString", "(II)Ljava/lang/String;");
                if (!method)
                    throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                                         "TypedValue.coerceToString is not linked");
                const auto outcome = call.vm.Call(
                    *method, std::array{dx::VmValue::Int(type), dx::VmValue::Int(std::bit_cast<std::int32_t>(data))});
                if (outcome.exception.IsValid())
                    throw dx::VmJavaThrow{call.vm.Linker().Class(outcome.exception_class).descriptor,
                                          outcome.exception_message, outcome.exception};
                if (!outcome.value.ref.IsValid())
                    throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;",
                                          "resource has no text value: " + std::to_string(id)};
                return outcome.value;
            };
            if (const auto* resource = FindSystemResource(context, *system_resources, id); resource != nullptr) {
                if (const auto* text = std::get_if<std::string>(resource))
                    return dx::VmValue::Ref(call.vm.NewStringUtf8(*text));
                return coerce(0x10U, std::bit_cast<std::uint32_t>(std::get<std::int32_t>(*resource)));
            }
            if (IsSystemResources(context, call.receiver))
                throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;",
                                      "system text resource is unavailable"};
            const loader::ArscEntry* entry = nullptr;
            try {
                entry = &ResolveUiResourceEntry(*context, id);
            } catch (const std::runtime_error& error) {
                throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;", error.what()};
            }
            if (entry->is_complex)
                throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;",
                                      "complex resource has no text value: " + std::to_string(id)};
            if (entry->value_type == 0x03U && entry->string_value.has_value())
                return dx::VmValue::Ref(call.vm.NewStringUtf8(*entry->string_value));
            return coerce(entry->value_type, entry->value_data);
        });
    builder.VirtualMethod("getString", "(I)Ljava/lang/String;", [](dx::IntrinsicContext& call) -> dx::VmValue {
        const auto text =
            CallAndroidMethod(call.vm, call.receiver, "getText", "(I)Ljava/lang/CharSequence;", {call.arguments[0]})
                .ref;
        if (!text.IsValid())
            throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;", "resource has no text value"};
        const auto roots = call.vm.ProtectReferences(std::array{text});
        return CallAndroidMethod(call.vm, text, "toString", "()Ljava/lang/String;");
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
dx::VmObjectRef MakeRomSkin(dx::IntrinsicContext& call, const Context& context, std::uint32_t id) {
    const auto density = static_cast<std::uint32_t>(std::lround(context->ui_density * 160.0F));
    const auto* resource = context->theme_resources.Drawable(id, density);
    if (!resource) throw std::runtime_error("unreviewed ROM drawable load");
    const bool selector = !resource->items.empty();
    const char* descriptor =
        selector ? "Landroid/graphics/drawable/StateListDrawable;" : "Landroid/graphics/drawable/NinePatchDrawable;";
    const auto klass = call.vm.Linker().ResolveDescriptor(descriptor);
    const auto init = call.vm.EnsureClassInitialized(klass);
    if (init.exception.IsValid())
        throw dx::VmJavaThrow{call.vm.Linker().Class(init.exception_class).descriptor, init.exception_message,
                              init.exception};
    const auto object = call.vm.NewIntrinsicInstance(descriptor);
    const auto roots = call.vm.ProtectReferences(std::array{object});
    const auto ctor = call.vm.Linker().FindDirectMethod(klass, "<init>", "()V");
    const auto out = call.vm.Call(*ctor, std::array{dx::VmValue::Ref(object)});
    if (out.exception.IsValid())
        throw dx::VmJavaThrow{call.vm.Linker().Class(out.exception_class).descriptor, out.exception_message,
                              out.exception};
    // Do not retain unordered-map references across recursive allocations.
    context->ui_drawables.at(object.Value()).resource_id = id;
    if (selector) {
        for (const auto& item : resource->items) {
            const auto child = MakeRomSkin(call, context, item.drawable);
            auto& state = context->ui_drawables.at(object.Value());
            state.skin_children.push_back(child);
            state.skin_states.push_back(item.states);
            const auto padding = context->ui_drawables.at(child.Value()).skin_padding;
            for (std::size_t i = 0; i < 4; ++i)
                state.skin_padding[i] = std::max(state.skin_padding[i], padding[i]);
        }
        const auto states = CallAndroidMethod(call.vm, object, "getState", "()[I").ref;
        static_cast<void>(CallAndroidMethod(call.vm, object, "onStateChange", "([I)Z", {dx::VmValue::Ref(states)}));
    } else {
        const auto decoded = DecodeImageToArgb(resource->png);
        if (!decoded || decoded->width != static_cast<int>(resource->width) ||
            decoded->height != static_cast<int>(resource->height))
            throw std::runtime_error("ROM ninepatch PNG decode/facts mismatch");
        auto pixels = std::make_shared<ui::UiBitmap>();
        pixels->width = decoded->width;
        pixels->height = decoded->height;
        for (auto argb : decoded->argb) {
            pixels->rgba8.push_back(static_cast<std::uint8_t>(argb >> 16));
            pixels->rgba8.push_back(static_cast<std::uint8_t>(argb >> 8));
            pixels->rgba8.push_back(static_cast<std::uint8_t>(argb));
            pixels->rgba8.push_back(static_cast<std::uint8_t>(argb >> 24));
        }
        auto& state = context->ui_drawables.at(object.Value());
        const auto scale = [&](std::int32_t value) {
            return resource->density == 0
                       ? value
                       : static_cast<std::int32_t>(
                             (static_cast<std::int64_t>(value) * density + resource->density / 2) / resource->density);
        };
        state.skin_width = scale(static_cast<std::int32_t>(resource->width));
        state.skin_height = scale(static_cast<std::int32_t>(resource->height));
        auto scaled = std::make_shared<ui::UiBitmap>();
        scaled->width = state.skin_width;
        scaled->height = state.skin_height;
        if (scaled->width <= 0 || scaled->height <= 0 || scaled->width > 4096 || scaled->height > 4096)
            throw std::runtime_error("ROM skin scaled pixels exceed bounds");
        scaled->nine_patch = true;
        for (std::size_t i = 0; i < 2; ++i) {
            scaled->stretch_x[i] = scale(resource->stretch_x[i]);
            scaled->stretch_y[i] = scale(resource->stretch_y[i]);
        }
        for (int y = 0; y < scaled->height; ++y) {
            const auto sy = static_cast<std::int64_t>(y) * pixels->height / scaled->height;
            for (int x = 0; x < scaled->width; ++x) {
                const auto sx = static_cast<std::int64_t>(x) * pixels->width / scaled->width;
                const auto at = static_cast<std::size_t>(sy * pixels->width + sx) * 4;
                scaled->rgba8.insert(scaled->rgba8.end(), pixels->rgba8.data() + at, pixels->rgba8.data() + at + 4);
            }
        }
        state.leaf_pixels = std::move(scaled);
        for (std::size_t i = 0; i < 4; ++i) {
            state.skin_padding[i] = scale(resource->padding[i]);
            state.skin_optical[i] = scale(resource->optical[i]);
        }
    }
    return object;
}
}  // namespace ogplay::runtime::android_intrinsics
