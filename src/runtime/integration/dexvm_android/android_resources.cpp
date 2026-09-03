// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_content_res_AssetFileDescriptor.cpp ----
#include <cstdint>
#include <utility>

#include "catalog.h"
#include "ogplay/runtime/dexvm/io_runtime.h"

namespace ogplay::runtime::android_intrinsics {

namespace {

void SetWide(dx::IntrinsicContext& call, const dx::VmObjectRef object,
             const std::size_t slot, const std::int64_t value) {
    const auto bits = static_cast<std::uint64_t>(value);
    auto slots = call.vm.Model().InstanceSlots(object);
    slots[slot] = {static_cast<std::uint32_t>(bits), dx::SlotTag::wide_lo};
    slots[slot + 1] = {static_cast<std::uint32_t>(bits >> 32U),
                       dx::SlotTag::wide_hi};
}

std::int64_t GetWide(dx::IntrinsicContext& call,
                     const dx::VmObjectRef object, const std::size_t slot) {
    const auto slots = call.vm.Model().InstanceSlots(object);
    const auto bits = static_cast<std::uint64_t>(slots[slot].bits) |
                      (static_cast<std::uint64_t>(slots[slot + 1].bits)
                       << 32U);
    return static_cast<std::int64_t>(bits);
}

void Initialize(dx::IntrinsicContext& call, const dx::VmObjectRef descriptor,
                const dx::VmObjectRef pfd, const std::int64_t start_offset,
                const std::int64_t length) {
    if (!pfd.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "fd must not be null"};
    }
    if (length < 0 && start_offset != 0) {
        throw dx::VmJavaThrow{
            "Ljava/lang/IllegalArgumentException;",
            "startOffset must be 0 when using UNKNOWN_LENGTH"};
    }
    auto slots = call.vm.Model().InstanceSlots(descriptor);
    slots[0] = {pfd.Value(), dx::SlotTag::ref};
    SetWide(call, descriptor, 1, start_offset);
    SetWide(call, descriptor, 3, length);
}

}  // namespace

Decl Declare_android_content_res_AssetFileDescriptor(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/res/AssetFileDescriptor;", "Ljava/lang/Object;");
    builder.InstanceField("mFd", "Landroid/os/ParcelFileDescriptor;",
                          dx::kAccPrivate | dx::kAccFinal);
    builder.InstanceField("mStartOffset", "J",
                          dx::kAccPrivate | dx::kAccFinal);
    builder.InstanceField("mLength", "J",
                          dx::kAccPrivate | dx::kAccFinal);
    builder.Constructor("(Landroid/os/ParcelFileDescriptor;JJ)V",
        [](dx::IntrinsicContext& call) {
            Initialize(call, call.receiver, call.arguments[0].ref,
                       call.arguments[1].AsLong(),
                       call.arguments[2].AsLong());
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getFileDescriptor", "()Ljava/io/FileDescriptor;",
        [](dx::IntrinsicContext& call) {
            const auto pfd = dx::VmObjectRef(
                call.vm.Model().InstanceSlots(call.receiver)[0].bits);
            if (!pfd.IsValid()) return dx::VmValue::Ref(dx::VmObjectRef{});
            return dx::VmValue::Ref(dx::VmObjectRef(
                call.vm.Model().InstanceSlots(pfd)[0].bits));
        });
    builder.FinalMethod("getStartOffset", "()J",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Long(GetWide(call, call.receiver, 1));
        });
    builder.FinalMethod("getLength", "()J",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Long(GetWide(call, call.receiver, 3));
        });
    builder.FinalMethod("close", "()V", [](dx::IntrinsicContext& call) {
        const auto pfd = dx::VmObjectRef(
            call.vm.Model().InstanceSlots(call.receiver)[0].bits);
        if (pfd.IsValid()) {
            const auto fd = dx::VmObjectRef(
                call.vm.Model().InstanceSlots(pfd)[0].bits);
            if (fd.IsValid()) call.vm.IO().CloseDescriptor(fd);
        }
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_content_res_AssetManager.cpp ----
#include <set>
#include <string>

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_res_AssetManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/res/AssetManager;", "Ljava/lang/Object;");
    builder.FinalMethod("openFd",
        "(Ljava/lang/String;)Landroid/content/res/AssetFileDescriptor;",
        [context](dx::IntrinsicContext& call) {
            const auto path = "assets/" +
                              call.vm.StringUtf8(call.arguments[0].ref);
            const loader::ApkEntry* entry = nullptr;
            for (const auto& candidate : context->archive.entries) {
                if (candidate.name == path) {
                    entry = &candidate;
                    break;
                }
            }
            if (entry == nullptr) {
                throw dx::VmJavaThrow{
                    "Ljava/io/FileNotFoundException;",
                    "APK asset is unavailable: " + path};
            }
            if (entry->compression_method != 0 ||
                entry->compressed_size != entry->uncompressed_size) {
                throw dx::VmJavaThrow{
                    "Ljava/io/FileNotFoundException;",
                    "APK asset is compressed: " + path};
            }
            std::uint64_t data_offset{};
            try {
                data_offset = loader::StoredApkEntryDataOffset(
                    context->apk_bytes, context->archive, path);
            } catch (const std::exception& error) {
                throw dx::VmJavaThrow{
                    "Ljava/io/FileNotFoundException;",
                    "APK asset cannot provide a descriptor: " + path +
                        " (" + error.what() + ")"};
            }
            const auto fd = call.vm.NewIntrinsicInstance(
                "Ljava/io/FileDescriptor;");
            call.vm.IO().SetDescriptor(
                fd, {dx::IoRuntime::DescriptorKind::apk_entry, path,
                     data_offset, false, {}, {}});
            const auto pfd = call.vm.NewIntrinsicInstance(
                "Landroid/os/ParcelFileDescriptor;");
            call.vm.Model().InstanceSlots(pfd)[0] = {
                fd.Value(), dx::SlotTag::ref};
            const auto descriptor = call.vm.NewIntrinsicInstance(
                "Landroid/content/res/AssetFileDescriptor;");
            const auto length =
                static_cast<std::uint64_t>(entry->uncompressed_size);
            Initialize(
                call, descriptor, pfd, static_cast<std::int64_t>(data_offset),
                static_cast<std::int64_t>(length));
            return dx::VmValue::Ref(descriptor);
        });
    builder.FinalMethod("list",
        "(Ljava/lang/String;)[Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            const auto requested =
                call.vm.StringUtf8(call.arguments[0].ref);
            const auto prefix = requested.empty()
                                    ? std::string{"assets/"}
                                    : "assets/" + requested + "/";
            std::set<std::string> children;
            for (const auto& entry : context->archive.entries) {
                if (!entry.name.starts_with(prefix)) continue;
                const auto remainder =
                    std::string_view(entry.name).substr(prefix.size());
                if (remainder.empty()) continue;
                const auto slash = remainder.find('/');
                children.emplace(remainder.substr(0, slash));
            }
            auto& vm = call.vm;
            const auto array_class =
                vm.Linker().ResolveDescriptor("[Ljava/lang/String;");
            const auto string_class =
                vm.Linker().ResolveDescriptor("Ljava/lang/String;");
            const auto array = vm.Model().NewObjectArray(
                array_class, string_class,
                static_cast<JniSize>(children.size()));
            JniSize index{};
            for (const auto& child : children) {
                vm.Model().SetObjectElement(
                    array, index++, vm.NewStringUtf8(child));
            }
            return dx::VmValue::Ref(array);
        });
    builder.FinalMethod("open", "(Ljava/lang/String;)Ljava/io/InputStream;",
        [context](dx::IntrinsicContext& call) {
            const auto name = call.vm.StringUtf8(call.arguments[0].ref);
            return dx::VmValue::Ref(OpenStream(
                call, context, ReadApkFile(context, "assets/" + name)));
        });
    builder.FinalMethod("open", "(Ljava/lang/String;I)Ljava/io/InputStream;",
        [context](dx::IntrinsicContext& call) {
            const auto name = call.vm.StringUtf8(call.arguments[0].ref);
            return dx::VmValue::Ref(OpenStream(
                call, context, ReadApkFile(context, "assets/" + name)));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_content_res_Configuration.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_res_Configuration(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/res/Configuration;", "Ljava/lang/Object;");
    builder.InstanceField("keyboard", "I");
    builder.InstanceField("screenLayout", "I");
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

std::int32_t Api19ScreenLayout(
    const ogplay::runtime::DexVmAndroidContext& context) {
    if (context.surface_width == 0U || context.surface_height == 0U ||
        !std::isfinite(context.ui_density) || context.ui_density <= 0.0F) {
        throw std::invalid_argument(
            "Configuration requires positive surface metrics and density");
    }
    const auto width_dp = static_cast<std::int32_t>(std::lround(
        static_cast<double>(context.surface_width) / context.ui_density));
    const auto height_dp = static_cast<std::int32_t>(std::lround(
        static_cast<double>(context.surface_height) / context.ui_density));
    const auto long_dp = std::max(width_dp, height_dp);
    const auto short_dp = std::min(width_dp, height_dp);

    std::int32_t size{};
    bool is_long{};
    bool compat_needed{};
    if (long_dp < 470) {
        size = kScreenLayoutSizeSmall;
    } else {
        size = long_dp >= 960 && short_dp >= 720
                   ? kScreenLayoutSizeXlarge
                   : long_dp >= 640 && short_dp >= 480
                         ? kScreenLayoutSizeLarge
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
    return layout;
}

void SetIntField(ogplay::runtime::dexvm::IntrinsicContext& call,
                 const ogplay::runtime::dexvm::VmObjectRef object,
                 const char* name, const std::int32_t value) {
    const auto field = call.vm.Linker().FindFieldRecursive(
        call.vm.Model().ObjectClass(object), name, "I");
    if (!field.has_value()) {
        throw std::logic_error(std::string("integer field missing: ") + name);
    }
    const auto slot = call.vm.Linker().Field(*field).slot;
    call.vm.Model().InstanceSlots(object)[slot] = {
        static_cast<std::uint32_t>(value),
        ogplay::runtime::dexvm::SlotTag::cat1};
}

std::size_t FieldSlot(ogplay::runtime::dexvm::IntrinsicContext& call,
                      const ogplay::runtime::dexvm::VmObjectRef object,
                      const char* name, const char* descriptor) {
    const auto field = call.vm.Linker().FindFieldRecursive(
        call.vm.Model().ObjectClass(object), name, descriptor);
    if (!field.has_value()) {
        throw std::logic_error(std::string("XML parser field missing: ") + name);
    }
    return call.vm.Linker().Field(*field).slot;
}

void SetRefField(ogplay::runtime::dexvm::IntrinsicContext& call,
                 const ogplay::runtime::dexvm::VmObjectRef object,
                 const char* name, const char* descriptor,
                 const ogplay::runtime::dexvm::VmObjectRef value) {
    call.vm.Model().InstanceSlots(object)[
        FieldSlot(call, object, name, descriptor)] = {
        value.Value(), ogplay::runtime::dexvm::SlotTag::ref};
}

ogplay::runtime::dexvm::VmObjectRef GetRefField(
    ogplay::runtime::dexvm::IntrinsicContext& call,
    const ogplay::runtime::dexvm::VmObjectRef object, const char* name,
    const char* descriptor) {
    return ogplay::runtime::dexvm::VmObjectRef(
        call.vm.Model().InstanceSlots(object)[
            FieldSlot(call, object, name, descriptor)]
            .bits);
}

std::int32_t GetIntField(ogplay::runtime::dexvm::IntrinsicContext& call,
                         const ogplay::runtime::dexvm::VmObjectRef object,
                         const char* name) {
    return static_cast<std::int32_t>(
        call.vm.Model().InstanceSlots(object)[FieldSlot(call, object, name, "I")]
            .bits);
}

void RequireOpenXmlParser(
    ogplay::runtime::dexvm::IntrinsicContext& call) {
    if (GetIntField(call, call.receiver, "mClosed") != 0) {
        throw ogplay::runtime::dexvm::VmJavaThrow{
            "Ljava/lang/IllegalStateException;", "XML parser is closed"};
    }
}

ogplay::runtime::dexvm::VmObjectRef MakeXmlParser(
    ogplay::runtime::dexvm::IntrinsicContext& call,
    const std::vector<ogplay::loader::BinaryXmlPullEvent>& events) {
    namespace dx = ogplay::runtime::dexvm;
    const auto parser = call.vm.NewIntrinsicInstance(
        "Landroid/content/res/XmlResourceParser$Impl;");
    const std::array parser_references{parser};
    [[maybe_unused]] const auto parser_roots =
        call.vm.ProtectReferences(parser_references);
    auto& model = call.vm.Model();
    const auto count =
        static_cast<ogplay::runtime::JniSize>(events.size());
    const auto types = model.NewPrimitiveArray(
        call.vm.Linker().ResolveDescriptor("[I"),
        ogplay::runtime::JniPrimitiveKind::integer, count);
    SetRefField(call, parser, "mEventTypes", "[I", types);
    const auto names = model.NewObjectArray(
        call.vm.Linker().ResolveDescriptor("[Ljava/lang/String;"),
        call.vm.Linker().ResolveDescriptor("Ljava/lang/String;"), count);
    SetRefField(call, parser, "mNames", "[Ljava/lang/String;", names);
    const auto texts = model.NewObjectArray(
        call.vm.Linker().ResolveDescriptor("[Ljava/lang/String;"),
        call.vm.Linker().ResolveDescriptor("Ljava/lang/String;"), count);
    SetRefField(call, parser, "mTexts", "[Ljava/lang/String;", texts);
    for (std::size_t index = 0; index < events.size(); ++index) {
        const auto array_index =
            static_cast<ogplay::runtime::JniSize>(index);
        model.SetPrimitiveElement(
            types, array_index,
            static_cast<std::uint32_t>(events[index].type));
        if (!events[index].name.empty()) {
            model.SetObjectElement(
                names, array_index,
                call.vm.NewStringUtf8(events[index].name));
        }
        if (events[index].type ==
            ogplay::loader::BinaryXmlPullEventType::text) {
            model.SetObjectElement(
                texts, array_index,
                call.vm.NewStringUtf8(events[index].text));
        }
    }
    SetIntField(call, parser, "mIndex", 0);
    SetIntField(call, parser, "mClosed", 0);
    return parser;
}

}  // namespace

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_res_Resources_NotFoundException(
    const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/res/Resources$NotFoundException;",
        "Ljava/lang/RuntimeException;");
    builder.Constructor("()V",
                        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
                        [](dx::IntrinsicContext& call) {
                            call.vm.SetThrowableMessage(call.receiver,
                                                       call.arguments[0].ref);
                            return dx::VmValue::Void();
                        });
    return std::move(builder).Build();
}

Decl Declare_android_content_res_XmlResourceParser(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface(
        "Landroid/content/res/XmlResourceParser;",
        {"Lorg/xmlpull/v1/XmlPullParser;", "Landroid/util/AttributeSet;",
         "Ljava/lang/AutoCloseable;"});
    builder.UnimplementedVirtual("close", "()V",
                                 dx::kAccPublic | dx::kAccAbstract);
    return std::move(builder).Build();
}

Decl Declare_android_content_res_XmlResourceParser_Impl(
    const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/res/XmlResourceParser$Impl;", "Ljava/lang/Object;",
        {"Landroid/content/res/XmlResourceParser;"});
    builder.InstanceField("mEventTypes", "[I",
                          dx::kAccPrivate | dx::kAccFinal)
        .InstanceField("mNames", "[Ljava/lang/String;",
                       dx::kAccPrivate | dx::kAccFinal)
        .InstanceField("mTexts", "[Ljava/lang/String;",
                       dx::kAccPrivate | dx::kAccFinal)
        .InstanceField("mIndex", "I", dx::kAccPrivate)
        .InstanceField("mClosed", "I", dx::kAccPrivate);
    // 返回解析器当前所在的 XML 事件类型。
    builder.FinalMethod("getEventType", "()I", [](dx::IntrinsicContext& call) {
        RequireOpenXmlParser(call);
        const auto types = GetRefField(call, call.receiver, "mEventTypes", "[I");
        return dx::VmValue::Int(static_cast<std::int32_t>(
            call.vm.Model().GetPrimitiveElement(
                types, GetIntField(call, call.receiver, "mIndex"))));
    });
    // 推进到下一个 XML 事件并返回其类型。
    builder.FinalMethod("next", "()I", [](dx::IntrinsicContext& call) {
        RequireOpenXmlParser(call);
        const auto types = GetRefField(call, call.receiver, "mEventTypes", "[I");
        auto index = GetIntField(call, call.receiver, "mIndex");
        const auto last = call.vm.Model().ArrayLength(types) - 1;
        if (index < last) ++index;
        SetIntField(call, call.receiver, "mIndex", index);
        return dx::VmValue::Int(static_cast<std::int32_t>(
            call.vm.Model().GetPrimitiveElement(types, index)));
    });
    const auto current_string = [](const char* field) {
        return [field](dx::IntrinsicContext& call) {
            RequireOpenXmlParser(call);
            const auto values = GetRefField(
                call, call.receiver, field, "[Ljava/lang/String;");
            return dx::VmValue::Ref(call.vm.Model().GetObjectElement(
                values, GetIntField(call, call.receiver, "mIndex")));
        };
    };
    // 返回当前开始或结束标签的名称。
    builder.FinalMethod("getName", "()Ljava/lang/String;",
                        current_string("mNames"));
    // 返回当前文本事件的内容。
    builder.FinalMethod("getText", "()Ljava/lang/String;",
                        current_string("mTexts"));
    // 关闭解析器并释放其事件数组，重复关闭保持幂等。
    builder.FinalMethod("close", "()V", [](dx::IntrinsicContext& call) {
        SetRefField(call, call.receiver, "mEventTypes", "[I",
                    dx::VmObjectRef{});
        SetRefField(call, call.receiver, "mNames", "[Ljava/lang/String;",
                    dx::VmObjectRef{});
        SetRefField(call, call.receiver, "mTexts", "[Ljava/lang/String;",
                    dx::VmObjectRef{});
        SetIntField(call, call.receiver, "mClosed", 1);
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_res_Resources(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/res/Resources;", "Ljava/lang/Object;");
    builder.FinalMethod("getConfiguration", "()Landroid/content/res/Configuration;",
        [context](dx::IntrinsicContext& call) {
            const auto instance = Singleton(call, context, "configuration",
                "Landroid/content/res/Configuration;");
            // keyboard = KEYBOARD_NOKEYS (1): desktop host has no guest
            // keypad.
            SetIntField(call, instance, "keyboard", 1);
            SetIntField(call, instance, "screenLayout",
                        Api19ScreenLayout(*context));
            return dx::VmValue::Ref(instance);
        });
    builder.FinalMethod("getIdentifier",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
        [context](dx::IntrinsicContext& call) {
            const auto entry_name = call.vm.StringUtf8(call.arguments[0].ref);
            const auto type_name = call.vm.StringUtf8(call.arguments[1].ref);
            const auto* entry = context->arsc.FindByName(type_name, entry_name);
            return dx::VmValue::Int(
                entry == nullptr
                    ? 0
                    : static_cast<std::int32_t>(entry->resource_id));
        });
    builder.FinalMethod("openRawResource", "(I)Ljava/io/InputStream;",
        [context](dx::IntrinsicContext& call) {
            const auto resource_id =
                static_cast<std::uint32_t>(call.arguments[0].AsInt());
            const auto* entry = context->arsc.FindById(resource_id);
            if (entry == nullptr || !entry->string_value.has_value()) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                    "resource id has no file entry: " +
                        std::to_string(resource_id)};
            }
            return dx::VmValue::Ref(OpenStream(
                call, context, ReadApkFile(context, *entry->string_value)));
        });
    // 按资源 ID 打开编译后的 XML pull parser。
    builder.VirtualMethod(
        "getXml", "(I)Landroid/content/res/XmlResourceParser;",
        [context](dx::IntrinsicContext& call) {
            const auto resource_id =
                static_cast<std::uint32_t>(call.arguments[0].AsInt());
            const auto* entry = context->arsc.FindById(resource_id);
            if (entry == nullptr || !entry->string_value.has_value()) {
                throw dx::VmJavaThrow{
                    "Landroid/content/res/Resources$NotFoundException;",
                    "resource id has no XML file entry: " +
                        std::to_string(resource_id)};
            }
            std::vector<std::byte> bytes;
            try {
                bytes = ReadApkFile(context, *entry->string_value);
            } catch (const dx::VmJavaThrow& error) {
                throw dx::VmJavaThrow{
                    "Landroid/content/res/Resources$NotFoundException;",
                    error.message};
            }
            std::vector<loader::BinaryXmlPullEvent> events;
            try {
                events = loader::ParseBinaryXmlPullEvents(bytes);
            } catch (const std::exception& error) {
                throw dx::VmJavaThrow{
                    "Landroid/content/res/Resources$NotFoundException;",
                    "resource XML is invalid: " + std::string(error.what())};
            }
            return dx::VmValue::Ref(MakeXmlParser(call, events));
        });
    builder.FinalMethod("getString", "(I)Ljava/lang/String;",
        [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "string resources are not provided yet"};
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
