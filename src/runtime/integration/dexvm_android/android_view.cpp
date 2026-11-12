// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_view_Display.cpp ----
#include "catalog.h"
#include "rom_input.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"

#include <bit>
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>

namespace ogplay::runtime::android_intrinsics {

namespace {}  // namespace

Decl Declare_android_view_ContextThemeWrapper_Shell(const Context&) {
    return std::move(dx::IntrinsicClassBuilder::Class("Landroid/view/ContextThemeWrapper;",
                                                      "Landroid/content/ContextWrapper;")
                         .MergesBootClass())
        .Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
namespace {

void WriteMetric(dx::IntrinsicContext& call, const dx::VmObjectRef metrics, const std::string& name,
                 const std::uint32_t bits, const std::string& descriptor) {
    const auto field = call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(metrics), name, descriptor);
    if (!field.has_value()) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "DisplayMetrics field is missing: " + name);
    }
    call.vm.Model().InstanceSlots(metrics)[call.vm.Linker().Field(*field).slot] = {bits, dx::SlotTag::cat1};
}

}  // namespace

[[nodiscard]] std::int32_t DeviceDensityDpi(const Context& context) {
    const double dpi = static_cast<double>(context->ui_density) * 160.0;
    if (!std::isfinite(dpi) || dpi < 1.0 || dpi > std::numeric_limits<std::int32_t>::max())
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "display density is invalid");
    return static_cast<std::int32_t>(std::lround(dpi));
}

void PopulateAndroidDisplayMetrics(dx::IntrinsicContext& call, const Context& context, const dx::VmObjectRef metrics) {
    if (!metrics.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "Display metrics output is null"};
    if (!std::isfinite(context->ui_scaled_density) || context->ui_scaled_density <= 0.0F)
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "display scaled density is invalid");
    const auto density_dpi = DeviceDensityDpi(context);
    const auto density_bits = std::bit_cast<std::uint32_t>(context->ui_density);
    const auto scaled_bits = std::bit_cast<std::uint32_t>(context->ui_scaled_density);
    const auto dpi_bits = std::bit_cast<std::uint32_t>(static_cast<float>(density_dpi));
    const auto write_int = [&](const std::string& name, const std::int32_t value) {
        WriteMetric(call, metrics, name, static_cast<std::uint32_t>(value), "I");
    };
    const auto write_float = [&](const std::string& name, const std::uint32_t bits) {
        WriteMetric(call, metrics, name, bits, "F");
    };
    write_int("widthPixels", static_cast<std::int32_t>(context->surface_width));
    write_int("heightPixels", static_cast<std::int32_t>(context->surface_height));
    write_float("density", density_bits);
    write_int("densityDpi", density_dpi);
    write_float("scaledDensity", scaled_bits);
    write_float("xdpi", dpi_bits);
    write_float("ydpi", dpi_bits);
    write_int("noncompatWidthPixels", static_cast<std::int32_t>(context->surface_width));
    write_int("noncompatHeightPixels", static_cast<std::int32_t>(context->surface_height));
    write_float("noncompatDensity", density_bits);
    write_int("noncompatDensityDpi", density_dpi);
    write_float("noncompatScaledDensity", scaled_bits);
    write_float("noncompatXdpi", dpi_bits);
    write_float("noncompatYdpi", dpi_bits);
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_KeyEvent.cpp ----
#include "catalog.h"
#include "rom_input.h"

namespace ogplay::runtime::android_intrinsics {

namespace {

struct VirtualKey {
    std::int32_t code, label, number;
};

struct VirtualKeyBehavior {
    std::int32_t code, meta, character, fallback;
};

#include "virtual_keymap.inc"

// API19 KeyCharacterMap.cpp::matchesMetaState. Native lookup keeps the
// original modifier-pair matching; normalization remains BootDex Java.
bool MatchesVirtualMeta(std::int32_t event, std::int32_t behavior) {
    if ((event & behavior) != behavior) return false;
    constexpr std::array<std::array<std::int32_t, 3>, 3> pairs{
        {{{0x1000, 0x2000, 0x4000}}, {{2, 0x10, 0x20}}, {{0x10000, 0x20000, 0x40000}}}};
    auto unmatched = event & ~behavior & 0x77032;
    for (const auto& pair : pairs) {
        if ((behavior & pair[0]) != 0)
            unmatched &= ~(pair[1] | pair[2]);
        else if ((behavior & (pair[1] | pair[2])) != 0)
            unmatched &= ~pair[0];
    }
    return unmatched == 0;
}

const VirtualKeyBehavior* VirtualBehavior(std::int32_t code, std::int32_t meta) {
    const auto found = std::find_if(kVirtualKeyBehaviors.begin(), kVirtualKeyBehaviors.end(), [=](const auto& row) {
        return row.code == code && MatchesVirtualMeta(meta, row.meta);
    });
    return found == kVirtualKeyBehaviors.end() ? nullptr : &*found;
}

dx::VmValue RequireKeyMapOutcome(dx::Interpreter& vm, const dx::VmCallOutcome& result) {
    if (result.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                              result.exception};
    return result.value;
}

dx::VmObjectRef NewKeyCharacterMap(dx::Interpreter& vm, bool keyboard);

dx::VmObjectRef NewKeyCharacterMap(dx::Interpreter& vm, bool keyboard) {
    const auto type = vm.Linker().ResolveDescriptor("Landroid/view/KeyCharacterMap;");
    RequireKeyMapOutcome(vm, vm.EnsureClassInitialized(type));
    const auto object = vm.NewIntrinsicInstance("Landroid/view/KeyCharacterMap;");
    const auto root = vm.ProtectReferences(std::array{object});
    const auto ctor = vm.Linker().FindDirectMethod(type, "<init>", "(I)V");
    if (!ctor) throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "KeyCharacterMap constructor is unavailable"};
    RequireKeyMapOutcome(vm, vm.Call(*ctor, std::array{dx::VmValue::Ref(object), dx::VmValue::Int(keyboard ? 1 : 2)}));
    return object;
}

}  // namespace

Decl Declare_android_view_KeyEvent_Natives(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/KeyEvent;");
    builder.MergesBootClass();
    {
        builder.DirectMethod(
            "native_isSystemKey", "(I)Z",
            [](dx::IntrinsicContext& call) {
                constexpr std::array keys{82, 2,  3,  4,  5,  6,   24, 25, 164, 91, 26,  79,  126, 127,
                                          85, 86, 87, 88, 89, 130, 90, 27, 80,  84, 220, 221, 222};
                return dx::VmValue::Int(std::ranges::find(keys, call.arguments[0].AsInt()) != keys.end());
            },
            dx::kAccPrivate | dx::kAccNative);
        builder.DirectMethod(
            "native_hasDefaultAction", "(I)Z",
            [](dx::IntrinsicContext& call) {
                constexpr std::array keys{3,   4,   5,  6,  24, 25, 164, 26,  27, 79, 82,  83,  80, 84,
                                          126, 127, 85, 86, 87, 88, 89,  130, 90, 91, 220, 221, 222};
                return dx::VmValue::Int(std::ranges::find(keys, call.arguments[0].AsInt()) != keys.end());
            },
            dx::kAccPrivate | dx::kAccNative);
    }
    return std::move(builder).Build();
}

Decl Declare_android_view_KeyCharacterMap_Natives(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/KeyCharacterMap;", "Ljava/lang/Object;");
    builder.MergesBootClass();
    const auto full_map = [](dx::IntrinsicContext& call) {
        const auto selector = call.arguments[0].AsInt();
        if (selector != 1 && selector != 2)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Invalid or disposed key character map"};
        return selector == 1;
    };
    builder.StaticMethod(
        "nativeGetCharacter", "(III)C",
        [full_map](dx::IntrinsicContext& call) {
            const auto full = full_map(call);
            const auto* row = full ? VirtualBehavior(call.arguments[1].AsInt(), call.arguments[2].AsInt()) : nullptr;
            return dx::VmValue::Int(row ? row->character : 0);
        },
        dx::kAccPrivate | dx::kAccNative);
    const auto key_property = [full_map](bool number) {
        return [full_map, number](dx::IntrinsicContext& call) {
            const bool full = full_map(call);
            const auto code = call.arguments[1].AsInt();
            const auto found = std::find_if(kVirtualKeys.begin(), kVirtualKeys.end(),
                                            [code](const auto& row) { return row.code == code; });
            return dx::VmValue::Int(!full || found == kVirtualKeys.end() ? 0 : number ? found->number : found->label);
        };
    };
    builder.StaticMethod("nativeGetNumber", "(II)C", key_property(true), dx::kAccPrivate | dx::kAccNative);
    builder.StaticMethod("nativeGetDisplayLabel", "(II)C", key_property(false), dx::kAccPrivate | dx::kAccNative);
    builder.StaticMethod(
        "nativeGetKeyboardType", "(I)I",
        [full_map](dx::IntrinsicContext& call) { return dx::VmValue::Int(full_map(call) ? 4 : 5); },
        dx::kAccPrivate | dx::kAccNative);
    builder.StaticMethod(
        "nativeGetMatch", "(II[CI)C",
        [full_map](dx::IntrinsicContext& call) {
            const bool full = full_map(call);
            const auto chars = call.arguments[2].ref;
            if (!chars.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "characters must not be null"};
            std::int32_t result = 0;
            if (!full) return dx::VmValue::Int(result);
            for (const auto& row : kVirtualKeyBehaviors) {
                if (row.code != call.arguments[1].AsInt() || row.character == 0) continue;
                for (JniSize i = 0; i < call.vm.Model().ArrayLength(chars); ++i) {
                    if (call.vm.Model().GetPrimitiveElement(chars, i) != static_cast<std::uint32_t>(row.character))
                        continue;
                    result = row.character;
                    if ((row.meta & call.arguments[3].AsInt()) == row.meta) return dx::VmValue::Int(result);
                    break;
                }
            }
            return dx::VmValue::Int(result);
        },
        dx::kAccPrivate | dx::kAccNative);
    builder.StaticMethod(
        "nativeGetFallbackAction", "(IIILandroid/view/KeyCharacterMap$FallbackAction;)Z",
        [full_map](dx::IntrinsicContext& call) {
            const bool full = full_map(call);
            const auto* row = full ? VirtualBehavior(call.arguments[1].AsInt(), call.arguments[2].AsInt()) : nullptr;
            const auto object = call.arguments[3].ref;
            if (!object.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "fallback action is null"};
            for (const auto& [name, value] : std::array<std::pair<const char*, std::int32_t>, 2>{
                     {{"keyCode", row ? row->fallback : 0},
                      {"metaState", row && row->fallback ? call.arguments[2].AsInt() & ~row->meta : 0}}}) {
                const auto field = call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(object), name, "I");
                if (!field)
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "FallbackAction field is missing"};
                call.vm.Model().InstanceSlots(object)[call.vm.Linker().Field(*field).slot] = {
                    static_cast<std::uint32_t>(value), dx::SlotTag::cat1};
            }
            return dx::VmValue::Int(row && row->fallback ? 1 : 0);
        },
        dx::kAccPrivate | dx::kAccNative);
    builder.StaticMethod(
        "nativeDispose", "(I)V",
        [full_map](dx::IntrinsicContext& call) {
            // Immutable selectors own no allocation; finalization only clears Java mPtr.
            static_cast<void>(full_map(call));
            return dx::VmValue::Void();
        },
        dx::kAccPrivate | dx::kAccNative);
    const auto unsupported = [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.input_device_services", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "KeyCharacterMap service, Unicode category or normalization capability is unsupported"};
    };
    builder.StaticMethod("nativeReadFromParcel", "(Landroid/os/Parcel;)I", unsupported,
                         dx::kAccPrivate | dx::kAccNative);
    builder.StaticMethod("nativeWriteToParcel", "(ILandroid/os/Parcel;)V", unsupported,
                         dx::kAccPrivate | dx::kAccNative);
    builder.StaticMethod("nativeGetEvents", "(I[C)[Landroid/view/KeyEvent;", unsupported,
                         dx::kAccPrivate | dx::kAccNative);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {

// InputDevice value semantics stay in BootDex. Only process service queries
// cross this boundary; no InputManager/Binder singleton is initialized.
dx::VmValue MakeInputDevice(dx::IntrinsicContext& call, const Context& context) {
    const auto id = call.arguments[0].AsInt();
    const auto found = std::find_if(context->input_devices.begin(), context->input_devices.end(),
                                    [id](const auto& device) { return device.id == id; });
    if (found == context->input_devices.end()) return dx::VmValue::Ref(dx::VmObjectRef{});
    const auto type = call.vm.Linker().ResolveDescriptor("Landroid/view/InputDevice;");
    const auto check = [&](const dx::VmCallOutcome& outcome) {
        if (outcome.exception.IsValid())
            throw dx::VmJavaThrow{call.vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                                  outcome.exception};
    };
    check(call.vm.EnsureClassInitialized(type));
    const auto object = call.vm.NewIntrinsicInstance("Landroid/view/InputDevice;");
    const auto root = call.vm.ProtectReferences(std::array{object});
    const auto name = call.vm.NewStringUtf8(found->name);
    const auto name_root = call.vm.ProtectReferences(std::array{name});
    const auto descriptor = call.vm.NewStringUtf8(found->descriptor);
    const auto descriptor_root = call.vm.ProtectReferences(std::array{descriptor});
    const auto key_map =
        NewKeyCharacterMap(call.vm, (found->sources & kAndroidKeyboardSource) == kAndroidKeyboardSource);
    const auto key_map_root = call.vm.ProtectReferences(std::array{key_map});
    const auto constructor = call.vm.Linker().FindDirectMethod(
        type, "<init>", "(IIILjava/lang/String;IILjava/lang/String;ZIILandroid/view/KeyCharacterMap;ZZ)V");
    const auto add_range = call.vm.Linker().FindDirectMethod(type, "addMotionRange", "(IIFFFFF)V");
    if (!constructor || !add_range)
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "API19 InputDevice constructors are unavailable"};
    check(call.vm.Call(
        *constructor, std::vector<dx::VmValue>{dx::VmValue::Ref(object), dx::VmValue::Int(id), dx::VmValue::Int(0),
                                               dx::VmValue::Int(0), dx::VmValue::Ref(name), dx::VmValue::Int(0),
                                               dx::VmValue::Int(0), dx::VmValue::Ref(descriptor), dx::VmValue::Int(0),
                                               dx::VmValue::Int(found->sources), dx::VmValue::Int(found->keyboard_type),
                                               dx::VmValue::Ref(key_map), dx::VmValue::Int(0), dx::VmValue::Int(0)}));
    for (const auto& range : found->ranges)
        check(call.vm.Call(*add_range, std::vector<dx::VmValue>{
                                           dx::VmValue::Ref(object), dx::VmValue::Int(range.axis),
                                           dx::VmValue::Int(range.source), dx::VmValue::Float(range.minimum),
                                           dx::VmValue::Float(range.maximum), dx::VmValue::Float(range.flat),
                                           dx::VmValue::Float(range.fuzz), dx::VmValue::Float(range.resolution)}));
    return dx::VmValue::Ref(object);
}

Decl Declare_android_view_MotionEvent_Natives(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/MotionEvent;");
    builder.MergesBootClass();
    AddRomMotionNatives(builder, context);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_ViewTreeObserver_OnGlobalLayoutListener_Shell(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;");
    builder.MergesBootClass();
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_ViewTreeObserver.cpp ----
#include "catalog.h"
#include "rom_input.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_ViewTreeObserver_Shell(const Context&) {
    return std::move(dx::IntrinsicClassBuilder::Class("Landroid/view/ViewTreeObserver;", "Ljava/lang/Object;")
                         .MergesBootClass())
        .Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_InputQueue_Natives(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/InputQueue;");
    builder.MergesBootClass();
    AddRomInputQueueNatives(builder, context);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_animation_PropertyValuesHolder_Natives(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/animation/PropertyValuesHolder;");
    builder.MergesBootClass();
    for (const bool floating : {false, true}) {
        const std::string signature = floating ? "(F)V" : "(I)V";
        builder.StaticMethod(
            floating ? "nGetFloatMethod" : "nGetIntMethod", "(Ljava/lang/Class;Ljava/lang/String;)I",
            [context, signature](dx::IntrinsicContext& c) {
                dx::IntrinsicCall args(c);
                const auto type = c.vm.Model().ClassOfClassObject(args.NonNullRef(0, "targetClass"));
                const auto name = c.vm.StringUtf8(args.NonNullRef(1, "methodName"));
                const auto slot = c.vm.Linker().FindVtableIndex(type, name, signature);
                std::optional<dx::VmMethodId> method;
                if (slot)
                    method = c.vm.Linker().Class(type).vtable[*slot];
                else
                    for (auto owner = std::optional{type}; owner; owner = c.vm.Linker().Class(*owner).super) {
                        const auto direct = c.vm.Linker().FindDirectMethod(*owner, name, signature);
                        if (direct && !(c.vm.Linker().Method(*direct).access_flags & dx::kAccStatic)) {
                            method = direct;
                            break;
                        }
                    }
                if (!method || name == "<init>" || name == "<clinit>")
                    throw dx::VmJavaThrow{"Ljava/lang/NoSuchMethodError;", name + signature};
                auto& setters = context->property_setters;
                for (std::size_t i = 0; i < setters.size(); ++i)
                    if (setters[i].target_class == type && setters[i].name == name && setters[i].signature == signature)
                        return dx::VmValue::Int(static_cast<int>(i + 1));
                if (setters.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max()))
                    throw dx::VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "Property setter IDs exhausted"};
                setters.push_back({type, *method, name, signature});
                return dx::VmValue::Int(static_cast<int>(setters.size()));
            },
            dx::kAccPrivate | dx::kAccNative);
        builder.StaticMethod(
            floating ? "nCallFloatMethod" : "nCallIntMethod",
            floating ? "(Ljava/lang/Object;IF)V" : "(Ljava/lang/Object;II)V",
            [context, signature](dx::IntrinsicContext& c) {
                const auto target = dx::IntrinsicCall(c).NonNullRef(0, "target");
                const auto id = c.arguments[1].AsInt();
                if (id <= 0 || static_cast<std::size_t>(id) > context->property_setters.size())
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Invalid property setter ID"};
                const auto setter = context->property_setters[static_cast<std::size_t>(id - 1)];
                if (setter.signature != signature ||
                    !c.vm.Linker().IsAssignable(setter.target_class, c.vm.Model().ObjectClass(target)))
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                          "Property setter target/type mismatch"};
                const auto slot =
                    c.vm.Linker().FindVtableIndex(c.vm.Model().ObjectClass(target), setter.name, setter.signature);
                const auto method = slot && !(c.vm.Linker().Method(setter.method).access_flags & dx::kAccPrivate)
                                        ? c.vm.Linker().Class(c.vm.Model().ObjectClass(target)).vtable[*slot]
                                        : setter.method;
                const auto result = c.vm.Call(method, std::array{dx::VmValue::Ref(target), c.arguments[2]});
                if (result.exception.IsValid())
                    throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor,
                                          result.exception_message, result.exception};
                return result.value;
            },
            dx::kAccPrivate | dx::kAccNative);
    }
    return std::move(builder).Build();
}

Decl Declare_android_view_DisplayEventReceiver_Natives(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/DisplayEventReceiver;", "Ljava/lang/Object;");
    builder.MergesBootClass();
    builder.StaticMethod(
        "nativeInit", "(Landroid/view/DisplayEventReceiver;Landroid/os/MessageQueue;)I",
        [context](dx::IntrinsicContext& call) {
            const auto receiver = call.arguments[0].ref, queue = call.arguments[1].ref;
            static_cast<void>(RegisterCurrentRomLooper(call.vm, context));
            std::scoped_lock lock(context->scheduler_mutex);
            const auto looper = std::find_if(context->loopers.begin(), context->loopers.end(), [&](const auto& item) {
                return queue.IsValid() && item.second.queue == queue && !item.second.quitting;
            });
            if (!receiver.IsValid() || context->scheduler_shutdown || context->process_stopping ||
                looper == context->loopers.end())
                throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;", "MessageQueue is not initialized."};
            if (looper->second.context_token != call.vm.CurrentContextToken()) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.choreographer_frame_source", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "DisplayEventReceiver requires its own Looper thread"};
            }
            if (context->next_display_receiver_token == 0 || context->next_display_receiver_token > 0x7fffffffU)
                throw dx::VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "DisplayEventReceiver token space exhausted"};
            const auto token = context->next_display_receiver_token++;
            context->display_receivers.emplace(token, DexVmAndroidContext::DisplayReceiverState{
                                                          receiver, dx::VmObjectRef{looper->first}, queue, false});
            return dx::VmValue::Int(static_cast<std::int32_t>(token));
        },
        dx::kAccPrivate | dx::kAccStatic | dx::kAccNative);
    builder.StaticMethod(
        "nativeDispose", "(I)V",
        [context](dx::IntrinsicContext& call) {
            std::scoped_lock lock(context->scheduler_mutex);
            context->display_receivers.erase(static_cast<std::uint32_t>(call.arguments[0].AsInt()));
            return dx::VmValue::Void();
        },
        dx::kAccPrivate | dx::kAccStatic | dx::kAccNative);
    builder.StaticMethod(
        "nativeScheduleVsync", "(I)V",
        [context](dx::IntrinsicContext& call) {
            std::scoped_lock lock(context->scheduler_mutex);
            const auto found = context->display_receivers.find(static_cast<std::uint32_t>(call.arguments[0].AsInt()));
            if (context->process_stopping || context->scheduler_shutdown || found == context->display_receivers.end())
                throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                                      "Failed to schedule next vertical sync pulse: invalid receiver"};
            found->second.requested = true;
            return dx::VmValue::Void();
        },
        dx::kAccPrivate | dx::kAccStatic | dx::kAccNative);
    return std::move(builder).Build();
}

Decl Declare_android_view_HardwareRenderer_Natives() {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/HardwareRenderer;", "Ljava/lang/Object;");
    builder.MergesBootClass();
    // ActivityThread configures a shader cache during binding. The software
    // renderer has no hardware shader cache to configure.
    builder.StaticMethod(
        "nSetupShadersDiskCache", "(Ljava/lang/String;)V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); },
        dx::kAccPrivate | dx::kAccStatic | dx::kAccNative);
    return std::move(builder).Build();
}

Decl Declare_local_DisplayService_Class(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalDisplayService;",
                                                    "Landroid/hardware/display/IDisplayManager$Stub;");
    builder.VirtualMethod("getDisplayIds", "()[I", [](dx::IntrinsicContext& call) {
        const auto ids =
            call.vm.Model().NewPrimitiveArray(call.vm.Linker().ResolveDescriptor("[I"), JniPrimitiveKind::integer, 1);
        call.vm.Model().SetPrimitiveElement(ids, 0, 0);
        return dx::VmValue::Ref(ids);
    });
    // The one logical display has no hotplug events. ROM registers the callback
    // even for clients that only query its metrics.
    builder.VirtualMethod("registerCallback", "(Landroid/hardware/display/IDisplayManagerCallback;)V",
                          [context](dx::IntrinsicContext& call) {
                              context->singletons["display_callback"] =
                                  dx::IntrinsicCall(call).NonNullRef(0, "display callback");
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod("getDisplayInfo", "(I)Landroid/view/DisplayInfo;", [context](dx::IntrinsicContext& call) {
        if (call.arguments[0].AsInt() != 0) return dx::VmValue::Ref(dx::VmObjectRef{});
        if (!std::isfinite(context->display_refresh_rate_hz) || context->display_refresh_rate_hz <= 0) {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.choreographer_frame_source", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "Logical display refresh rate is unavailable"};
        }
        const auto info = NewPlatformObject(call.vm, "Landroid/view/DisplayInfo;", "()V", {});
        const auto write = [&](const char* name, const char* type, std::uint32_t bits) {
            const auto field = call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(info), name, type);
            if (!field) throw std::logic_error("DisplayInfo field is unavailable");
            call.vm.Model().InstanceSlots(info)[call.vm.Linker().Field(*field).slot] = {bits, dx::SlotTag::cat1};
        };
        write("refreshRate", "F", std::bit_cast<std::uint32_t>(context->display_refresh_rate_hz));
        write("logicalWidth", "I", context->surface_width);
        write("logicalHeight", "I", context->surface_height);
        write("appWidth", "I", context->surface_width);
        write("appHeight", "I", context->surface_height);
        const auto density = DeviceDensityDpi(context);
        write("logicalDensityDpi", "I", static_cast<std::uint32_t>(density));
        write("physicalXDpi", "F", std::bit_cast<std::uint32_t>(static_cast<float>(density)));
        write("physicalYDpi", "F", std::bit_cast<std::uint32_t>(static_cast<float>(density)));
        return dx::VmValue::Ref(info);
    });
    constexpr auto owner = "android.hardware.display.IDisplayManager";
    for (const auto* method : {"startWifiDisplayScan", "stopWifiDisplayScan", "disconnectWifiDisplay",
                               "pauseWifiDisplay", "resumeWifiDisplay"})
        RejectUnsupportedServiceMethod(builder, owner, method, "()V");
    for (const auto* method : {"connectWifiDisplay", "forgetWifiDisplay"})
        RejectUnsupportedServiceMethod(builder, owner, method, "(Ljava/lang/String;)V");
    RejectUnsupportedServiceMethod(builder, owner, "renameWifiDisplay", "(Ljava/lang/String;Ljava/lang/String;)V");
    RejectUnsupportedServiceMethod(builder, owner, "getWifiDisplayStatus",
                                   "()Landroid/hardware/display/WifiDisplayStatus;");
    RejectUnsupportedServiceMethod(
        builder, owner, "createVirtualDisplay",
        "(Landroid/os/IBinder;Ljava/lang/String;Ljava/lang/String;IIILandroid/view/Surface;I)I");
    RejectUnsupportedServiceMethod(builder, owner, "releaseVirtualDisplay", "(Landroid/os/IBinder;)V");
    return std::move(builder).Build();
}
}  // namespace ogplay::runtime::android_intrinsics
