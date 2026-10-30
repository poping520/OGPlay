// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_view_Display.cpp ----
#include "catalog.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/integration/bitmap_pixels.h"

#include <bit>
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>

namespace ogplay::runtime::android_intrinsics {

namespace {}  // namespace

Decl Declare_android_view_ContextThemeWrapper(const Context&) {
    return dx::IntrinsicClassBuilder::Class("Landroid/view/ContextThemeWrapper;", "Landroid/content/ContextWrapper;")
        .Build();
}

Decl Declare_android_view_LayoutInflater(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/LayoutInflater;");
    {
        const auto owner =
            builder.BoundInstanceField("mContext", "Landroid/content/Context;", dx::kAccProtected | dx::kAccFinal);
        const auto args = builder.BoundInstanceField("mConstructorArgs", "[Ljava/lang/Object;", dx::kAccFinal);
        const auto filter =
            builder.BoundInstanceField("mFilter", "Landroid/view/LayoutInflater$Filter;", dx::kAccPrivate);
        const auto filters = builder.BoundInstanceField("mFilterMap", "Ljava/util/HashMap;", dx::kAccPrivate);
        const auto cache =
            builder.BoundStaticField("sConstructorMap", "Ljava/util/HashMap;", dx::kAccPrivate | dx::kAccFinal);
        const auto signature =
            builder.BoundStaticField("mConstructorSignature", "[Ljava/lang/Class;", dx::kAccPrivate | dx::kAccFinal);
        builder.FinalMethod(
            "createView", "(Ljava/lang/String;Ljava/lang/String;Landroid/util/AttributeSet;)Landroid/view/View;",
            [owner, args, filter, filters, cache, signature](dx::IntrinsicContext& call) {
                dx::IntrinsicCall fields(call);
                const auto name = call.arguments[0].ref;
                const auto prefix = call.arguments[1].ref;
                const auto attrs = call.arguments[2].ref;
                const auto full =
                    (prefix.IsValid() ? call.vm.StringUtf8(prefix) : std::string{}) + call.vm.StringUtf8(name);
                auto constructor = CallAndroidMethod(call.vm, fields.GetRef(cache), "get",
                                                     "(Ljava/lang/Object;)Ljava/lang/Object;", {dx::VmValue::Ref(name)})
                                       .ref;
                dx::VmObjectRef clazz;
                const auto position = [&] {
                    return call.vm.StringUtf8(
                        CallAndroidMethod(call.vm, attrs, "getPositionDescription", "()Ljava/lang/String;").ref);
                };
                const auto load = [&] {
                    const auto loader =
                        CallAndroidMethod(call.vm, fields.GetRef(owner), "getClassLoader", "()Ljava/lang/ClassLoader;")
                            .ref;
                    const auto text = call.vm.NewStringUtf8(full);
                    const auto roots = call.vm.ProtectReferences(std::array{loader, text});
                    const auto loaded =
                        CallAndroidMethod(call.vm, loader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;",
                                          {dx::VmValue::Ref(text)})
                            .ref;
                    const auto loaded_root = call.vm.ProtectReferences(std::array{loaded});
                    return CallAndroidMethod(call.vm, loaded, "asSubclass", "(Ljava/lang/Class;)Ljava/lang/Class;",
                                             {dx::VmValue::Ref(call.vm.Model().ClassObject(
                                                 call.vm.Linker().ResolveDescriptor("Landroid/view/View;")))})
                        .ref;
                };
                try {
                    const auto check_filter = [&] {
                        if (!CallAndroidMethod(call.vm, fields.GetRef(filter), "onLoadClass", "(Ljava/lang/Class;)Z",
                                               {dx::VmValue::Ref(clazz)})
                                 .AsInt())
                            throw dx::VmJavaThrow{"Landroid/view/InflateException;",
                                                  position() + ": Class not allowed to be inflated " + full};
                    };
                    if (!constructor.IsValid()) {
                        clazz = load();
                        const auto roots = call.vm.ProtectReferences(std::array{clazz});
                        if (fields.GetRef(filter).IsValid()) check_filter();
                        constructor = CallAndroidMethod(call.vm, clazz, "getConstructor",
                                                        "([Ljava/lang/Class;)Ljava/lang/reflect/Constructor;",
                                                        {dx::VmValue::Ref(fields.GetRef(signature))})
                                          .ref;
                        const auto constructor_root = call.vm.ProtectReferences(std::array{constructor});
                        static_cast<void>(CallAndroidMethod(call.vm, fields.GetRef(cache), "put",
                                                            "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                                                            {dx::VmValue::Ref(name), dx::VmValue::Ref(constructor)}));
                    } else if (fields.GetRef(filter).IsValid()) {
                        const auto map = fields.GetRef(filters);
                        auto allowed = CallAndroidMethod(call.vm, map, "get", "(Ljava/lang/Object;)Ljava/lang/Object;",
                                                         {dx::VmValue::Ref(name)})
                                           .ref;
                        if (!allowed.IsValid()) {
                            clazz = load();
                            const auto roots = call.vm.ProtectReferences(std::array{clazz});
                            const auto value = CallAndroidMethod(call.vm, fields.GetRef(filter), "onLoadClass",
                                                                 "(Ljava/lang/Class;)Z", {dx::VmValue::Ref(clazz)});
                            const auto boolean = call.vm.Linker().FindDirectMethod(
                                call.vm.Linker().ResolveDescriptor("Ljava/lang/Boolean;"), "valueOf",
                                "(Z)Ljava/lang/Boolean;");
                            const auto result = call.vm.Call(*boolean, std::array{value});
                            if (result.exception.IsValid())
                                throw dx::VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor,
                                                      result.exception_message, result.exception};
                            allowed = result.value.ref;
                            const auto allowed_root = call.vm.ProtectReferences(std::array{allowed});
                            static_cast<void>(CallAndroidMethod(
                                call.vm, map, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                                {dx::VmValue::Ref(name), dx::VmValue::Ref(allowed)}));
                        }
                        if (!CallAndroidMethod(call.vm, allowed, "booleanValue", "()Z").AsInt())
                            throw dx::VmJavaThrow{"Landroid/view/InflateException;",
                                                  position() + ": Class not allowed to be inflated " + full};
                    }
                    const auto roots = call.vm.ProtectReferences(std::array{constructor, clazz});
                    call.vm.Model().SetObjectElement(fields.GetRef(args), 1, attrs);
                    return CallAndroidMethod(call.vm, constructor, "newInstance",
                                             "([Ljava/lang/Object;)Ljava/lang/Object;",
                                             {dx::VmValue::Ref(fields.GetRef(args))});
                } catch (const dx::VmJavaThrow& error) {
                    const auto error_roots = call.vm.ProtectReferences(std::array{error.existing, clazz});
                    if (error.descriptor == "Ljava/lang/ClassNotFoundException;" ||
                        !call.vm.Linker().IsAssignable(call.vm.Linker().ResolveDescriptor("Ljava/lang/Exception;"),
                                                       call.vm.Linker().ResolveDescriptor(error.descriptor)))
                        throw;
                    const auto message =
                        error.descriptor == "Ljava/lang/NoSuchMethodException;"
                            ? position() + ": Error inflating class " + full
                        : error.descriptor == "Ljava/lang/ClassCastException;"
                            ? position() + ": Class is not a View " + full
                            : position() + ": Error inflating class " +
                                  (clazz.IsValid()
                                       ? call.vm.StringUtf8(
                                             CallAndroidMethod(call.vm, clazz, "getName", "()Ljava/lang/String;").ref)
                                       : "<unknown>");
                    auto cause = error.existing;
                    const auto make = [&](const std::string& type, const std::string& text) {
                        const auto object = call.vm.NewIntrinsicInstance(type);
                        const auto object_root = call.vm.ProtectReferences(std::array{object});
                        const auto string = call.vm.NewStringUtf8(text);
                        const auto protected_refs = call.vm.ProtectReferences(std::array{object, string});
                        const auto init = call.vm.Linker().FindDirectMethod(call.vm.Model().ObjectClass(object),
                                                                            "<init>", "(Ljava/lang/String;)V");
                        const auto result =
                            call.vm.Call(*init, std::array{dx::VmValue::Ref(object), dx::VmValue::Ref(string)});
                        if (result.exception.IsValid())
                            throw dx::VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor,
                                                  result.exception_message, result.exception};
                        return object;
                    };
                    if (!cause.IsValid()) cause = make(error.descriptor, error.message);
                    const auto cause_root = call.vm.ProtectReferences(std::array{cause});
                    const auto exception = make("Landroid/view/InflateException;", message);
                    const auto exception_root = call.vm.ProtectReferences(std::array{exception});
                    static_cast<void>(CallAndroidMethod(call.vm, exception, "initCause",
                                                        "(Ljava/lang/Throwable;)Ljava/lang/Throwable;",
                                                        {dx::VmValue::Ref(cause)}));
                    throw dx::VmJavaThrow{"Landroid/view/InflateException;", message, exception};
                }
            });
        return std::move(builder).Build();
    }
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

[[nodiscard]] std::int32_t DeviceDensityDpi(const Context& context) {
    const double dpi = static_cast<double>(context->ui_density) * 160.0;
    if (!std::isfinite(dpi) || dpi < 1.0 || dpi > std::numeric_limits<std::int32_t>::max())
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "display density is invalid");
    return static_cast<std::int32_t>(std::lround(dpi));
}

}  // namespace

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

Decl Declare_android_util_DisplayMetrics(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/util/DisplayMetrics;");
    builder.StaticMethod(
        "getDeviceDensity", "()I",
        [context](dx::IntrinsicContext&) { return dx::VmValue::Int(DeviceDensityDpi(context)); }, dx::kAccPrivate);
    return std::move(builder).Build();
}

Decl Declare_android_hardware_display_DisplayManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/hardware/display/DisplayManager;", "Ljava/lang/Object;",
                                                    {}, dx::kAccPublic | dx::kAccFinal);
    const auto owner =
        builder.BoundInstanceField("mContext", "Landroid/content/Context;", dx::kAccPrivate | dx::kAccFinal);
    builder.Constructor("(Landroid/content/Context;)V", [owner](dx::IntrinsicContext& call) {
        dx::IntrinsicCall(call).SetRef(owner, call.arguments[0].ref);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("getDisplay", "(I)Landroid/view/Display;", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(call.arguments[0].AsInt() == 0
                                    ? Singleton(call, context, "display", "Landroid/view/Display;")
                                    : dx::VmObjectRef{});
    });
    const auto displays = [context](dx::IntrinsicContext& call) {
        const bool all = call.arguments.empty() || !call.arguments[0].ref.IsValid();
        const auto type = call.vm.Linker().ResolveDescriptor("Landroid/view/Display;");
        const auto array = call.vm.Model().NewObjectArray(call.vm.Linker().ResolveDescriptor("[Landroid/view/Display;"),
                                                          type, all ? 1 : 0);
        const auto roots = call.vm.ProtectReferences(std::array{array});
        if (all)
            call.vm.Model().SetObjectElement(array, 0, Singleton(call, context, "display", "Landroid/view/Display;"));
        return dx::VmValue::Ref(array);
    };
    builder.VirtualMethod("getDisplays", "()[Landroid/view/Display;", displays);
    builder.VirtualMethod("getDisplays", "(Ljava/lang/String;)[Landroid/view/Display;", displays);
    // The sole managed display is not a presentation/external display.
    return std::move(builder).Build();
}

Decl Declare_android_view_Display(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/Display;", "Ljava/lang/Object;");
    builder.FinalMethod("getRealSize", "(Landroid/graphics/Point;)V", [context](dx::IntrinsicContext& call) {
        const auto point = call.arguments[0].ref;
        if (!point.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "Display size output is null"};
        const auto type = call.vm.Model().ObjectClass(point);
        for (const auto [name, value] : {std::pair{"x", context->surface_width}, {"y", context->surface_height}}) {
            const auto field = call.vm.Linker().FindFieldRecursive(type, name, "I");
            if (!field) throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Display point field missing"};
            call.vm.RequireImplementedField(*field);
            call.vm.Model().InstanceSlots(point)[call.vm.Linker().Field(*field).slot] = {value, dx::SlotTag::cat1};
        }
        return dx::VmValue::Void();
    });
    builder.FinalMethod("getWidth", "()I", [context](dx::IntrinsicContext&) {
        return dx::VmValue::Int(static_cast<std::int32_t>(context->surface_width));
    });
    builder.FinalMethod("getHeight", "()I", [context](dx::IntrinsicContext&) {
        return dx::VmValue::Int(static_cast<std::int32_t>(context->surface_height));
    });
    // Managed surface coordinates are landscape-natural and never
    // rotate independently from the host window.
    const auto get_rotation = dx::IntrinsicHandler([](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.FinalMethod("getRotation", "()I", get_rotation);
    builder.FinalMethod("getOrientation", "()I", get_rotation);
    builder.FinalMethod("getDisplayId", "()I", [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    const auto get_metrics = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        PopulateAndroidDisplayMetrics(call, context, call.arguments[0].ref);
        return dx::VmValue::Void();
    });
    builder.FinalMethod("getMetrics", "(Landroid/util/DisplayMetrics;)V", get_metrics);
    builder.FinalMethod("getRealMetrics", "(Landroid/util/DisplayMetrics;)V", get_metrics);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_inputmethod_InputMethodManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {

void CheckInputFocusWithoutConnection(dx::IntrinsicContext& call, const Context& context) {
    // API19 restart/show/hide first checkFocus(), which may itself start an
    // input connection before checking the caller View or window token.
    // No connection is published in this bounded phase. Derive eligibility
    // from the existing owners rather than maintaining a second focus state.
    if (!context->window_has_focus.load() || !context->activity.IsValid() ||
        context->window_focus_activity.load() != context->activity.Value()) {
        return;
    }
    {
        const auto root = context->content_view;
        if (!root.IsValid()) return;
        const auto roots = call.vm.ProtectReferences(std::array{root});
        const auto focused = CallAndroidMethod(call.vm, root, "findFocus", "()Landroid/view/View;").ref;
        if (!focused.IsValid() || !AndroidViewBelongsToRoot(call.vm, *context, focused)) return;
    }
    // A non-editor View can also supply a dummy InputConnection on API19.
    // Do not silently skip it based on onCheckIsTextEditor or the request's
    // receiver/token; a real served candidate requires the next capability.
    if (auto* ledger = call.vm.Ledger()) {
        ledger->RecordUnimplemented("dexvm.input_method_sessions", 0);
    }
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                          "InputConnection creation for the focused View is unsupported"};
}

}  // namespace

Decl Declare_android_view_inputmethod_InputMethodManager(const Context& context) {
    auto builder =
        dx::IntrinsicClassBuilder::Class("Landroid/view/inputmethod/InputMethodManager;", "Ljava/lang/Object;");
    builder.FinalMethod("restartInput", "(Landroid/view/View;)V", [context](dx::IntrinsicContext& call) {
        CheckInputFocusWithoutConnection(call, context);
        return dx::VmValue::Void();
    });
    builder.FinalMethod("showSoftInput", "(Landroid/view/View;I)Z", [context](dx::IntrinsicContext& call) {
        CheckInputFocusWithoutConnection(call, context);
        return dx::VmValue::Int(0);
    });
    builder.FinalMethod("hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z", [context](dx::IntrinsicContext& call) {
        CheckInputFocusWithoutConnection(call, context);
        return dx::VmValue::Int(0);
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_KeyEvent.cpp ----
#include "catalog.h"

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

std::int32_t UnicodeForKeyCode(dx::Interpreter& vm, std::int32_t code, std::int32_t meta) {
    const auto map = NewKeyCharacterMap(vm, true);
    const auto root = vm.ProtectReferences(std::array{map});
    return CallAndroidMethod(vm, map, "get", "(II)I", {dx::VmValue::Int(code), dx::VmValue::Int(meta)}).AsInt();
}

// mPtr is an immutable map selector, never a guest or host memory pointer.
// 1 is the FULL virtual layout; 2 is an empty SPECIAL_FUNCTION device map.
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

Decl Declare_android_view_KeyEvent(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/KeyEvent;", "Ljava/lang/Object;");
    const auto action = builder.BoundInstanceField("mAction", "I", dx::kAccPrivate);
    const auto key_code = builder.BoundInstanceField("mKeyCode", "I", dx::kAccPrivate);
    const auto repeat_count = builder.BoundInstanceField("mRepeatCount", "I", dx::kAccPrivate);
    const auto meta_state = builder.BoundInstanceField("mMetaState", "I", dx::kAccPrivate);
    const auto scan_code = builder.BoundInstanceField("mScanCode", "I", dx::kAccPrivate);
    const auto device_id = builder.BoundInstanceField("mDeviceId", "I", dx::kAccPrivate);
    const auto unicode_char = builder.BoundInstanceField("mOgplayUnicodeChar", "I", dx::kAccPrivate);
    const auto down_time = builder.BoundInstanceField("mDownTime", "J", dx::kAccPrivate);
    const auto event_time = builder.BoundInstanceField("mEventTime", "J", dx::kAccPrivate);
    const auto flags = builder.BoundInstanceField("mFlags", "I", dx::kAccPrivate);
    const auto source = builder.BoundInstanceField("mSource", "I", dx::kAccPrivate);
    builder.Constructor("(II)V", [action, key_code, repeat_count, meta_state, scan_code, device_id, unicode_char,
                                  down_time, event_time, flags, source](dx::IntrinsicContext& call) {
        const auto value = call.arguments[0].AsInt();
        dx::IntrinsicCall fields(call);
        fields.SetLong(down_time, 0);
        fields.SetLong(event_time, 0);
        fields.SetInt(flags, 0);
        fields.SetInt(source, kAndroidKeyboardSource);
        fields.SetInt(action, value);
        fields.SetInt(key_code, call.arguments[1].AsInt());
        fields.SetInt(repeat_count, 0);
        fields.SetInt(meta_state, 0);
        fields.SetInt(scan_code, 0);
        fields.SetInt(device_id, -1);
        fields.SetInt(unicode_char, UnicodeForKeyCode(call.vm, call.arguments[1].AsInt(), 0));
        return dx::VmValue::Void();
    });
    const auto timed_constructor = [action, key_code, repeat_count, meta_state, scan_code, device_id, unicode_char,
                                    down_time, event_time, flags, source](dx::IntrinsicContext& call) {
        dx::IntrinsicCall fields(call);
        fields.SetLong(down_time, call.arguments[0].AsLong());
        fields.SetLong(event_time, call.arguments[1].AsLong());
        fields.SetInt(flags, call.arguments.size() > 8 ? call.arguments[8].AsInt() : 0);
        fields.SetInt(source, call.arguments.size() > 9 ? call.arguments[9].AsInt() : kAndroidKeyboardSource);
        fields.SetInt(action, call.arguments[2].AsInt());
        fields.SetInt(key_code, call.arguments[3].AsInt());
        fields.SetInt(repeat_count, call.arguments[4].AsInt());
        fields.SetInt(meta_state, call.arguments[5].AsInt());
        fields.SetInt(device_id, call.arguments[6].AsInt());
        fields.SetInt(scan_code, call.arguments[7].AsInt());
        fields.SetInt(unicode_char, UnicodeForKeyCode(call.vm, call.arguments[3].AsInt(), call.arguments[5].AsInt()));
        return dx::VmValue::Void();
    };
    builder.Constructor("(JJIIIIII)V", timed_constructor);
    builder.Constructor("(JJIIIIIIII)V", timed_constructor);
    builder.FinalMethod("getDownTime", "()J", [down_time](dx::IntrinsicContext& call) {
        return dx::VmValue::Long(dx::IntrinsicCall(call).GetLong(down_time));
    });
    builder.FinalMethod("getEventTime", "()J", [event_time](dx::IntrinsicContext& call) {
        return dx::VmValue::Long(dx::IntrinsicCall(call).GetLong(event_time));
    });
    builder.FinalMethod("getFlags", "()I", [flags](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(dx::IntrinsicCall(call).GetInt(flags));
    });
    builder.FinalMethod("getAction", "()I", [action](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(dx::IntrinsicCall(call).GetInt(action));
    });
    builder.FinalMethod("getKeyCode", "()I", [key_code](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(dx::IntrinsicCall(call).GetInt(key_code));
    });
    builder.FinalMethod("getRepeatCount", "()I", [repeat_count](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(dx::IntrinsicCall(call).GetInt(repeat_count));
    });
    builder.FinalMethod("getMetaState", "()I", [meta_state](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(dx::IntrinsicCall(call).GetInt(meta_state));
    });
    builder.FinalMethod("getScanCode", "()I", [scan_code](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(dx::IntrinsicCall(call).GetInt(scan_code));
    });
    builder.FinalMethod("getDeviceId", "()I", [device_id](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(dx::IntrinsicCall(call).GetInt(device_id));
    });
    builder.FinalMethod("getSource", "()I", [source](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(dx::IntrinsicCall(call).GetInt(source));
    });
    builder.FinalMethod("getUnicodeChar", "()I", [unicode_char](dx::IntrinsicContext& call) {
        dx::IntrinsicCall fields(call);
        return dx::VmValue::Int(fields.GetInt(unicode_char));
    });
    builder.FinalMethod("getUnicodeChar", "(I)I", [key_code](dx::IntrinsicContext& call) {
        dx::IntrinsicCall fields(call);
        return dx::VmValue::Int(UnicodeForKeyCode(call.vm, fields.GetInt(key_code), call.arguments[0].AsInt()));
    });
    const auto unsupported = [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.input_device_services", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "KeyEvent system-key policy is unsupported"};
    };
    builder.DirectMethod("native_isSystemKey", "(I)Z", unsupported);
    builder.DirectMethod("native_hasDefaultAction", "(I)Z", unsupported);
    return std::move(builder).Build();
}

Decl Declare_android_view_KeyCharacterMap(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/KeyCharacterMap;", "Ljava/lang/Object;");
    builder.StaticMethod("load", "(I)Landroid/view/KeyCharacterMap;", [context](dx::IntrinsicContext& call) {
        const auto find = [&](std::int32_t id) {
            return std::find_if(context->input_devices.begin(), context->input_devices.end(),
                                [id](const auto& device) { return device.id == id; });
        };
        auto device = find(call.arguments[0].AsInt());
        if (device == context->input_devices.end()) device = find(kAndroidKeyboardDeviceId);
        if (device == context->input_devices.end())
            throw dx::VmJavaThrow{"Landroid/view/KeyCharacterMap$UnavailableException;",
                                  "No virtual keyboard character map is installed"};
        const bool keyboard = (device->sources & kAndroidKeyboardSource) == kAndroidKeyboardSource;
        return dx::VmValue::Ref(NewKeyCharacterMap(call.vm, keyboard));
    });
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
        dx::kAccPrivate);
    const auto key_property = [full_map](bool number) {
        return [full_map, number](dx::IntrinsicContext& call) {
            const bool full = full_map(call);
            const auto code = call.arguments[1].AsInt();
            const auto found = std::find_if(kVirtualKeys.begin(), kVirtualKeys.end(),
                                            [code](const auto& row) { return row.code == code; });
            return dx::VmValue::Int(!full || found == kVirtualKeys.end() ? 0 : number ? found->number : found->label);
        };
    };
    builder.StaticMethod("nativeGetNumber", "(II)C", key_property(true), dx::kAccPrivate);
    builder.StaticMethod("nativeGetDisplayLabel", "(II)C", key_property(false), dx::kAccPrivate);
    builder.StaticMethod(
        "nativeGetKeyboardType", "(I)I",
        [full_map](dx::IntrinsicContext& call) { return dx::VmValue::Int(full_map(call) ? 4 : 5); }, dx::kAccPrivate);
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
        dx::kAccPrivate);
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
        dx::kAccPrivate);
    builder.StaticMethod(
        "nativeDispose", "(I)V",
        [full_map](dx::IntrinsicContext& call) {
            // Immutable selectors own no allocation; finalization only clears Java mPtr.
            static_cast<void>(full_map(call));
            return dx::VmValue::Void();
        },
        dx::kAccPrivate);
    const auto unsupported = [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.input_device_services", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "KeyCharacterMap service, Unicode category or normalization capability is unsupported"};
    };
    builder.StaticMethod("nativeReadFromParcel", "(Landroid/os/Parcel;)I", unsupported, dx::kAccPrivate);
    builder.StaticMethod("nativeWriteToParcel", "(ILandroid/os/Parcel;)V", unsupported, dx::kAccPrivate);
    builder.StaticMethod("nativeGetEvents", "(I[C)[Landroid/view/KeyEvent;", unsupported, dx::kAccPrivate);
    builder.StaticMethod("deviceHasKey", "(I)Z", unsupported);
    builder.StaticMethod("deviceHasKeys", "([I)[Z", unsupported);
    // These ordinary algorithms require core Unicode backends outside the
    // admitted keyboard boundary. Fail explicitly until those are available.
    builder.StaticMethod("getDeadChar", "(II)I", unsupported);
    builder.VirtualMethod("isPrintingKey", "(I)Z", unsupported);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime {

void SetAndroidKeyEventUnicode(dexvm::Interpreter& vm, const dexvm::VmObjectRef event,
                               const std::int32_t unicode_char) {
    auto& linker = vm.Linker();
    auto& model = vm.Model();
    const auto field = linker.FindFieldRecursive(model.ObjectClass(event), "mOgplayUnicodeChar", "I");
    if (!field.has_value()) {
        throw dexvm::DexVmError(dexvm::DexVmErrorReason::internal_invariant, "KeyEvent unicode field is missing");
    }
    model.InstanceSlots(event)[linker.Field(*field).slot] = {static_cast<std::uint32_t>(unicode_char),
                                                             dexvm::SlotTag::cat1};
}

}  // namespace ogplay::runtime

// ---- migrated from android_view_MotionEvent.cpp ----
// Motion events read their slots directly.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

// InputDevice value semantics stay in BootDex. Only process service queries
// cross this boundary; no InputManager/Binder singleton is initialized.
Decl Declare_android_view_InputDevice(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/InputDevice;", "Ljava/lang/Object;");
    builder.StaticMethod("getDeviceIds", "()[I", [context](dx::IntrinsicContext& call) {
        const auto ids =
            call.vm.Model().NewPrimitiveArray(call.vm.Linker().ResolveDescriptor("[I"), JniPrimitiveKind::integer,
                                              static_cast<JniSize>(context->input_devices.size()));
        for (std::size_t i = 0; i < context->input_devices.size(); ++i)
            call.vm.Model().SetPrimitiveElement(ids, static_cast<JniSize>(i),
                                                static_cast<std::uint32_t>(context->input_devices[i].id));
        return dx::VmValue::Ref(ids);
    });
    builder.StaticMethod("getDevice", "(I)Landroid/view/InputDevice;", [context](dx::IntrinsicContext& call) {
        const auto id = call.arguments[0].AsInt();
        const auto found = std::find_if(context->input_devices.begin(), context->input_devices.end(),
                                        [id](const auto& device) { return device.id == id; });
        if (found == context->input_devices.end()) return dx::VmValue::Ref(dx::VmObjectRef{});
        const auto type = call.vm.Linker().ResolveDescriptor("Landroid/view/InputDevice;");
        const auto check = [&](const dx::VmCallOutcome& outcome) {
            if (outcome.exception.IsValid())
                throw dx::VmJavaThrow{call.vm.Linker().Class(outcome.exception_class).descriptor,
                                      outcome.exception_message, outcome.exception};
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
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  "API19 InputDevice constructors are unavailable"};
        check(call.vm.Call(*constructor, std::vector<dx::VmValue>{
                                             dx::VmValue::Ref(object), dx::VmValue::Int(id), dx::VmValue::Int(0),
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
    });
    // These service contracts are not implied by device enumeration.
    const auto unsupported = [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.input_device_services", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "InputDevice key capability, vibrator and parcel services are unsupported"};
    };
    builder.VirtualMethod("hasKeys", "([I)[Z", unsupported);
    builder.VirtualMethod("getVibrator", "()Landroid/os/Vibrator;", unsupported);
    builder.VirtualMethod("writeToParcel", "(Landroid/os/Parcel;I)V", unsupported);
    return std::move(builder).Build();
}

namespace {
class MotionSnapshotReader final {
public:
    using Layout = MotionSnapshotLayout;

    MotionSnapshotReader(dx::IntrinsicContext& call, dx::IntrinsicFieldHandle field)
        : call_(call), data_(dx::IntrinsicCall(call).GetRef(field)) {
        if (!data_.IsValid())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "MotionEvent is recycled or uninitialized"};
    }

    std::uint64_t Word(std::size_t index) const {
        return call_.vm.Model().GetPrimitiveElement(data_, static_cast<JniSize>(index));
    }

    std::int32_t Int(std::size_t index) const {
        return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(Word(index)));
    }

    float Float(std::size_t index) const { return std::bit_cast<float>(static_cast<std::uint32_t>(Word(index))); }

    std::size_t Pointer(std::int32_t index) const {
        if (index < 0 || static_cast<std::uint64_t>(index) >= Word(Layout::pointer_count))
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "MotionEvent pointer index out of range"};
        return static_cast<std::size_t>(index);
    }

    std::size_t History(std::int32_t index) const {
        if (index < 0 || static_cast<std::uint64_t>(index) >= Word(Layout::history_count))
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "MotionEvent history index out of range"};
        return static_cast<std::size_t>(index) + 1;
    }

    std::size_t Sample(std::size_t index) const {
        return Layout::Sample(static_cast<std::size_t>(Word(Layout::pointer_count)), index);
    }

    float Axis(std::int32_t axis, std::int32_t pointer, std::size_t sample = 0, bool raw = false) const {
        const auto index = Pointer(pointer);
        if (axis < 0 || axis >= 64) return 0;
        auto value = Float(Sample(sample) + 1 + index * 64 + static_cast<std::size_t>(axis));
        if (!raw && axis == 0) value += Float(Layout::x_offset);
        if (!raw && axis == 1) value += Float(Layout::y_offset);
        return value;
    }

private:
    dx::IntrinsicContext& call_;
    dx::VmObjectRef data_;
};

// nativeInitialize/nativeAddBatch inputs. PointerCoords/PointerProperties are
// BootDex objects; their values are read through the guest-visible API only.
constexpr std::int32_t kMaxMotionPointers = 16;
using MotionAxes = std::array<float, MotionSnapshotLayout::axis_count>;

[[noreturn]] void ThrowMotion(const char* type, std::string message) {
    throw dx::VmJavaThrow{type, std::move(message)};
}

void ValidateMotionPointerCount(const std::int32_t count) {
    if (count < 1) ThrowMotion("Ljava/lang/IllegalArgumentException;", "pointerCount must be at least 1");
    if (count > kMaxMotionPointers)
        ThrowMotion("Ljava/lang/IllegalArgumentException;", "pointerCount exceeds the 16 pointer limit");
}

void ValidateMotionArray(dx::Interpreter& vm, const dx::VmObjectRef array, const std::int32_t count, const char* name) {
    if (!array.IsValid())
        ThrowMotion("Ljava/lang/IllegalArgumentException;", std::string(name) + " array must not be null");
    if (vm.Model().ArrayLength(array) < count)
        ThrowMotion("Ljava/lang/IllegalArgumentException;",
                    std::string(name) + " array must be large enough to hold all pointers");
}

MotionAxes ReadPointerCoords(dx::Interpreter& vm, const dx::VmObjectRef array, const std::int32_t index,
                             const float x_offset, const float y_offset) {
    const auto coords = vm.Model().GetObjectElement(array, index);
    if (!coords.IsValid()) ThrowMotion("Ljava/lang/NullPointerException;", "pointerCoords");
    MotionAxes axes{};
    for (std::size_t axis = 0; axis < axes.size(); ++axis)
        axes[axis] =
            CallAndroidMethod(vm, coords, "getAxisValue", "(I)F", {dx::VmValue::Int(static_cast<std::int32_t>(axis))})
                .AsFloat();
    axes[0] -= x_offset;
    axes[1] -= y_offset;
    return axes;
}

std::int32_t ReadIntField(dx::Interpreter& vm, const dx::VmObjectRef object, const char* name) {
    const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), name, "I");
    if (!field)
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             std::string("PointerProperties field is missing: ") + name);
    return static_cast<std::int32_t>(vm.Model().InstanceSlots(object)[vm.Linker().Field(*field).slot].bits);
}

struct MotionInit final {
    std::int32_t action{}, device{}, source{}, flags{}, meta{}, buttons{}, edges{};
    std::int64_t down_time_ms{}, event_time_ms{};
    float x_precision{}, y_precision{};
    std::vector<std::pair<std::int32_t, std::int32_t>> pointers;  // id, tool type
    std::vector<MotionAxes> axes;
};

constexpr std::int64_t MillisToNanos(const std::int64_t ms) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(ms) * 1000000U);
}

dx::VmObjectRef NewMotionEvent(dx::Interpreter& vm, const MotionInit& init) {
    using Layout = MotionSnapshotLayout;
    const auto instance = vm.NewIntrinsicInstance("Landroid/view/MotionEvent;");
    const auto root = vm.ProtectReferences(std::array{instance});
    const auto constructor = vm.Linker().FindDirectMethod(vm.Model().ObjectClass(instance), "<init>", "()V");
    const auto initialized = vm.Call(*constructor, std::array{dx::VmValue::Ref(instance)});
    if (initialized.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(initialized.exception_class).descriptor, initialized.exception_message,
                              initialized.exception};
    const auto count = init.pointers.size();
    const auto data = vm.Model().NewPrimitiveArray(vm.Linker().ResolveDescriptor("[J"), JniPrimitiveKind::long_integer,
                                                   static_cast<JniSize>(Layout::Sample(count, 1)));
    const auto data_root = vm.ProtectReferences(std::array{data});
    const auto put = [&](std::size_t index, std::uint64_t bits) {
        vm.Model().SetPrimitiveElement(data, static_cast<JniSize>(index), bits);
    };
    put(Layout::action, static_cast<std::uint32_t>(init.action));
    put(Layout::device, static_cast<std::uint32_t>(init.device));
    put(Layout::source, static_cast<std::uint32_t>(init.source));
    put(Layout::flags, static_cast<std::uint32_t>(init.flags));
    put(Layout::meta, static_cast<std::uint32_t>(init.meta));
    put(Layout::buttons, static_cast<std::uint32_t>(init.buttons));
    put(Layout::edges, static_cast<std::uint32_t>(init.edges));
    put(Layout::down_time, static_cast<std::uint64_t>(MillisToNanos(init.down_time_ms)));
    put(Layout::x_offset, 0);
    put(Layout::y_offset, 0);
    put(Layout::x_precision, std::bit_cast<std::uint32_t>(init.x_precision));
    put(Layout::y_precision, std::bit_cast<std::uint32_t>(init.y_precision));
    put(Layout::pointer_count, count);
    put(Layout::history_count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        put(Layout::header_size + i * 2, static_cast<std::uint32_t>(init.pointers[i].first));
        put(Layout::header_size + i * 2 + 1, static_cast<std::uint32_t>(init.pointers[i].second));
    }
    const auto base = Layout::Sample(count, 0);
    put(base, static_cast<std::uint64_t>(MillisToNanos(init.event_time_ms)));
    for (std::size_t i = 0; i < count; ++i)
        for (std::size_t axis = 0; axis < Layout::axis_count; ++axis)
            put(base + 1 + i * Layout::axis_count + axis, std::bit_cast<std::uint32_t>(init.axes[i][axis]));
    const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(instance), "mOgplaySnapshot", "[J");
    if (!field) throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "MotionEvent snapshot field is missing");
    vm.Model().InstanceSlots(instance)[vm.Linker().Field(*field).slot] = {data.Value(), dx::SlotTag::ref};
    return instance;
}

// MotionEvent.obtain(downTime, eventTime, action, pointerCount, pointerProperties,
// pointerCoords, metaState, buttonState, xPrecision, yPrecision, deviceId, edgeFlags,
// source, flags). Argument layout is shared by both overloads below; `properties` holds
// either PointerProperties[] or int[] pointer ids.
dx::VmValue ObtainMotionEvent(dx::IntrinsicContext& call, const bool pointer_ids) {
    auto& vm = call.vm;
    const auto& a = call.arguments;
    // Overload (JJII[I[Coords;IFFIIII) has no buttonState; (..[Props;[Coords;IIFFIIII) does.
    const std::size_t after_meta = 7;
    const auto count = a[3].AsInt();
    const auto properties = a[4].ref;
    const auto coords = a[5].ref;
    ValidateMotionPointerCount(count);
    MotionInit init;
    init.down_time_ms = a[0].AsLong();
    init.event_time_ms = a[1].AsLong();
    init.action = a[2].AsInt();
    init.meta = a[6].AsInt();
    std::size_t next = after_meta;
    if (!pointer_ids) init.buttons = a[next++].AsInt();
    init.x_precision = a[next++].AsFloat();
    init.y_precision = a[next++].AsFloat();
    init.device = a[next++].AsInt();
    init.edges = a[next++].AsInt();
    init.source = a[next++].AsInt();
    init.flags = a[next++].AsInt();
    if (pointer_ids) {
        // Java side of the AOSP overload: pointerIds[i] is dereferenced before native validation.
        if (!properties.IsValid()) ThrowMotion("Ljava/lang/NullPointerException;", "pointerIds");
        if (vm.Model().ArrayLength(properties) < count)
            ThrowMotion("Ljava/lang/ArrayIndexOutOfBoundsException;", "pointerIds");
    } else {
        ValidateMotionArray(vm, properties, count, "pointerProperties");
    }
    ValidateMotionArray(vm, coords, count, "pointerCoords");
    for (std::int32_t i = 0; i < count; ++i) {
        if (pointer_ids) {
            init.pointers.emplace_back(static_cast<std::int32_t>(vm.Model().GetPrimitiveElement(properties, i)), 0);
        } else {
            const auto props = vm.Model().GetObjectElement(properties, i);
            if (!props.IsValid()) ThrowMotion("Ljava/lang/NullPointerException;", "pointerProperties");
            init.pointers.emplace_back(ReadIntField(vm, props, "id"), ReadIntField(vm, props, "toolType"));
        }
        init.axes.push_back(ReadPointerCoords(vm, coords, i, 0, 0));
    }
    return dx::VmValue::Ref(NewMotionEvent(vm, init));
}
}  // namespace

Decl Declare_android_view_MotionEvent(const Context& context) {
    static_cast<void>(context);
    using Layout = MotionSnapshotLayout;
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/MotionEvent;", "Landroid/view/InputEvent;",
                                                    {"Landroid/os/Parcelable;"});
    builder.Constructor(
        "()V",
        [](dx::IntrinsicContext& c) {
            const auto target = c.vm.Linker().FindDirectMethod(
                c.vm.Linker().ResolveDescriptor("Landroid/view/InputEvent;"), "<init>", "()V");
            std::vector<dx::VmValue> arguments{dx::VmValue::Ref(c.receiver)};
            const std::vector<dx::VmValue> parameters{};
            arguments.insert(arguments.end(), parameters.begin(), parameters.end());
            const auto result = c.vm.Call(*target, arguments);
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        },
        dx::kAccPrivate);
    // API19 action constants must also be declared for JNI field lookup.
    for (const auto& [name, value] : std::array{std::pair{"ACTION_MASK", 0xff},
                                                std::pair{"ACTION_DOWN", 0},
                                                std::pair{"ACTION_UP", 1},
                                                std::pair{"ACTION_MOVE", 2},
                                                std::pair{"ACTION_CANCEL", 3},
                                                std::pair{"ACTION_OUTSIDE", 4},
                                                std::pair{"ACTION_POINTER_DOWN", 5},
                                                std::pair{"ACTION_POINTER_UP", 6},
                                                std::pair{"ACTION_HOVER_MOVE", 7},
                                                std::pair{"ACTION_SCROLL", 8},
                                                std::pair{"ACTION_HOVER_ENTER", 9},
                                                std::pair{"ACTION_HOVER_EXIT", 10},
                                                std::pair{"ACTION_POINTER_INDEX_MASK", 0xff00},
                                                std::pair{"ACTION_POINTER_INDEX_SHIFT", 8},
                                                std::pair{"ACTION_POINTER_1_DOWN", 5},
                                                std::pair{"ACTION_POINTER_2_DOWN", 0x105},
                                                std::pair{"ACTION_POINTER_3_DOWN", 0x205},
                                                std::pair{"ACTION_POINTER_1_UP", 6},
                                                std::pair{"ACTION_POINTER_2_UP", 0x106},
                                                std::pair{"ACTION_POINTER_3_UP", 0x206},
                                                std::pair{"ACTION_POINTER_ID_MASK", 0xff00},
                                                std::pair{"ACTION_POINTER_ID_SHIFT", 8}})
        builder.ConstantInt(name, "I", value, dx::kAccPublic | dx::kAccStatic | dx::kAccFinal);
    // API19 symbolic names for the already-supported 64-axis snapshot ABI.
    for (const auto& [name, value] : std::array{
             std::pair{"AXIS_X", 0},           std::pair{"AXIS_Y", 1},           std::pair{"AXIS_PRESSURE", 2},
             std::pair{"AXIS_SIZE", 3},        std::pair{"AXIS_TOUCH_MAJOR", 4}, std::pair{"AXIS_TOUCH_MINOR", 5},
             std::pair{"AXIS_TOOL_MAJOR", 6},  std::pair{"AXIS_TOOL_MINOR", 7},  std::pair{"AXIS_ORIENTATION", 8},
             std::pair{"AXIS_VSCROLL", 9},     std::pair{"AXIS_HSCROLL", 10},    std::pair{"AXIS_Z", 11},
             std::pair{"AXIS_RX", 12},         std::pair{"AXIS_RY", 13},         std::pair{"AXIS_RZ", 14},
             std::pair{"AXIS_HAT_X", 15},      std::pair{"AXIS_HAT_Y", 16},      std::pair{"AXIS_LTRIGGER", 17},
             std::pair{"AXIS_RTRIGGER", 18},   std::pair{"AXIS_THROTTLE", 19},   std::pair{"AXIS_RUDDER", 20},
             std::pair{"AXIS_WHEEL", 21},      std::pair{"AXIS_GAS", 22},        std::pair{"AXIS_BRAKE", 23},
             std::pair{"AXIS_DISTANCE", 24},   std::pair{"AXIS_TILT", 25},       std::pair{"AXIS_GENERIC_1", 32},
             std::pair{"AXIS_GENERIC_2", 33},  std::pair{"AXIS_GENERIC_3", 34},  std::pair{"AXIS_GENERIC_4", 35},
             std::pair{"AXIS_GENERIC_5", 36},  std::pair{"AXIS_GENERIC_6", 37},  std::pair{"AXIS_GENERIC_7", 38},
             std::pair{"AXIS_GENERIC_8", 39},  std::pair{"AXIS_GENERIC_9", 40},  std::pair{"AXIS_GENERIC_10", 41},
             std::pair{"AXIS_GENERIC_11", 42}, std::pair{"AXIS_GENERIC_12", 43}, std::pair{"AXIS_GENERIC_13", 44},
             std::pair{"AXIS_GENERIC_14", 45}, std::pair{"AXIS_GENERIC_15", 46}, std::pair{"AXIS_GENERIC_16", 47}})
        builder.ConstantInt(name, "I", value, dx::kAccPublic | dx::kAccStatic | dx::kAccFinal);
    const auto data = builder.BoundInstanceField("mOgplaySnapshot", "[J", dx::kAccPrivate);
    const std::pair<const char*, std::size_t> integers[]{{"getAction", Layout::action},
                                                         {"getDeviceId", Layout::device},
                                                         {"getSource", Layout::source},
                                                         {"getFlags", Layout::flags},
                                                         {"getMetaState", Layout::meta},
                                                         {"getButtonState", Layout::buttons},
                                                         {"getEdgeFlags", Layout::edges},
                                                         {"getPointerCount", Layout::pointer_count},
                                                         {"getHistorySize", Layout::history_count}};
    for (const auto [name, slot] : integers) {
        const auto handler = [data, slot](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(MotionSnapshotReader(call, data).Int(slot));
        };
        if (slot == Layout::device || slot == Layout::source)
            builder.FinalOverrideMethod(name, "()I", handler);
        else
            builder.FinalMethod(name, "()I", handler);
    }
    builder.FinalMethod("getActionMasked", "()I", [data](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(MotionSnapshotReader(call, data).Int(Layout::action) & 0xff);
    });
    builder.FinalMethod("getActionIndex", "()I", [data](dx::IntrinsicContext& call) {
        return dx::VmValue::Int((MotionSnapshotReader(call, data).Int(Layout::action) >> 8) & 0xff);
    });
    for (bool tool : {false, true})
        builder.FinalMethod(tool ? "getToolType" : "getPointerId", "(I)I", [data, tool](dx::IntrinsicContext& call) {
            MotionSnapshotReader reader(call, data);
            const auto index = reader.Pointer(call.arguments[0].AsInt());
            return dx::VmValue::Int(reader.Int(Layout::header_size + 2 * index + (tool ? 1 : 0)));
        });
    builder.FinalMethod("findPointerIndex", "(I)I", [data](dx::IntrinsicContext& call) {
        MotionSnapshotReader reader(call, data);
        for (std::int32_t i = 0; i < reader.Int(Layout::pointer_count); ++i)
            if (reader.Int(Layout::header_size + 2 * static_cast<std::size_t>(i)) == call.arguments[0].AsInt())
                return dx::VmValue::Int(i);
        return dx::VmValue::Int(-1);
    });
    builder.FinalMethod("getDownTime", "()J", [data](dx::IntrinsicContext& call) {
        return dx::VmValue::Long(static_cast<std::int64_t>(MotionSnapshotReader(call, data).Word(Layout::down_time)) /
                                 1000000);
    });
    for (bool nano : {false, true}) {
        builder.FinalOverrideMethod(
            nano ? "getEventTimeNano" : "getEventTime", "()J", [data, nano](dx::IntrinsicContext& call) {
                MotionSnapshotReader reader(call, data);
                return dx::VmValue::Long(static_cast<std::int64_t>(reader.Word(reader.Sample(0))) /
                                         (nano ? 1 : 1000000));
            });
        builder.FinalMethod(nano ? "getHistoricalEventTimeNano" : "getHistoricalEventTime", "(I)J",
                            [data, nano](dx::IntrinsicContext& call) {
                                MotionSnapshotReader reader(call, data);
                                const auto sample = reader.History(call.arguments[0].AsInt());
                                return dx::VmValue::Long(static_cast<std::int64_t>(reader.Word(reader.Sample(sample))) /
                                                         (nano ? 1 : 1000000));
                            });
    }
    const std::array names{"X",          "Y",         "Pressure",  "Size",       "TouchMajor",
                           "TouchMinor", "ToolMajor", "ToolMinor", "Orientation"};
    for (std::size_t axis = 0; axis < names.size(); ++axis) {
        const auto name = std::string("get") + names[axis];
        const auto history_name = std::string("getHistorical") + names[axis];
        for (bool indexed : {false, true}) {
            builder.FinalMethod(name, indexed ? "(I)F" : "()F", [data, axis, indexed](dx::IntrinsicContext& call) {
                return dx::VmValue::Float(
                    MotionSnapshotReader(call, data)
                        .Axis(static_cast<std::int32_t>(axis), indexed ? call.arguments[0].AsInt() : 0));
            });
            builder.FinalMethod(
                history_name, indexed ? "(II)F" : "(I)F", [data, axis, indexed](dx::IntrinsicContext& call) {
                    MotionSnapshotReader reader(call, data);
                    const auto sample = reader.History(call.arguments[indexed ? 1 : 0].AsInt());
                    return dx::VmValue::Float(
                        reader.Axis(static_cast<std::int32_t>(axis), indexed ? call.arguments[0].AsInt() : 0, sample));
                });
        }
    }
    for (bool indexed : {false, true}) {
        builder.FinalMethod("getAxisValue", indexed ? "(II)F" : "(I)F", [data, indexed](dx::IntrinsicContext& call) {
            return dx::VmValue::Float(MotionSnapshotReader(call, data)
                                          .Axis(call.arguments[0].AsInt(), indexed ? call.arguments[1].AsInt() : 0));
        });
        builder.FinalMethod("getHistoricalAxisValue", indexed ? "(III)F" : "(II)F",
                            [data, indexed](dx::IntrinsicContext& call) {
                                MotionSnapshotReader reader(call, data);
                                const auto sample = reader.History(call.arguments[indexed ? 2 : 1].AsInt());
                                return dx::VmValue::Float(reader.Axis(call.arguments[0].AsInt(),
                                                                      indexed ? call.arguments[1].AsInt() : 0, sample));
                            });
    }
    for (std::int32_t axis = 0; axis < 2; ++axis) {
        builder.FinalMethod(axis == 0 ? "getRawX" : "getRawY", "()F", [data, axis](dx::IntrinsicContext& call) {
            return dx::VmValue::Float(MotionSnapshotReader(call, data).Axis(axis, 0, 0, true));
        });
        builder.FinalMethod(
            axis == 0 ? "getXPrecision" : "getYPrecision", "()F", [data, axis](dx::IntrinsicContext& call) {
                return dx::VmValue::Float(
                    MotionSnapshotReader(call, data).Float(axis == 0 ? Layout::x_precision : Layout::y_precision));
            });
    }
    builder.StaticMethod("obtain", "(JJII[I[Landroid/view/MotionEvent$PointerCoords;IFFIIII)Landroid/view/MotionEvent;",
                         [](dx::IntrinsicContext& call) { return ObtainMotionEvent(call, true); });
    builder.StaticMethod(
        "obtain",
        "(JJII[Landroid/view/MotionEvent$PointerProperties;[Landroid/view/MotionEvent$PointerCoords;IIFFIIII)"
        "Landroid/view/MotionEvent;",
        [](dx::IntrinsicContext& call) { return ObtainMotionEvent(call, false); });
    // nativeAddBatch: the current sample becomes the newest historical sample.
    builder.FinalMethod(
        "addBatch", "(J[Landroid/view/MotionEvent$PointerCoords;I)V", [data](dx::IntrinsicContext& call) {
            auto& vm = call.vm;
            MotionSnapshotReader reader(call, data);
            const auto count = static_cast<std::int32_t>(reader.Word(Layout::pointer_count));
            const auto pointers = static_cast<std::size_t>(count);
            const auto history = static_cast<std::size_t>(reader.Word(Layout::history_count));
            const auto array = call.arguments[1].ref;
            ValidateMotionArray(vm, array, count, "pointerCoords");
            std::vector<MotionAxes> axes;
            for (std::int32_t i = 0; i < count; ++i)
                axes.push_back(
                    ReadPointerCoords(vm, array, i, reader.Float(Layout::x_offset), reader.Float(Layout::y_offset)));
            const auto fresh =
                vm.Model().NewPrimitiveArray(vm.Linker().ResolveDescriptor("[J"), JniPrimitiveKind::long_integer,
                                             static_cast<JniSize>(Layout::Sample(pointers, history + 2)));
            const auto fresh_root = vm.ProtectReferences(std::array{fresh});
            const auto put = [&](std::size_t index, std::uint64_t bits) {
                vm.Model().SetPrimitiveElement(fresh, static_cast<JniSize>(index), bits);
            };
            // Everything keeps its position; the old current sample is appended as the newest history.
            const auto old_end = Layout::Sample(pointers, history + 1);
            for (std::size_t i = 0; i < old_end; ++i)
                put(i, reader.Word(i));
            const auto stride = Layout::Sample(pointers, 1) - Layout::Sample(pointers, 0);
            for (std::size_t i = 0; i < stride; ++i)
                put(old_end + i, reader.Word(Layout::Sample(pointers, 0) + i));
            put(Layout::meta, static_cast<std::uint32_t>(reader.Int(Layout::meta) | call.arguments[2].AsInt()));
            put(Layout::history_count, history + 1);
            const auto base = Layout::Sample(pointers, 0);
            put(base, static_cast<std::uint64_t>(MillisToNanos(call.arguments[0].AsLong())));
            for (std::size_t i = 0; i < pointers; ++i)
                for (std::size_t axis = 0; axis < Layout::axis_count; ++axis)
                    put(base + 1 + i * Layout::axis_count + axis, std::bit_cast<std::uint32_t>(axes[i][axis]));
            dx::IntrinsicCall(call).SetRef(data, fresh);
            return dx::VmValue::Void();
        });
    builder.FinalOverrideMethod("recycle", "()V", [data](dx::IntrinsicContext& call) {
        static_cast<void>(MotionSnapshotReader(call, data));
        dx::IntrinsicCall(call).SetRef(data, dx::VmObjectRef{});
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_SurfaceHolder_Callback.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_SurfaceHolder_Callback(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/SurfaceHolder$Callback;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_SurfaceHolder_Impl.cpp ----
#include "catalog.h"
#include <limits>

namespace ogplay::runtime::android_intrinsics {

dx::VmObjectRef GetSurfaceHolderFrame(dx::Interpreter& vm, DexVmAndroidContext& context, const dx::VmObjectRef holder) {
    if (!context.surface_holder_frame_field.has_value()) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "SurfaceHolder frame field is not bound");
    }
    const auto holder_root = vm.ProtectReferences(std::array{holder});
    dx::IntrinsicContext call{vm, holder, {}};
    dx::IntrinsicCall fields(call);
    auto frame = fields.GetRef(*context.surface_holder_frame_field);
    if (frame.IsValid()) return frame;
    const auto type = vm.Linker().ResolveDescriptor("Landroid/graphics/Rect;");
    const auto initialized = vm.EnsureClassInitialized(type);
    if (initialized.exception.IsValid()) {
        throw dx::VmJavaThrow{vm.Linker().Class(initialized.exception_class).descriptor, initialized.exception_message,
                              initialized.exception};
    }
    frame = vm.NewIntrinsicInstance("Landroid/graphics/Rect;");
    const auto frame_root = vm.ProtectReferences(std::array{frame});
    const auto constructor = vm.Linker().FindDirectMethod(type, "<init>", "()V");
    if (!constructor) {
        throw dx::DexVmError(dx::DexVmErrorReason::unresolved_reference, "Rect constructor is not linked");
    }
    const auto outcome = vm.Call(*constructor, std::array{dx::VmValue::Ref(frame)});
    if (outcome.exception.IsValid()) {
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
    }
    fields.SetRef(*context.surface_holder_frame_field, frame);
    return frame;
}

namespace {
dx::VmObjectRef ManagedHolderView(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef holder) {
    for (const auto& [view, candidate] : context.surface_holders)
        if (candidate == holder) return dx::VmObjectRef{view};
    if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.surface_buffer_geometry", 0);
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "SurfaceHolder has no current managed View"};
}

void SetManagedSurfaceRequest(dx::IntrinsicContext& call, DexVmAndroidContext& context, std::int32_t width,
                              std::int32_t height) {
    // Managed holder 0 means layout, never ROM fixed=0. Canonicalize the old
    // nonpositive layout requests to the exact ROM sentinel before publication.
    if (call.receiver == context.window_surface_holder) {
        if ((width > 0 && static_cast<std::uint32_t>(width) != context.surface_width) ||
            (height > 0 && static_cast<std::uint32_t>(height) != context.surface_height)) {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.surface_buffer_geometry", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "window buffer geometry is fixed"};
        }
        return;
    }
    if (width > 4096 || height > 4096) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Surface buffer exceeds 4096 extent budget"};
    }
    const auto view = ManagedHolderView(call.vm, context, call.receiver);
    dx::IntrinsicContext view_call{call.vm, view, {}};
    dx::IntrinsicCall fields(view_call);
    width = width > 0 ? width : -1;
    height = height > 0 ? height : -1;
    const auto w = *context.surface_view_requested_width_field;
    const auto h = *context.surface_view_requested_height_field;
    if (fields.GetInt(w) != width || fields.GetInt(h) != height) {
        // Both axes are validated before either field changes; no buffer/frame
        // publication or geometry backfill is part of a measurement request.
        fields.SetInt(w, width);
        fields.SetInt(h, height);
        static_cast<void>(CallAndroidMethod(call.vm, view, "requestLayout", "()V"));
    }
}

// ROM View.getWidth/getHeight are final mRight-mLeft / mBottom-mTop. Reading the
// fields keeps buffer geometry queries out of the interpreter, so NDK producer
// threads (ANativeWindow_fromSurface) never borrow the owner execution.
ui::Size ViewFrameSize(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef view) {
    if (!context.view_java_measure_fields)
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "View frame fields are not bound");
    dx::IntrinsicContext call{vm, view, {}};
    dx::IntrinsicCall fields(call);
    const auto& frame = context.view_java_measure_fields->frame;
    return {fields.GetInt(frame[2]) - fields.GetInt(frame[0]), fields.GetInt(frame[3]) - fields.GetInt(frame[1])};
}
}  // namespace

ui::Size ManagedSurfaceBufferSize(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef holder) {
    ui::Size size{static_cast<std::int32_t>(context.surface_width), static_cast<std::int32_t>(context.surface_height)};
    if (holder != context.window_surface_holder && !context.window_surface_holders.contains(holder.Value())) {
        const auto view = ManagedHolderView(vm, context, holder);
        dx::IntrinsicContext call{vm, view, {}};
        dx::IntrinsicCall fields(call);
        // ROM updateWindow resolves each nonpositive axis against the View frame.
        // Legacy fixtures without a Java layout keep their existing window geometry.
        const auto [w, h] = ViewFrameSize(vm, context, view);
        if (w > 0) size.width = w;
        if (h > 0) size.height = h;
        const auto rw = fields.GetInt(*context.surface_view_requested_width_field);
        const auto rh = fields.GetInt(*context.surface_view_requested_height_field);
        if (rw > 0) size.width = rw;
        if (rh > 0) size.height = rh;
    }
    if (size.width <= 0 || size.height <= 0 || size.width > 4096 || size.height > 4096)
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Surface buffer exceeds managed extent budget"};
    return size;
}

void PublishSurfaceHolderFrame(dx::Interpreter& vm, DexVmAndroidContext& context, const dx::VmObjectRef holder) {
    auto size = ManagedSurfaceBufferSize(vm, context, holder);
    if (holder != context.window_surface_holder && !context.window_surface_holders.contains(holder.Value())) {
        const auto view = ManagedHolderView(vm, context, holder);
        // API19 SurfaceView publishes the relayout window frame here, while
        // surfaceChanged and producer dequeue report the independent buffer size.
        const auto [width, height] = ViewFrameSize(vm, context, view);
        if (width > 0) size.width = width;
        if (height > 0) size.height = height;
    }
    const auto holder_root = vm.ProtectReferences(std::array{holder});
    const auto frame = GetSurfaceHolderFrame(vm, context, holder);
    static_cast<void>(CallAndroidMethod(
        vm, frame, "set", "(IIII)V",
        {dx::VmValue::Int(0), dx::VmValue::Int(0), dx::VmValue::Int(size.width), dx::VmValue::Int(size.height)}));
}

namespace {

dx::IntrinsicHandler LockCanvasHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        auto& canvas = context->holder_canvases[call.receiver.Value()];
        if (!canvas.IsValid()) {
            canvas = call.vm.NewIntrinsicInstance("Landroid/graphics/Canvas;");
        }
        auto& state = context->canvases[canvas.Value()];
        if (state.locked) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "SurfaceHolder canvas is already locked"};
        }
        state.holder = call.receiver.Value();
        if (!context->active_surface_holders.contains(call.receiver.Value()))
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "SurfaceHolder is not active"};
        const auto size = ManagedSurfaceBufferSize(call.vm, *context, call.receiver);
        state.width = static_cast<std::uint32_t>(size.width);
        state.height = static_cast<std::uint32_t>(size.height);
        const auto format = context->surface_active_formats.find(call.receiver.Value());
        const bool alpha =
            format != context->surface_active_formats.end() && AndroidPixelFormatHasAlpha(format->second);
        // Surface contents in the complete dirty region are undefined; choose deterministic initial pixels.
        state.argb.assign(static_cast<std::size_t>(state.width) * state.height, alpha ? 0U : 0xff000000U);
        if (!call.arguments.empty() && call.arguments[0].ref.IsValid()) {
            // Every lock currently redraws the complete buffer; advertise the expanded dirty region.
            static_cast<void>(CallAndroidMethod(call.vm, call.arguments[0].ref, "set", "(IIII)V",
                                                {dx::VmValue::Int(0), dx::VmValue::Int(0), dx::VmValue::Int(size.width),
                                                 dx::VmValue::Int(size.height)}));
        }
        state.locked = true;
        return dx::VmValue::Ref(canvas);
    };
}

dx::IntrinsicHandler UnlockCanvasAndPostHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        const auto canvas = call.arguments[0].ref;
        const auto found = context->canvases.find(canvas.Value());
        if (!canvas.IsValid() || found == context->canvases.end() || !found->second.locked ||
            found->second.holder != call.receiver.Value()) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Canvas is not locked by this SurfaceHolder"};
        }
        if (context->session == nullptr) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "SurfaceHolder has no guest session"};
        }
        if (!context->active_surface_holders.contains(call.receiver.Value()))
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "SurfaceHolder is no longer active"};
        auto& state = found->second;
        std::vector<std::uint8_t> rgba8;
        rgba8.reserve(state.argb.size() * 4U);
        for (const auto pixel : state.argb) {
            rgba8.push_back(static_cast<std::uint8_t>(pixel >> 16U));
            rgba8.push_back(static_cast<std::uint8_t>(pixel >> 8U));
            rgba8.push_back(static_cast<std::uint8_t>(pixel));
            rgba8.push_back(static_cast<std::uint8_t>(pixel >> 24U));
        }
        state.locked = false;
        {
            std::scoped_lock lock(context->surface_pixels_mutex);
            context->surface_buffer_queued[call.receiver.Value()] = true;
        }
        if (call.receiver == context->window_surface_holder) {
            const auto format = context->surface_active_formats.find(call.receiver.Value());
            if (format == context->surface_active_formats.end() || !AndroidPixelFormatHasAlpha(format->second))
                for (std::size_t i = 3; i < rgba8.size(); i += 4)
                    rgba8[i] = 255;
            context->session->PublishSoftwareFrame(std::move(rgba8));
            return dx::VmValue::Void();
        }
        {
            std::scoped_lock lock(context->surface_pixels_mutex);
            context->surface_pixels[call.receiver.Value()] =
                video::VideoFrame{0, state.width, state.height, std::move(rgba8)};
        }
        context->session->PublishSoftwareFrame(std::vector<std::uint8_t>(
            static_cast<std::size_t>(context->surface_width) * context->surface_height * 4U, 0U));
        return dx::VmValue::Void();
    };
}

dx::IntrinsicHandler HolderGetSurface(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        auto& surface = context->holder_surfaces[call.receiver.Value()];
        if (!surface.IsValid()) {
            surface = call.vm.NewIntrinsicInstance("Landroid/view/Surface;");
            try {
                InitializeManagedJavaSurface(call.vm, *context, surface, call.receiver.Value());
            } catch (...) {
                surface = dx::VmObjectRef{};
                throw;
            }
        }
        return dx::VmValue::Ref(surface);
    };
}

void AddCanvasMethods(dx::IntrinsicClassBuilder& builder, const Context& context) {
    builder.FinalMethod("setKeepScreenOn", "(Z)V", [context](dx::IntrinsicContext& call) {
        // ROM posts KEEP_SCREEN_ON_MSG so the window keeps the display awake.
        // The host window already holds that for the whole session, so true is
        // satisfied only when the host reports it; false needs no host action.
        if (call.arguments[0].AsInt() != 0 && !context->host_display_sleep_inhibited) {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.surface_keep_screen_on", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "display wake inhibition is unavailable"};
        }
        return dx::VmValue::Void();
    });
    builder.FinalMethod("lockCanvas", "()Landroid/graphics/Canvas;", LockCanvasHandler(context));
    builder.FinalMethod("lockCanvas", "(Landroid/graphics/Rect;)Landroid/graphics/Canvas;", LockCanvasHandler(context));
    builder.FinalMethod("unlockCanvasAndPost", "(Landroid/graphics/Canvas;)V", UnlockCanvasAndPostHandler(context));
}

}  // namespace

Decl Declare_android_view_SurfaceHolder_Impl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/SurfaceHolder$Impl;", "Ljava/lang/Object;",
                                                    {"Landroid/view/SurfaceHolder;"});
    context->surface_holder_frame_field =
        builder.BoundInstanceField("mSurfaceFrame", "Landroid/graphics/Rect;", dx::kAccPrivate);
    {
        builder.FinalMethod("setFixedSize", "(II)V", [context](dx::IntrinsicContext& call) {
            SetManagedSurfaceRequest(call, *context, call.arguments[0].AsInt(), call.arguments[1].AsInt());
            return dx::VmValue::Void();
        });
        builder.FinalMethod("setSizeFromLayout", "()V", [context](dx::IntrinsicContext& call) {
            SetManagedSurfaceRequest(call, *context, 0, 0);
            return dx::VmValue::Void();
        });
    }
    builder.FinalMethod("getSurfaceFrame", "()Landroid/graphics/Rect;", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(GetSurfaceHolderFrame(call.vm, *context, call.receiver));
    });
    builder.FinalMethod("addCallback", "(Landroid/view/SurfaceHolder$Callback;)V",
                        SurfaceHolderAddCallbackHandler(context));
    builder.FinalMethod("removeCallback", "(Landroid/view/SurfaceHolder$Callback;)V",
                        SurfaceHolderRemoveCallbackHandler(context));
    builder.FinalMethod("setType", "(I)V", SurfaceHolderSetTypeHandler());
    builder.FinalMethod("setFormat", "(I)V", SurfaceHolderSetFormatHandler(context));
    builder.FinalMethod("getSurface", "()Landroid/view/Surface;", HolderGetSurface(context));
    AddCanvasMethods(builder, context);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_SurfaceHolder.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_SurfaceHolder(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/SurfaceHolder;");
    builder.UnimplementedVirtual("setKeepScreenOn", "(Z)V", dx::kAccPublic | dx::kAccAbstract);
    builder.UnimplementedVirtual("getSurfaceFrame", "()Landroid/graphics/Rect;", dx::kAccPublic | dx::kAccAbstract);
    builder.UnimplementedVirtual("getSurface", "()Landroid/view/Surface;", dx::kAccPublic | dx::kAccAbstract);
    builder.UnimplementedVirtual("addCallback", "(Landroid/view/SurfaceHolder$Callback;)V",
                                 dx::kAccPublic | dx::kAccAbstract);
    builder.UnimplementedVirtual("removeCallback", "(Landroid/view/SurfaceHolder$Callback;)V",
                                 dx::kAccPublic | dx::kAccAbstract);
    builder.UnimplementedVirtual("setType", "(I)V", dx::kAccPublic | dx::kAccAbstract);
    builder.UnimplementedVirtual("setFormat", "(I)V", dx::kAccPublic | dx::kAccAbstract);
    builder.UnimplementedVirtual("setFixedSize", "(II)V", dx::kAccPublic | dx::kAccAbstract);
    builder.UnimplementedVirtual("setSizeFromLayout", "()V", dx::kAccPublic | dx::kAccAbstract);
    builder.UnimplementedVirtual("lockCanvas", "()Landroid/graphics/Canvas;", dx::kAccPublic | dx::kAccAbstract);
    builder.UnimplementedVirtual("lockCanvas", "(Landroid/graphics/Rect;)Landroid/graphics/Canvas;",
                                 dx::kAccPublic | dx::kAccAbstract);
    builder.UnimplementedVirtual("unlockCanvasAndPost", "(Landroid/graphics/Canvas;)V",
                                 dx::kAccPublic | dx::kAccAbstract);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_SurfaceView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_SurfaceView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/SurfaceView;", "Landroid/view/View;");
    const auto window_visible = builder.BoundInstanceField("mWindowVisibility", "Z", 0);
    const auto view_visible = builder.BoundInstanceField("mViewVisibility", "Z", 0);
    const auto requested_visible = builder.BoundInstanceField("mRequestedVisible", "Z", 0);
    context->surface_view_visibility_fields = std::array{window_visible, view_visible, requested_visible};
    const auto parent_call = [](dx::IntrinsicContext& call, const char* name, const char* descriptor) {
        const auto type = call.vm.Linker().ResolveDescriptor("Landroid/view/View;");
        const auto index = call.vm.Linker().FindVtableIndex(type, name, descriptor);
        auto args = std::vector<dx::VmValue>{dx::VmValue::Ref(call.receiver)};
        args.insert(args.end(), call.arguments.begin(), call.arguments.end());
        const auto result = call.vm.Call(call.vm.Linker().Class(type).vtable.at(*index), args);
        if (result.exception.IsValid())
            throw dx::VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
        return result.value;
    };
    const auto visibility = [context, requested_visible](dx::IntrinsicContext& call,
                                                         std::optional<int> window = std::nullopt) {
        const bool previous = dx::IntrinsicCall(call).GetInt(requested_visible) != 0;
        const bool requested = RefreshSurfaceViewVisibility(call.vm, *context, call.receiver, window);
        if (previous != requested) static_cast<void>(CallAndroidMethod(call.vm, call.receiver, "requestLayout", "()V"));
        ReconcileSurfaceViewVisibility(call.vm, *context, call.receiver, requested);
    };
    {
        const auto width = builder.BoundInstanceField("mRequestedWidth", "I", 0);
        const auto height = builder.BoundInstanceField("mRequestedHeight", "I", 0);
        context->surface_view_requested_width_field = width;
        context->surface_view_requested_height_field = height;
        for (const auto descriptor :
             {"(Landroid/content/Context;)V", "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
              "(Landroid/content/Context;Landroid/util/AttributeSet;I)V"}) {
            builder.Constructor(
                descriptor, [context, width, height, descriptor, view_visible](dx::IntrinsicContext& call) {
                    const auto parent = call.vm.Linker().FindDirectMethod(
                        call.vm.Linker().ResolveDescriptor("Landroid/view/View;"), "<init>", descriptor);
                    auto args = std::vector<dx::VmValue>{dx::VmValue::Ref(call.receiver)};
                    args.insert(args.end(), call.arguments.begin(), call.arguments.end());
                    const auto result = call.vm.Call(*parent, args);
                    if (result.exception.IsValid())
                        throw dx::VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor,
                                              result.exception_message, result.exception};
                    dx::IntrinsicCall fields(call);
                    fields.SetInt(width, -1);
                    fields.SetInt(height, -1);
                    fields.SetInt(view_visible, 1);
                    static_cast<void>(SurfaceHolderForView(call.vm, *context, call.receiver));
                    static_cast<void>(
                        CallAndroidMethod(call.vm, call.receiver, "setWillNotDraw", "(Z)V", {dx::VmValue::Int(1)}));
                    return dx::VmValue::Void();
                });
        }
        builder.OverrideMethod(
            "onWindowVisibilityChanged", "(I)V",
            [parent_call, visibility](dx::IntrinsicContext& call) {
                parent_call(call, "onWindowVisibilityChanged", "(I)V");
                visibility(call, call.arguments[0].AsInt());
                return dx::VmValue::Void();
            },
            dx::kAccProtected);
        builder.OverrideMethod(
            "setFrame", "(IIII)Z",
            [](dx::IntrinsicContext& call) {
                const auto type = call.vm.Linker().ResolveDescriptor("Landroid/view/View;");
                const auto index = call.vm.Linker().FindVtableIndex(type, "setFrame", "(IIII)Z");
                const auto parent = call.vm.Linker().Class(type).vtable.at(*index);
                auto args = std::vector<dx::VmValue>{dx::VmValue::Ref(call.receiver)};
                args.insert(args.end(), call.arguments.begin(), call.arguments.end());
                const auto result = call.vm.Call(parent, args);
                if (result.exception.IsValid())
                    throw dx::VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor,
                                          result.exception_message, result.exception};
                return result.value;
            },
            dx::kAccProtected);
    }
    builder.OverrideMethod("setVisibility", "(I)V", [parent_call, visibility](dx::IntrinsicContext& call) {
        parent_call(call, "setVisibility", "(I)V");
        visibility(call);
        return dx::VmValue::Void();
    });
    builder.OverrideMethod(
        "onAttachedToWindow", "()V",
        [context, visibility](dx::IntrinsicContext& call) {
            // Managed attachment/observer merging replaces View AttachInfo and WMS.
            if (const auto node = FindViewUiNode(*context, call.receiver.Value()))
                static_cast<void>(AttachSurfaceViewSubtree(call.vm, *context, *node));
            visibility(call);
            return dx::VmValue::Void();
        },
        dx::kAccProtected);
    builder.OverrideMethod(
        "onDetachedFromWindow", "()V",
        [context, requested_visible](dx::IntrinsicContext& call) {
            dx::IntrinsicCall(call).SetInt(requested_visible, 0);
            ReconcileSurfaceViewVisibility(call.vm, *context, call.receiver, false);
            return dx::VmValue::Void();
        },
        dx::kAccProtected);
    for (const auto* name : {"draw", "dispatchDraw"}) {
        builder.OverrideMethod(
            name, "(Landroid/graphics/Canvas;)V",
            [parent_call, name](dx::IntrinsicContext& call) {
                // Hole composition already belongs to the managed compositor. The
                // parent Java Canvas drawing protocol is still unavailable.
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.surface_view_lifecycle", 0);
                return parent_call(call, name, "(Landroid/graphics/Canvas;)V");
            },
            std::string_view(name) == "draw" ? dx::kAccPublic : dx::kAccProtected);
    }
    builder.OverrideMethod(
        "gatherTransparentRegion", "(Landroid/graphics/Region;)Z", [context](dx::IntrinsicContext& call) {
            const auto node = FindViewUiNode(*context, call.receiver.Value());
            const bool skip = CallAndroidMethod(call.vm, call.receiver, "willNotDraw", "()Z").AsInt() != 0;
            if (call.arguments[0].ref.IsValid() || !skip || !node ||
                context->ui_tree.Get(*node)->surface_layer == ui::SurfaceLayer::on_top) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.surface_view_lifecycle", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "SurfaceView Region/parent gathering is unavailable"};
            }
            const auto holder = SurfaceHolderForView(call.vm, *context, call.receiver);
            const auto format = context->surface_requested_formats.find(holder.Value());
            return dx::VmValue::Int(
                !AndroidPixelFormatHasAlpha(format == context->surface_requested_formats.end() ? 1 : format->second));
        });
    builder.VirtualMethod("setSecure", "(Z)V", [](dx::IntrinsicContext& call) {
        if (call.arguments[0].AsInt()) {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.surface_view_lifecycle", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "Secure Surface display/capture exclusion is unavailable"};
        }
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("setWindowType", "(I)V", [context](dx::IntrinsicContext& call) {
        const auto type = call.arguments[0].AsInt();
        if (type != 1000 && type != 1001 && type != 1004) {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.surface_view_lifecycle", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "SurfaceView window type is unavailable"};
        }
        const auto node = FindViewUiNode(*context, call.receiver.Value());
        if (!node) throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "SurfaceView has no UI node"};
        context->ui_tree.Get(*node)->surface_layer = type == 1000   ? ui::SurfaceLayer::on_top
                                                     : type == 1004 ? ui::SurfaceLayer::media_overlay
                                                                    : ui::SurfaceLayer::media;
        context->ui_tree.MarkDrawDirty(*node);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("getHolder", "()Landroid/view/SurfaceHolder;", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(SurfaceHolderForView(call.vm, *context, call.receiver));
    });
    builder.VirtualMethod("setZOrderOnTop", "(Z)V", [context](dx::IntrinsicContext& call) {
        const auto node = FindViewUiNode(*context, call.receiver.Value());
        if (!node) throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "SurfaceView has no UI node"};
        context->ui_tree.Get(*node)->surface_layer =
            call.arguments[0].AsInt() != 0 ? ui::SurfaceLayer::on_top : ui::SurfaceLayer::media;
        context->ui_tree.MarkDrawDirty(*node);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("setZOrderMediaOverlay", "(Z)V", [context](dx::IntrinsicContext& call) {
        const auto node = FindViewUiNode(*context, call.receiver.Value());
        if (!node) throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "SurfaceView has no UI node"};
        context->ui_tree.Get(*node)->surface_layer =
            call.arguments[0].AsInt() != 0 ? ui::SurfaceLayer::media_overlay : ui::SurfaceLayer::media;
        context->ui_tree.MarkDrawDirty(*node);
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_View_OnClickListener.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_View_OnClickListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/View$OnClickListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- android.view.View.OnFocusChangeListener (API 19) ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm89_android_view_View_OnFocusChangeListener {

Decl Declare_android_view_View_OnFocusChangeListener(const Context& context) {
    static_cast<void>(context);
    // 当前只发布 API 19 接口形状与抽象方法引用，使 DEX implements 和
    // invoke-interface 能正常链接、分派。尚未实现 View 的完整焦点监听语义：
    // set/getOnFocusChangeListener、逐 View listener 保存与 GC tracing、
    // focused/focusable 状态、requestFocus/clearFocus 状态转换，以及焦点变化时
    // 自动调用 onFocusChange(View, boolean)。在这些能力闭合前不得伪造回调。
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/View$OnFocusChangeListener;");
    builder.UnimplementedVirtual("onFocusChange", "(Landroid/view/View;Z)V", dx::kAccPublic | dx::kAccAbstract);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics::dvm89_android_view_View_OnFocusChangeListener

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_View_OnFocusChangeListener(const Context& context) {
    return dvm89_android_view_View_OnFocusChangeListener::Declare_android_view_View_OnFocusChangeListener(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- android.view.View.OnSystemUiVisibilityChangeListener (API 19) ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm_system_ui_listener {

Decl Declare_android_view_View_OnSystemUiVisibilityChangeListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/View$OnSystemUiVisibilityChangeListener;");
    builder.UnimplementedVirtual("onSystemUiVisibilityChange", "(I)V", dx::kAccPublic | dx::kAccAbstract);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics::dvm_system_ui_listener

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_View_OnSystemUiVisibilityChangeListener(const Context& context) {
    return dvm_system_ui_listener::Declare_android_view_View_OnSystemUiVisibilityChangeListener(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_View_OnTouchListener.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_View_OnTouchListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/View$OnTouchListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- android.view.ViewParent (API 19 bounded type surface) ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_ViewParent(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/ViewParent;");
    builder.UnimplementedVirtual("getParent", "()Landroid/view/ViewParent;", dx::kAccPublic | dx::kAccAbstract);
    builder.UnimplementedVirtual("bringChildToFront", "(Landroid/view/View;)V", dx::kAccPublic | dx::kAccAbstract);
    {
        // The CPU fixture does not materialize the full skeleton. Declare the
        // original abstract interface symbols; real receivers supply ROM bodies.
        for (const auto* name : {"canResolveLayoutDirection", "isLayoutDirectionResolved", "canResolveTextDirection",
                                 "isTextDirectionResolved", "canResolveTextAlignment", "isTextAlignmentResolved"})
            builder.UnimplementedVirtual(name, "()Z", dx::kAccPublic | dx::kAccAbstract);
        for (const auto* name : {"clearChildFocus", "focusableViewAvailable", "childDrawableStateChanged"})
            builder.UnimplementedVirtual(name, "(Landroid/view/View;)V", dx::kAccPublic | dx::kAccAbstract);
        builder.UnimplementedVirtual("requestChildFocus", "(Landroid/view/View;Landroid/view/View;)V",
                                     dx::kAccPublic | dx::kAccAbstract);
        for (const auto* name : {"getLayoutDirection", "getTextDirection", "getTextAlignment"})
            builder.UnimplementedVirtual(name, "()I", dx::kAccPublic | dx::kAccAbstract);
    }
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_View.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

ui::LayoutParams ReadAndroidLayoutParams(dx::Interpreter& vm, const dx::VmObjectRef object) {
    if (!object.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "LayoutParams is null"};
    const auto root = vm.ProtectReferences(std::array{object});
    const auto type = vm.Model().ObjectClass(object);
    const auto is = [&](const char* descriptor) {
        return vm.Linker().IsAssignable(vm.Linker().ResolveDescriptor(descriptor), type);
    };
    if (!is("Landroid/view/ViewGroup$LayoutParams;"))
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "object is not LayoutParams"};
    const auto unsupported = [&](const char* message) -> void {
        if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.layout_params", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", message};
    };
    const auto bits = [&](const char* name, const char* descriptor) {
        const auto field = vm.Linker().FindFieldRecursive(type, name, descriptor);
        if (!field)
            throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                                 "LayoutParams field is missing: " + std::string(name));
        return vm.Model().InstanceSlots(object)[vm.Linker().Field(*field).slot].bits;
    };
    const auto integer = [&](const char* name) { return std::bit_cast<std::int32_t>(bits(name, "I")); };
    const auto dimension = [](std::int32_t value) -> ui::DimensionSpec {
        if (value == -1) return {ui::SizeMode::MatchParent, 0};
        if (value == -2) return {ui::SizeMode::WrapContent, 0};
        if (value >= 0) return {ui::SizeMode::Fixed, value};
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid layout dimension"};
    };
    ui::LayoutParams result;
    result.width = dimension(integer("width"));
    result.height = dimension(integer("height"));
    if (bits("layoutAnimationParameters", "Landroid/view/animation/LayoutAnimationController$AnimationParameters;") !=
        0)
        unsupported("layout animation parameters are unsupported");
    if (is("Landroid/view/ViewGroup$MarginLayoutParams;")) {
        // The current UI traversal is LTR. Resolve start/end using API 19 Java.
        static_cast<void>(CallAndroidMethod(vm, object, "resolveLayoutDirection", "(I)V", {dx::VmValue::Int(0)}));
        result.margin = {integer("leftMargin"), integer("topMargin"), integer("rightMargin"), integer("bottomMargin")};
    }
    if (is("Landroid/widget/FrameLayout$LayoutParams;") || is("Landroid/widget/LinearLayout$LayoutParams;")) {
        const auto gravity = integer("gravity");
        if (gravity != -1) result.layout_gravity = static_cast<std::uint32_t>(gravity);
    }
    if (is("Landroid/widget/LinearLayout$LayoutParams;")) {
        result.weight = std::bit_cast<float>(bits("weight", "F"));
        if (!std::isfinite(result.weight) || result.weight < 0.0F)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid layout weight"};
    }
    if (is("Landroid/widget/RelativeLayout$LayoutParams;")) {
        if (bits("alignWithParent", "Z") != 0) unsupported("RelativeLayout alignWithParent is unsupported");
        const auto rules = CallAndroidMethod(vm, object, "getRules", "(I)[I", {dx::VmValue::Int(0)}).ref;
        const auto rule = [&](int index) {
            return static_cast<std::int32_t>(vm.Model().GetPrimitiveElement(rules, index));
        };
        if (rule(4) != 0) unsupported("RelativeLayout ALIGN_BASELINE is unsupported");
        const auto sibling = [&](int index) -> std::optional<std::int32_t> {
            const auto id = rule(index);
            // RelativeLayout.LayoutParams.addRule(verb) stores TRUE (-1);
            // the AOSP dependency graph only honours rule > 0, so a TRUE or
            // otherwise non-positive sibling anchor behaves as "no anchor".
            if (id <= 0) return std::nullopt;
            return id;
        };
        auto& r = result.relative;
        r.left_of = sibling(0);
        r.right_of = sibling(1);
        r.above = sibling(2);
        r.below = sibling(3);
        r.align_left = sibling(5);
        r.align_top = sibling(6);
        r.align_right = sibling(7);
        r.align_bottom = sibling(8);
        r.align_parent_left = rule(9) != 0;
        r.align_parent_top = rule(10) != 0;
        r.align_parent_right = rule(11) != 0;
        r.align_parent_bottom = rule(12) != 0;
        r.center_in_parent = rule(13) != 0;
        r.center_horizontal = rule(14) != 0;
        r.center_vertical = rule(15) != 0;
    }
    return result;
}

namespace {

ui::UiNodeId ViewNode(dx::IntrinsicContext& call, const Context& context) {
    return EnsureViewUiNode(*context, call.receiver, UiClassForObject(call.vm, call.receiver));
}

}  // namespace

Decl Declare_android_view_View(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/View;", "Ljava/lang/Object;",
                                                    {"Landroid/graphics/drawable/Drawable$Callback;",
                                                     "Landroid/view/KeyEvent$Callback;",
                                                     "Landroid/view/accessibility/AccessibilityEventSource;"});

    builder.VirtualMethod("post", "(Ljava/lang/Runnable;)Z", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(PostViewRunnable(call, context, call.receiver, call.arguments[0].ref, 0));
    });
    builder.VirtualMethod("postDelayed", "(Ljava/lang/Runnable;J)Z", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(
            PostViewRunnable(call, context, call.receiver, call.arguments[0].ref, call.arguments[1].AsLong()));
    });
    builder.VirtualMethod("dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z", [context](dx::IntrinsicContext& call) {
        const auto owner = context->view_adapter_references[call.receiver.Value()].window_owner;
        if (owner.IsValid())
            return CallAndroidMethod(call.vm, owner, "dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z",
                                     {call.arguments[0]});
        return dx::VmValue::Int(
            DispatchAndroidViewTouch(call.vm, *context, call.receiver, call.arguments[0].ref, false));
    });
    builder.ConstantInt("SYSTEM_UI_FLAG_VISIBLE", "I", 0, dx::kAccPublic)
        .ConstantInt("SYSTEM_UI_FLAG_LOW_PROFILE", "I", 0x00000001, dx::kAccPublic)
        .ConstantInt("SYSTEM_UI_FLAG_HIDE_NAVIGATION", "I", 0x00000002, dx::kAccPublic)
        .ConstantInt("SYSTEM_UI_FLAG_FULLSCREEN", "I", 0x00000004, dx::kAccPublic)
        .ConstantInt("SYSTEM_UI_FLAG_LAYOUT_STABLE", "I", 0x00000100, dx::kAccPublic)
        .ConstantInt("SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION", "I", 0x00000200, dx::kAccPublic)
        .ConstantInt("SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN", "I", 0x00000400, dx::kAccPublic)
        .ConstantInt("SYSTEM_UI_FLAG_IMMERSIVE", "I", 0x00000800, dx::kAccPublic)
        .ConstantInt("SYSTEM_UI_FLAG_IMMERSIVE_STICKY", "I", 0x00001000, dx::kAccPublic);
    const auto system_ui_visibility = builder.BoundInstanceField("mSystemUiVisibility", "I", dx::kAccNone);
    const auto view_flags = builder.BoundInstanceField("mViewFlags", "I", dx::kAccNone);
    context->view_java_input_fields =
        std::array{builder.BoundInstanceField("mListenerInfo", "Landroid/view/View$ListenerInfo;", dx::kAccNone),
                   builder.BoundInstanceField("mBackground", "Landroid/graphics/drawable/Drawable;", dx::kAccPrivate)};
    {
        const auto background_changed = builder.BoundInstanceField("mBackgroundSizeChanged", "Z", dx::kAccPrivate);
        builder.VirtualMethod(
            "onScrollChanged", "(IIII)V",
            [=](dx::IntrinsicContext& c) {
                dx::IntrinsicCall fields(c);
                if (!context->UiAccessibilityUnavailable() ||
                    fields.GetRef(context->view_java_measure_fields->unsupported_state[4]).IsValid()) {
                    if (auto* ledger = c.vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_scroll_notification", 0);
                    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                          "Attached/accessibility scroll notification unsupported"};
                }
                fields.SetInt(background_changed, 1);
                return dx::VmValue::Void();
            },
            dx::kAccProtected);
    }
    context->view_java_hierarchy_fields = DexVmAndroidContext::ViewJavaHierarchyFields{
        builder.BoundInstanceField("mParent", "Landroid/view/ViewParent;", dx::kAccNone),
        builder.BoundInstanceField("mLayoutParams", "Landroid/view/ViewGroup$LayoutParams;", dx::kAccNone),
        builder.BoundInstanceField("mID", "I", dx::kAccNone),
        builder.BoundInstanceField("mLabelForId", "I", dx::kAccPrivate)};
    context->view_java_measure_fields = DexVmAndroidContext::ViewJavaMeasureFields{
        {builder.BoundInstanceField("mMeasuredWidth", "I", dx::kAccPrivate),
         builder.BoundInstanceField("mMeasuredHeight", "I", dx::kAccPrivate)},
        {builder.BoundInstanceField("mOldWidthMeasureSpec", "I", dx::kAccNone),
         builder.BoundInstanceField("mOldHeightMeasureSpec", "I", dx::kAccNone)},
        {builder.BoundInstanceField("mPrivateFlags", "I", dx::kAccNone),
         builder.BoundInstanceField("mPrivateFlags2", "I", dx::kAccNone),
         builder.BoundInstanceField("mPrivateFlags3", "I", dx::kAccNone)},
        builder.BoundInstanceField("mMeasureCache", "Landroid/util/LongSparseLongArray;", dx::kAccPrivate),
        view_flags,
        {builder.BoundInstanceField("mLeft", "I", dx::kAccProtected),
         builder.BoundInstanceField("mTop", "I", dx::kAccProtected),
         builder.BoundInstanceField("mRight", "I", dx::kAccProtected),
         builder.BoundInstanceField("mBottom", "I", dx::kAccProtected)},
        {builder.BoundInstanceField("mScrollX", "I", dx::kAccProtected),
         builder.BoundInstanceField("mScrollY", "I", dx::kAccProtected)},
        {builder.BoundInstanceField("mDisplayList", "Landroid/view/DisplayList;", dx::kAccNone),
         builder.BoundInstanceField("mTransformationInfo", "Landroid/view/View$TransformationInfo;", dx::kAccNone),
         builder.BoundInstanceField("mOverlay", "Landroid/view/ViewOverlay;", dx::kAccNone),
         builder.BoundInstanceField("mCurrentAnimation", "Landroid/view/animation/Animation;", dx::kAccNone),
         builder.BoundInstanceField("mAttachInfo", "Landroid/view/View$AttachInfo;", dx::kAccNone),
         builder.BoundInstanceField("mAccessibilityDelegate", "Landroid/view/View$AccessibilityDelegate;",
                                    dx::kAccNone)}};
    builder.VirtualMethod(
        "unscheduleDrawable", "(Landroid/graphics/drawable/Drawable;)V", [context](dx::IntrinsicContext& call) {
            // API19 has no queued callback to remove without a real AttachInfo.
            if (!call.arguments[0].ref.IsValid() ||
                !dx::IntrinsicCall(call).GetRef(context->view_java_measure_fields->unsupported_state[4]).IsValid())
                return dx::VmValue::Void();
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.drawable_resource", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "attached Drawable scheduling unsupported"};
        });
    builder.DirectMethod(
        "hasRtlSupport", "()Z",
        [context](dx::IntrinsicContext& call) {
            const auto owner = dx::IntrinsicCall(call).GetRef(*context->view_context_field);
            const auto roots = call.vm.ProtectReferences(std::array{call.receiver, owner});
            const auto info =
                CallAndroidMethod(call.vm, owner, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;").ref;
            return CallAndroidMethod(call.vm, info, "hasRtlSupport", "()Z");
        },
        dx::kAccPrivate);
    builder.StaticMethod("isLayoutModeOptical", "(Ljava/lang/Object;)Z", [context](dx::IntrinsicContext& call) {
        const auto object = call.arguments[0].ref;
        if (object.IsValid() &&
            call.vm.Linker().IsAssignable(call.vm.Linker().ResolveDescriptor("Landroid/view/ViewGroup;"),
                                          call.vm.Model().ObjectClass(object))) {
            return CallAndroidMethod(call.vm, object, "isLayoutModeOptical", "()Z");
        }
        return dx::VmValue::Int(0);
    });
    context->view_context_field =
        builder.BoundInstanceField("mContext", "Landroid/content/Context;", dx::kAccProtected);
    context->view_java_layout_fields = DexVmAndroidContext::ViewJavaLayoutFields{
        builder.BoundInstanceField("mResources", "Landroid/content/res/Resources;", dx::kAccPrivate | dx::kAccFinal),
        {builder.BoundInstanceField("mPaddingLeft", "I", dx::kAccProtected),
         builder.BoundInstanceField("mPaddingTop", "I", dx::kAccProtected),
         builder.BoundInstanceField("mPaddingRight", "I", dx::kAccProtected),
         builder.BoundInstanceField("mPaddingBottom", "I", dx::kAccProtected)},
        {builder.BoundInstanceField("mMinWidth", "I", dx::kAccPrivate),
         builder.BoundInstanceField("mMinHeight", "I", dx::kAccPrivate)},
        {builder.BoundInstanceField("mUserPaddingLeft", "I", dx::kAccProtected),
         builder.BoundInstanceField("mUserPaddingRight", "I", dx::kAccProtected),
         builder.BoundInstanceField("mUserPaddingBottom", "I", dx::kAccProtected),
         builder.BoundInstanceField("mUserPaddingStart", "I", dx::kAccNone),
         builder.BoundInstanceField("mUserPaddingEnd", "I", dx::kAccNone),
         builder.BoundInstanceField("mUserPaddingLeftInitial", "I", dx::kAccNone),
         builder.BoundInstanceField("mUserPaddingRightInitial", "I", dx::kAccNone)},
        {builder.BoundInstanceField("mLeftPaddingDefined", "Z", dx::kAccPrivate),
         builder.BoundInstanceField("mRightPaddingDefined", "Z", dx::kAccPrivate)}};
    // Absolute padding only. ROM left/right getters require the unselected
    // RTL resolution state machine; these adapters read the Java authority.
    builder.VirtualMethod("getPaddingLeft", "()I", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(ReadAndroidViewPadding(call.vm, *context, call.receiver).left);
    });
    builder.VirtualMethod("getPaddingRight", "()I", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(ReadAndroidViewPadding(call.vm, *context, call.receiver).right);
    });
    for (const auto width : {true, false}) {
        builder.VirtualMethod(width ? "setMinimumWidth" : "setMinimumHeight", "(I)V",
                              [context, width](dx::IntrinsicContext& call) {
                                  auto minimum = ReadAndroidViewMinimum(call.vm, *context, call.receiver);
                                  (width ? minimum.width : minimum.height) = call.arguments[0].AsInt();
                                  WriteAndroidViewMinimum(call.vm, *context, call.receiver, minimum);
                                  return CallAndroidMethod(call.vm, call.receiver, "requestLayout", "()V");
                              });
    }
    context->view_compatibility_fields =
        std::array{builder.BoundStaticField("sCompatibilityDone", "Z", dx::kAccPrivate),
                   builder.BoundStaticField("sUseBrokenMakeMeasureSpec", "Z", dx::kAccPrivate),
                   builder.BoundStaticField("sIgnoreMeasureCache", "Z", dx::kAccPrivate)};
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.FinalMethod("getContext", "()Landroid/content/Context;", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(dx::IntrinsicCall(call).GetRef(*context->view_context_field));
    });

    const auto managed_attached = [context](dx::IntrinsicContext& call) {
        dx::IntrinsicCall fields(call);
        auto view = call.receiver;
        for (std::size_t depth = 0; view.IsValid() && depth <= ui::UiTree::kMaxDepth; ++depth) {
            if (view == AndroidUiTraversalRoot(call.vm, *context) ||
                AndroidApplicationWindowRoot(call.vm, *context, view).IsValid())
                return true;
            const auto node = FindViewUiNode(*context, view.Value());
            for (const auto& record : context->activity_stack)
                if (node && record.window_root == node) return true;
            view = fields.GetRef(context->view_java_hierarchy_fields->parent, view);
        }
        return false;
    };
    builder.VirtualMethod("getWindowVisibility", "()I", [context, managed_attached](dx::IntrinsicContext& call) {
        if (!managed_attached(call)) return dx::VmValue::Int(8);
        const auto application_root = AndroidApplicationWindowRoot(call.vm, *context, call.receiver);
        for (const auto& window : context->application_windows) {
            if (window.removed || window.root != application_root) continue;
            for (const auto& record : context->activity_stack)
                if (record.object == window.owner) return dx::VmValue::Int(record.visible ? 0 : 4);
        }
        {
            dx::IntrinsicCall fields(call);
            auto view = call.receiver;
            for (std::size_t depth = 0; view.IsValid() && depth <= ui::UiTree::kMaxDepth; ++depth) {
                const auto node = FindViewUiNode(*context, view.Value());
                for (const auto& record : context->activity_stack)
                    if (node && record.window_root == node) return dx::VmValue::Int(record.visible ? 0 : 4);
                view = fields.GetRef(context->view_java_hierarchy_fields->parent, view);
            }
            return dx::VmValue::Int(0);
        }
    });

    builder.VirtualMethod("hasWindowFocus", "()Z", [context, managed_attached](dx::IntrinsicContext& call) {
        const auto window = AndroidApplicationWindowRoot(call.vm, *context, call.receiver);
        if (window.IsValid()) return dx::VmValue::Int(context->focused_application_window == window ? 1 : 0);
        if (!managed_attached(call) || !context->window_has_focus.load()) {
            return dx::VmValue::Int(0);
        }
        const auto owner = context->view_adapter_references[call.receiver.Value()].window_owner;
        if (owner.IsValid()) return dx::VmValue::Int(context->window_focus_activity.load() == owner.Value() ? 1 : 0);
        const auto node = FindViewUiNode(*context, call.receiver.Value());
        return dx::VmValue::Int(node.has_value() && context->ui_tree.IsAttached(*node));
    });

    builder.VirtualMethod("onTouchEvent", "(Landroid/view/MotionEvent;)Z", [context](dx::IntrinsicContext& call) {
        const auto node = EnsureViewUiNode(*context, call.receiver, UiClassForObject(call.vm, call.receiver));
        return dx::VmValue::Int(context->ui_tree.Get(node)->clickable ? 1 : 0);
    });
    const auto unhandled_key = dx::IntrinsicHandler([](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });

    const auto dispatch_focus = [context](dx::IntrinsicContext& call, const dx::VmObjectRef view, const bool focused) {
        if (!view.IsValid()) return;
        const auto listener = context->view_adapter_references[view.Value()].focus_listener;
        if (!listener.IsValid()) return;
        static_cast<void>(CallAndroidMethod(call.vm, listener, "onFocusChange", "(Landroid/view/View;Z)V",
                                            {dx::VmValue::Ref(view), dx::VmValue::Int(focused ? 1 : 0)}));
    };

    const auto request_focus = dx::IntrinsicHandler([context, dispatch_focus](dx::IntrinsicContext& call) {
        const auto node = ViewNode(call, context);
        const auto previous = context->ui_tree.Focused();
        if (!context->ui_tree.RequestFocus(node, false)) {
            return dx::VmValue::Int(0);
        }
        if (previous.has_value() && *previous != node) {
            dispatch_focus(call, ViewObjectForUiNode(*context, *previous), false);
        }
        if (previous != node) dispatch_focus(call, call.receiver, true);
        return dx::VmValue::Int(1);
    });

    const auto scroll_bar_enabled = [context](const bool horizontal) {
        return dx::IntrinsicHandler([context, horizontal](dx::IntrinsicContext& call) {
            auto* state = context->ui_tree.Get(ViewNode(call, context));
            auto& enabled = horizontal ? state->horizontal_scroll_bar_enabled : state->vertical_scroll_bar_enabled;
            if (!call.arguments.empty()) {
                const auto current =
                    CallAndroidMethod(call.vm, call.receiver,
                                      horizontal ? "isHorizontalScrollBarEnabled" : "isVerticalScrollBarEnabled", "()Z")
                        .AsInt() != 0;
                if (current == (call.arguments[0].AsInt() != 0)) return dx::VmValue::Void();
                auto* current_state = context->ui_tree.Get(ViewNode(call, context));
                (horizontal ? current_state->horizontal_scroll_bar_enabled
                            : current_state->vertical_scroll_bar_enabled) = call.arguments[0].AsInt() != 0;
                return dx::VmValue::Void();
            }
            return dx::VmValue::Int(enabled ? 1 : 0);
        });
    };

    const auto invalidate = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto node = EnsureViewUiNode(*context, call.receiver, ui::UiClass::View);
        const auto application_root = AndroidApplicationWindowRoot(call.vm, *context, call.receiver);
        if (!application_root.IsValid() && AndroidViewBelongsToRoot(call.vm, *context, call.receiver))
            context->ui_window_schedules[AndroidUiTraversalRoot(call.vm, *context)].published.reset();
        if (application_root.IsValid()) context->ui_window_schedules[application_root].published.reset();
        context->ui_tree.MarkDrawDirty(node);
        return dx::VmValue::Void();
    });
    const auto software_dirty = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        if (call.vm.CurrentContextToken() != 1U || context->ui_java_owner_thread != std::this_thread::get_id())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "View invalidation requires owner thread"};
        dx::IntrinsicCall fields(call);
        const auto& tokens = *context->view_java_measure_fields;
        bool unsupported = (fields.GetInt(tokens.flags[0]) & 0x40) != 0;  // PFLAG_DRAW_ANIMATION
        for (std::size_t i = 0; i < tokens.unsupported_state.size(); ++i)
            if (i != 1) unsupported = unsupported || fields.GetRef(tokens.unsupported_state[i]).IsValid();
        RequireAndroidAlphaOnly(call.vm, *context, call.receiver);
        if (unsupported) {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_layout_system_state", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "View hardware, animation or attached system state is not supported"};
        }
        context->ui_tree.MarkDrawDirty(EnsureViewUiNode(*context, call.receiver, ui::UiClass::View));
        auto root = AndroidApplicationWindowRoot(call.vm, *context, call.receiver);
        if (!root.IsValid() && AndroidViewBelongsToRoot(call.vm, *context, call.receiver))
            root = AndroidUiTraversalRoot(call.vm, *context);
        if (root.IsValid()) context->ui_window_schedules[root].draw_requested = true;
        return dx::VmValue::Void();
    });
    const auto invalidate_software = dx::IntrinsicHandler([context, software_dirty](dx::IntrinsicContext& call) {
        static_cast<void>(software_dirty(call));
        dx::IntrinsicCall fields(call);
        const auto token = context->view_java_measure_fields->flags[0];
        auto flags = (fields.GetInt(token) & ~0x20) | 0x00200000;  // DRAWN -> DIRTY
        if (call.arguments.size() != 1 || call.arguments[0].AsInt() != 0)
            flags = (flags & ~0x8000) | static_cast<std::int32_t>(0x80000000U);
        fields.SetInt(token, flags);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("invalidate", "(Z)V", invalidate_software, dx::kAccNone);
    builder.VirtualMethod("invalidate", "(IIII)V", invalidate_software);
    builder.DirectMethod(
        "updateMatrix", "()V",
        [context](dx::IntrinsicContext& call) {
            if (!AndroidAlphaOnly(call.vm, *context, call.receiver)) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_layout_transform", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "Transformed layout is not supported"};
            }
            return dx::VmValue::Void();
        },
        dx::kAccPrivate);
    builder.VirtualMethod("notifySubtreeAccessibilityStateChangedIfNeeded", "()V", software_dirty);
    {
        builder.VirtualMethod("onWindowFocusChanged", "(Z)V", [context, software_dirty](dx::IntrinsicContext& call) {
            static_cast<void>(software_dirty(call));
            if (!call.arguments[0].AsInt()) {
                dx::IntrinsicCall fields(call);
                const auto flags = context->view_java_measure_fields->flags[0];
                // PFLAG_PRESSED / PFLAG_PREPRESSED: managed input has no IME or long-press timer.
                fields.SetInt(flags, fields.GetInt(flags) & ~0x02004000);
                static_cast<void>(CallAndroidMethod(call.vm, call.receiver, "cancelPendingInputEvents", "()V"));
                static_cast<void>(CallAndroidMethod(call.vm, call.receiver, "onFocusLost", "()V"));
            }
            static_cast<void>(CallAndroidMethod(call.vm, call.receiver, "refreshDrawableState", "()V"));
            return dx::VmValue::Void();
        });
        builder.VirtualMethod("needGlobalAttributesUpdate", "(Z)V", software_dirty, dx::kAccNone);
        builder.DirectMethod(
            "initialAwakenScrollBars", "()Z",
            [](dx::IntrinsicContext& call) -> dx::VmValue {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_attached_scrollbars", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "Attached scrollbars unsupported"};
            },
            dx::kAccPrivate);
        builder.VirtualMethod("clearAccessibilityFocus", "()V", [context, software_dirty](dx::IntrinsicContext& call) {
            static_cast<void>(software_dirty(call));
            if (dx::IntrinsicCall(call).GetInt(context->view_java_measure_fields->flags[1]) & 0x04000000) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_accessibility_focus", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "Accessibility focus unsupported"};
            }
            return dx::VmValue::Void();
        });
        builder.VirtualMethod("isInTouchMode", "()Z", [context](dx::IntrinsicContext& call) {
            if (dx::IntrinsicCall(call).GetRef(context->view_java_measure_fields->unsupported_state[4]).IsValid()) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_touch_mode", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "System touch mode unsupported"};
            }
            return dx::VmValue::Int(0);
        });
        builder.VirtualMethod(
            "onCreateDrawableState", "(I)[I",
            [context](dx::IntrinsicContext& c) {
                dx::IntrinsicCall fields(c);
                RequireAndroidAlphaOnly(c.vm, *context, c.receiver);
                for (std::size_t i = 0; i < context->view_java_measure_fields->unsupported_state.size(); ++i)
                    if (i != 1 && fields.GetRef(context->view_java_measure_fields->unsupported_state[i]).IsValid()) {
                        if (auto* ledger = c.vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_drawable_state", 0);
                        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                              "system-attached drawable state unsupported"};
                    }
                const auto extra = c.arguments[0].AsInt();
                if (extra < 0 || extra > 1024)
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "drawable extraSpace outside bounds"};
                const auto flags = fields.GetInt(context->view_java_measure_fields->flags[0]);
                const auto flags2 = fields.GetInt(context->view_java_measure_fields->flags[1]);
                const auto view_flags = fields.GetInt(context->view_java_measure_fields->view_flags);
                const auto parent = fields.GetRef(context->view_java_hierarchy_fields->parent);
                const auto klass = c.vm.Linker().ResolveDescriptor("Landroid/view/View;");
                if ((view_flags & 0x400000) && parent.IsValid() &&
                    c.vm.Linker().IsAssignable(klass, c.vm.Model().ObjectClass(parent)))
                    return CallAndroidMethod(c.vm, parent, "onCreateDrawableState", "(I)[I", {c.arguments[0]});
                std::uint32_t index = 0;
                const auto bit = [&](const char* name, bool active) {
                    if (!active) return;
                    const auto f = c.vm.Linker().FindFieldRecursive(klass, name, "I");
                    index |= c.vm.Linker().Class(klass).static_storage[c.vm.Linker().Field(*f).slot];
                };
                bit("VIEW_STATE_PRESSED", (flags & 0x4000) != 0);
                bit("VIEW_STATE_ENABLED", (view_flags & 0x20) == 0);
                bit("VIEW_STATE_FOCUSED", CallAndroidMethod(c.vm, c.receiver, "isFocused", "()Z").AsInt() != 0);
                bit("VIEW_STATE_SELECTED", (flags & 4) != 0);
                bit("VIEW_STATE_WINDOW_FOCUSED",
                    CallAndroidMethod(c.vm, c.receiver, "hasWindowFocus", "()Z").AsInt() != 0);
                bit("VIEW_STATE_ACTIVATED", (flags & 0x40000000) != 0);
                bit("VIEW_STATE_HOVERED", (flags & 0x10000000) != 0);
                bit("VIEW_STATE_DRAG_CAN_ACCEPT", (flags2 & 1) != 0);
                bit("VIEW_STATE_DRAG_HOVERED", (flags2 & 2) != 0);
                const auto f = c.vm.Linker().FindFieldRecursive(klass, "VIEW_STATE_SETS", "[[I");
                const dx::VmObjectRef sets{c.vm.Linker().Class(klass).static_storage[c.vm.Linker().Field(*f).slot]};
                const auto state = c.vm.Model().GetObjectElement(sets, static_cast<JniSize>(index));
                if (!extra) return dx::VmValue::Ref(state);
                const auto length = c.vm.Model().ArrayLength(state);
                const auto copy = c.vm.Model().NewPrimitiveArray(c.vm.Linker().ResolveDescriptor("[I"),
                                                                 JniPrimitiveKind::integer, length + extra);
                c.vm.Model().CopyPrimitiveElements(state, 0, copy, 0, length);
                return dx::VmValue::Ref(copy);
            },
            dx::kAccProtected);
        builder.VirtualMethod(
            "drawableStateChanged", "()V",
            [context, software_dirty](dx::IntrinsicContext& call) {
                static_cast<void>(software_dirty(call));
                dx::IntrinsicCall fields(call);
                const auto background = fields.GetRef((*context->view_java_input_fields)[1]);
                if (background.IsValid()) {
                    if (!CallAndroidMethod(call.vm, background, "isStateful", "()Z").AsInt())
                        return dx::VmValue::Void();
                    const auto state = context->ui_drawables.find(background.Value());
                    if (state != context->ui_drawables.end() && state->second.rom_skin) {
                        const auto states = CallAndroidMethod(call.vm, call.receiver, "getDrawableState", "()[I").ref;
                        static_cast<void>(
                            CallAndroidMethod(call.vm, background, "setState", "([I)Z", {dx::VmValue::Ref(states)}));
                        return dx::VmValue::Void();
                    }
                    if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_drawable_state", 0);
                    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "Drawable state unsupported"};
                }
                return dx::VmValue::Void();
            },
            dx::kAccProtected);
        builder.VirtualMethod(
            "onFocusChanged", "(ZILandroid/graphics/Rect;)V",
            [context, software_dirty](dx::IntrinsicContext& call) {
                if (!context->UiAccessibilityUnavailable()) {
                    if (auto* ledger = call.vm.Ledger())
                        ledger->RecordUnimplemented("dexvm.view_accessibility_provider", 0);
                    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                          "Focus requires configured test accessibility provider"};
                }
                static_cast<void>(software_dirty(call));
                dx::IntrinsicCall fields(call);
                if (fields.GetInt(context->view_java_measure_fields->flags[0]) & 0x4000) {
                    if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_pressed_focus", 0);
                    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                          "Pressed focus callbacks unsupported"};
                }
                const auto listener =
                    ReadAndroidViewListener(call.vm, *context, call.receiver, AndroidViewListener::FocusChange);
                if (listener.IsValid())
                    static_cast<void>(CallAndroidMethod(call.vm, listener, "onFocusChange", "(Landroid/view/View;Z)V",
                                                        {dx::VmValue::Ref(call.receiver), call.arguments[0]}));
                return dx::VmValue::Void();
            },
            dx::kAccProtected);
        builder.VirtualMethod(
            "onCancelPendingInputEvents", "()V", [context, software_dirty](dx::IntrinsicContext& call) {
                static_cast<void>(software_dirty(call));
                dx::IntrinsicCall fields(call);
                const auto flags = context->view_java_measure_fields->flags[2];
                fields.SetInt(flags, fields.GetInt(flags) | 0x10);  // PFLAG3_CALLED_SUPER
                auto captured = dx::VmObjectRef{static_cast<std::uint32_t>(context->ui_touch_capture)};
                for (std::size_t depth = 0; captured.IsValid() && depth <= ui::UiTree::kMaxDepth; ++depth) {
                    if (captured == call.receiver) {
                        context->ui_touch_capture = 0;
                        context->ui_touch_targets.clear();
                        context->ui_touch_clicks.clear();
                        break;
                    }
                    captured = fields.GetRef(context->view_java_hierarchy_fields->parent, captured);
                }
                return dx::VmValue::Void();
            });
        builder.VirtualMethod("isInLayout", "()Z", [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(context->ui_java_layout_pass != 0 &&
                                    AndroidViewBelongsToRoot(call.vm, *context, call.receiver));
        });
    }

    builder.VirtualMethod("requestLayout", "()V", [context](dx::IntrinsicContext& call) {
        {
            if (call.vm.CurrentContextToken() != 1U || context->ui_java_owner_thread != std::this_thread::get_id())
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "View measure requests require owner thread"};
            dx::IntrinsicCall fields(call);
            const auto& tokens = *context->view_java_measure_fields;
            const auto cache = fields.GetRef(tokens.cache);
            if (cache.IsValid()) static_cast<void>(CallAndroidMethod(call.vm, cache, "clear", "()V"));
            const bool attached = AndroidViewBelongsToRoot(call.vm, *context, call.receiver);
            const auto application_root = AndroidApplicationWindowRoot(call.vm, *context, call.receiver);
            if (!context->ui_java_propagating_request) {
                if (application_root.IsValid())
                    context->ui_window_schedules[application_root].requests.insert(call.receiver);
                else if (attached)
                    AndroidUiMeasureRequests(call.vm, *context).insert(call.receiver);
            }
            if (attached && context->ui_java_layout_pass == 2) return dx::VmValue::Void();
            fields.SetInt(tokens.flags[0], fields.GetInt(tokens.flags[0]) | static_cast<std::int32_t>(0x80001000U));
            const auto parent = fields.GetRef(context->view_java_hierarchy_fields->parent);
            if (parent.IsValid() && !CallAndroidMethod(call.vm, parent, "isLayoutRequested", "()Z").AsInt()) {
                const auto previous = context->ui_java_propagating_request;
                context->ui_java_propagating_request = true;
                struct Restore {
                    bool& value;
                    bool previous;
                    ~Restore() { value = previous; }
                } restore{context->ui_java_propagating_request, previous};
                static_cast<void>(CallAndroidMethod(call.vm, parent, "requestLayout", "()V"));
            }
            return dx::VmValue::Void();
        }
    });

    const auto geometry = [context](const auto member) {
        return dx::IntrinsicHandler([context, member](dx::IntrinsicContext& call) {
            const auto node = ViewNode(call, context);
            EnsureAndroidUiTraversal(call.vm, *context, AndroidUiTraversalReason::GeometryRead,
                                     AndroidUiTraversalPhase::ResolveOnly);
            return dx::VmValue::Int(member(*context->ui_tree.Get(node)));
        });
    };

    const auto location = [context](dx::IntrinsicContext& call) {
        if (!call.arguments[0].ref.IsValid())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "location requires two entries"};
        const auto output = dx::IntrinsicCall(call).NonNullRef(0, "location");
        if (call.vm.Model().ArrayLength(output) < 2)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "location requires two entries"};
        {
            const auto geometry = ReadAndroidUiGeometry(call.vm, *context);
            const auto node = FindViewUiNode(*context, call.receiver.Value());
            if (!node || !geometry->IsAttached(*node) || !AndroidViewBelongsToRoot(call.vm, *context, call.receiver)) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_window_coordinates", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "Coordinates require managed attachment"};
            }
            dx::IntrinsicCall fields(call);
            auto ancestor = call.receiver;
            for (std::size_t depth = 0; ancestor.IsValid() && depth <= ui::UiTree::kMaxDepth; ++depth) {
                if (!AndroidAlphaOnly(call.vm, *context, ancestor)) {
                    if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_window_transform", 0);
                    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                          "Transformed window coordinates unsupported"};
                }
                ancestor = fields.GetRef(context->view_java_hierarchy_fields->parent, ancestor);
            }
            const auto& frame = geometry->Get(*node)->screen_frame;
            call.vm.Model().SetPrimitiveElement(output, 0, static_cast<std::uint32_t>(frame.left));
            call.vm.Model().SetPrimitiveElement(output, 1, static_cast<std::uint32_t>(frame.top));
        }
        return dx::VmValue::Void();
    };
    builder.VirtualMethod("getLocationInWindow", "([I)V", location);
    builder.VirtualMethod("getLocationOnScreen", "([I)V", location);

    builder.VirtualMethod(
        "setSystemUiVisibility", "(I)V", [context, system_ui_visibility, managed_attached](dx::IntrinsicContext& call) {
            dx::IntrinsicCall fields(call);
            const auto value = call.arguments[0].AsInt();
            {
                if (fields.GetInt(system_ui_visibility) == value) return dx::VmValue::Void();
                fields.SetInt(system_ui_visibility, value);
                if (managed_attached(call)) {
                    const auto parent = fields.GetRef(context->view_java_hierarchy_fields->parent);
                    if (parent.IsValid())
                        static_cast<void>(CallAndroidMethod(call.vm, parent, "recomputeViewAttributes",
                                                            "(Landroid/view/View;)V",
                                                            {dx::VmValue::Ref(call.receiver)}));
                }
            }
            return dx::VmValue::Void();
        });

    builder.VirtualMethod("getWindowToken", "()Landroid/os/IBinder;", [context](dx::IntrinsicContext& c) {
        return dx::VmValue::Ref(ManagedWindowToken(c.vm, *context, c.receiver));
    });
    builder.VirtualMethod("getApplicationWindowToken", "()Landroid/os/IBinder;", [context](dx::IntrinsicContext& c) {
        return dx::VmValue::Ref(ManagedWindowToken(c.vm, *context, c.receiver));
    });
    builder.VirtualMethod("setClickable", "(Z)V", [context, view_flags](dx::IntrinsicContext& call) {
        const auto before = dx::IntrinsicCall(call).GetInt(view_flags);
        static_cast<void>(
            CallAndroidMethod(call.vm, call.receiver, "setFlags", "(II)V",
                              {dx::VmValue::Int(call.arguments[0].AsInt() ? 0x4000 : 0), dx::VmValue::Int(0x4000)}));
        if (((before ^ dx::IntrinsicCall(call).GetInt(view_flags)) & 0x4000) != 0)
            context->ui_tree.MarkDrawDirty(context->ui_tree.Root());
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("getViewTreeObserver", "()Landroid/view/ViewTreeObserver;",
                          [context](dx::IntrinsicContext& call) {
                              return dx::VmValue::Ref(ManagedViewTreeObserver(call.vm, *context, call.receiver));
                          });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_ViewGroup.cpp ----
#include "catalog.h"

#include <optional>

namespace ogplay::runtime::android_intrinsics {
namespace {

// Shared by removeViews/removeViewsInLayout/removeAllViewsInLayout.

}  // namespace

Decl Declare_android_view_ViewGroup(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/ViewGroup;", "Landroid/view/View;",
                                                    {"Landroid/view/ViewParent;", "Landroid/view/ViewManager;"});
    {
        context->view_group_java_fields = DexVmAndroidContext::ViewGroupJavaFields{
            builder.BoundInstanceField("mChildren", "[Landroid/view/View;", dx::kAccPrivate),
            builder.BoundInstanceField("mChildrenCount", "I", dx::kAccPrivate),
            builder.BoundInstanceField("mGroupFlags", "I", dx::kAccProtected),
            builder.BoundInstanceField("mLayoutMode", "I", dx::kAccPrivate),
            builder.BoundInstanceField("mLastTouchDownIndex", "I", dx::kAccPrivate),
            builder.BoundInstanceField("mFirstTouchTarget", "Landroid/view/ViewGroup$TouchTarget;", dx::kAccPrivate),
            builder.BoundInstanceField("mFirstHoverTarget", "Landroid/view/ViewGroup$HoverTarget;", dx::kAccPrivate)};
        builder.Constructor("(Landroid/content/Context;)V", [context](dx::IntrinsicContext& call) {
            static_cast<void>(ViewInitHandler(context)(call));
            dx::IntrinsicCall fields(call);
            const auto& tokens = *context->view_group_java_fields;
            const auto vf = context->view_java_measure_fields->view_flags;
            fields.SetInt(vf, fields.GetInt(vf) | 0x80);  // WILL_NOT_DRAW, API19 initViewGroup.
            const auto private_flags = context->view_java_measure_fields->flags[0];
            fields.SetInt(private_flags, fields.GetInt(private_flags) | 0x80);  // PFLAG_SKIP_DRAW, null background.
            static_cast<void>(CallAndroidMethod(call.vm, call.receiver, "requestLayout", "()V"));
            static_cast<void>(CallAndroidMethod(call.vm, call.receiver, "invalidate", "(Z)V", {dx::VmValue::Int(1)}));
            const auto array =
                call.vm.Model().NewObjectArray(call.vm.Linker().ResolveDescriptor("[Landroid/view/View;"),
                                               call.vm.Linker().ResolveDescriptor("Landroid/view/View;"), 12);
            fields.SetRef(tokens.children, array);
            fields.SetInt(tokens.count, 0);
            fields.SetInt(tokens.flags, 0x20000 | 0x4053 | (context->target_sdk_version >= 11 ? 0x200000 : 0));
            fields.SetInt(tokens.mode, -1);
            fields.SetInt(tokens.last_touch_index, -1);
            return dx::VmValue::Void();
        });
        builder.VirtualMethod(
            "onViewAdded", "(Landroid/view/View;)V",
            [context](dx::IntrinsicContext& call) {
                MergeManagedViewTreeObservers(call.vm, *context, call.arguments[0].ref);
                return dx::VmValue::Void();
            },
            dx::kAccProtected);
        builder.VirtualMethod(
            "recomputeViewAttributes", "(Landroid/view/View;)V", [context](dx::IntrinsicContext& call) {
                // No system bars or WMS exist on this managed-window platform.
                // The request is retained in the actual Java View field; no system callback is synthesized.
                if (call.vm.CurrentContextToken() != 1U || context->ui_java_owner_thread != std::this_thread::get_id())
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                          "Window attributes require owner thread"};
                return dx::VmValue::Void();
            });
        const auto cancel_target = [context](bool touch) {
            return dx::IntrinsicHandler([context, touch](dx::IntrinsicContext& call) {
                if (touch && call.arguments[0].ref.IsValid()) ++context->ui_mount_generations[call.arguments[0].ref];
                const auto& t = *context->view_group_java_fields;
                if (dx::IntrinsicCall(call).GetRef(touch ? t.touch_target : t.hover_target).IsValid()) {
                    if (auto* ledger = call.vm.Ledger())
                        ledger->RecordUnimplemented("dexvm.view_group_input_target", 0);
                    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                          "Container input targets are not enabled"};
                }
                if (touch && context->ui_touch_capture) {
                    dx::IntrinsicCall fields(call);
                    auto captured = dx::VmObjectRef{static_cast<std::uint32_t>(context->ui_touch_capture)};
                    for (std::size_t depth = 0; captured.IsValid() && depth <= ui::UiTree::kMaxDepth; ++depth) {
                        if (captured == call.arguments[0].ref) {
                            context->ui_touch_capture = 0;
                            context->ui_touch_targets.clear();
                            context->ui_touch_clicks.clear();
                            break;
                        }
                        captured = fields.GetRef(context->view_java_hierarchy_fields->parent, captured);
                    }
                }
                return dx::VmValue::Void();
            });
        };
        const auto transition =
            builder.BoundInstanceField("mTransition", "Landroid/animation/LayoutTransition;", dx::kAccPrivate);
        const auto drag = builder.BoundInstanceField("mCurrentDrag", "Landroid/view/DragEvent;", dx::kAccPrivate);
        builder.VirtualMethod(
            "onChildVisibilityChanged", "(Landroid/view/View;II)V",
            [transition, drag](dx::IntrinsicContext& call) {
                dx::IntrinsicCall fields(call);
                if (fields.GetRef(transition).IsValid() || fields.GetRef(drag).IsValid()) {
                    if (auto* ledger = call.vm.Ledger())
                        ledger->RecordUnimplemented("dexvm.view_visibility_services", 0);
                    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                          "Transition or drag service unsupported"};
                }
                return dx::VmValue::Void();
            },
            dx::kAccProtected);
        builder.DirectMethod(
            "addDisappearingView", "(Landroid/view/View;)V",
            [](dx::IntrinsicContext& call) -> dx::VmValue {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_group_animation", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "Container disappearing animations are not enabled"};
            },
            dx::kAccPrivate);
        builder.DirectMethod("cancelTouchTarget", "(Landroid/view/View;)V", cancel_target(true), dx::kAccPrivate);
        builder.DirectMethod("cancelHoverTarget", "(Landroid/view/View;)V", cancel_target(false), dx::kAccPrivate);
    }
    builder.OverrideMethod("dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z",
                           [context](dx::IntrinsicContext& call) {
                               const auto owner = context->view_adapter_references[call.receiver.Value()].window_owner;
                               if (owner.IsValid())
                                   return CallAndroidMethod(call.vm, owner, "dispatchTouchEvent",
                                                            "(Landroid/view/MotionEvent;)Z", {call.arguments[0]});
                               return dx::VmValue::Int(DispatchAndroidViewTouch(call.vm, *context, call.receiver,
                                                                                call.arguments[0].ref, true));
                           });

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_ViewTreeObserver_OnGlobalLayoutListener.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_ViewTreeObserver_OnGlobalLayoutListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_ViewTreeObserver.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_ViewTreeObserver(const Context&) {
    return dx::IntrinsicClassBuilder::Class("Landroid/view/ViewTreeObserver;", "Ljava/lang/Object;").Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_Window.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
namespace {

// API19 WindowManager.LayoutParams fields (including copyFrom's hidden state).
constexpr std::pair<const char*, const char*> kWindowFields[] = {{"x", "I"},
                                                                 {"y", "I"},
                                                                 {"horizontalWeight", "F"},
                                                                 {"verticalWeight", "F"},
                                                                 {"type", "I"},
                                                                 {"memoryType", "I"},
                                                                 {"flags", "I"},
                                                                 {"privateFlags", "I"},
                                                                 {"softInputMode", "I"},
                                                                 {"gravity", "I"},
                                                                 {"horizontalMargin", "F"},
                                                                 {"verticalMargin", "F"},
                                                                 {"format", "I"},
                                                                 {"windowAnimations", "I"},
                                                                 {"alpha", "F"},
                                                                 {"dimAmount", "F"},
                                                                 {"screenBrightness", "F"},
                                                                 {"buttonBrightness", "F"},
                                                                 {"rotationAnimation", "I"},
                                                                 {"token", "Landroid/os/IBinder;"},
                                                                 {"packageName", "Ljava/lang/String;"},
                                                                 {"screenOrientation", "I"},
                                                                 {"systemUiVisibility", "I"},
                                                                 {"subtreeSystemUiVisibility", "I"},
                                                                 {"hasSystemUiListeners", "Z"},
                                                                 {"inputFeatures", "I"},
                                                                 {"userActivityTimeout", "J"},
                                                                 {"mTitle", "Ljava/lang/CharSequence;"}};

auto& WindowSlot(dx::Interpreter& vm, dx::VmObjectRef object, const char* name, const char* type) {
    const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), name, type);
    if (!field) throw std::logic_error(std::string("Window attribute field missing: ") + name);
    return vm.Model().InstanceSlots(object)[vm.Linker().Field(*field).slot];
}

void InitializeWindowAttributes(dx::Interpreter& vm, dx::VmObjectRef object) {
    const auto integer = [&](const char* name, int value) {
        WindowSlot(vm, object, name, "I") = {static_cast<std::uint32_t>(value), dx::SlotTag::cat1};
    };
    integer("width", -1);
    integer("height", -1);
    integer("type", 2);
    integer("format", -1);
    integer("screenOrientation", -1);
    for (const auto name : {"alpha", "dimAmount", "screenBrightness", "buttonBrightness"}) {
        const float value = (std::string_view(name) == "alpha" || std::string_view(name) == "dimAmount") ? 1.0F : -1.0F;
        WindowSlot(vm, object, name, "F") = {std::bit_cast<std::uint32_t>(value), dx::SlotTag::cat1};
    }
    // Dalvik wide values occupy two adjacent slots.
    const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), "userActivityTimeout", "J");
    const auto slot = vm.Linker().Field(*field).slot;
    vm.Model().InstanceSlots(object)[slot] = {0xffffffffU, dx::SlotTag::wide_lo};
    vm.Model().InstanceSlots(object)[slot + 1] = {0xffffffffU, dx::SlotTag::wide_hi};
    const auto title = vm.NewStringUtf8("");
    WindowSlot(vm, object, "mTitle", "Ljava/lang/CharSequence;") = {title.Value(), dx::SlotTag::ref};
}

[[nodiscard]] dx::VmObjectRef Attributes(dx::IntrinsicContext& call, const Context& context) {
    static_cast<void>(context);
    const auto field = call.vm.Linker().FindFieldRecursive(
        call.vm.Model().ObjectClass(call.receiver), "mWindowAttributes", "Landroid/view/WindowManager$LayoutParams;");
    if (!field) throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "Window attributes field is missing");
    const auto slot = call.vm.Linker().Field(*field).slot;
    auto value = dx::VmObjectRef(call.vm.Model().InstanceSlots(call.receiver)[slot].bits);
    if (!value.IsValid()) {
        value = call.vm.NewIntrinsicInstance("Landroid/view/WindowManager$LayoutParams;");
        const auto root = call.vm.ProtectReferences(std::array{value});
        InitializeWindowAttributes(call.vm, value);
        call.vm.Model().InstanceSlots(call.receiver)[slot] = {value.Value(), dx::SlotTag::ref};
    }
    return value;
}

[[nodiscard]] const dx::LinkedField& IntField(dx::IntrinsicContext& call, const dx::VmObjectRef object,
                                              const std::string& name) {
    const auto field = call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(object), name, "I");
    if (!field.has_value()) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "Window LayoutParams field is missing: " + name);
    }
    return call.vm.Linker().Field(*field);
}

[[nodiscard]] std::int32_t ReadIntField(dx::IntrinsicContext& call, const dx::VmObjectRef object,
                                        const std::string& name) {
    const auto& field = IntField(call, object, name);
    return static_cast<std::int32_t>(call.vm.Model().InstanceSlots(object)[field.slot].bits);
}

void WriteIntField(dx::IntrinsicContext& call, const dx::VmObjectRef object, const std::string& name,
                   const std::int32_t value) {
    const auto& field = IntField(call, object, name);
    call.vm.Model().InstanceSlots(object)[field.slot] = {static_cast<std::uint32_t>(value), dx::SlotTag::cat1};
}

}  // namespace

Decl Declare_android_view_Window(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/Window;", "Ljava/lang/Object;");
    const auto wu_attributes =
        builder.BoundInstanceField("mWindowAttributes", "Landroid/view/WindowManager$LayoutParams;", dx::kAccPrivate);
    builder.VirtualMethod("takeSurface", "(Landroid/view/SurfaceHolder$Callback2;)V",
                          [context](dx::IntrinsicContext& call) {
                              SetWindowSurfaceCallback(call.vm, *context, call.arguments[0].ref);
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod("takeInputQueue", "(Landroid/view/InputQueue$Callback;)V",
                          [context](dx::IntrinsicContext& call) {
                              SetWindowInputCallback(call.vm, *context, call.arguments[0].ref);
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod("setFormat", "(I)V", [context](dx::IntrinsicContext& call) {
        context->window_format = call.arguments[0].AsInt();
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("setFlags", "(II)V", [context](dx::IntrinsicContext& call) {
        const auto attributes = Attributes(call, context);
        const auto flags = call.arguments[0].AsInt();
        const auto mask = call.arguments[1].AsInt();
        const auto old = ReadIntField(call, attributes, "flags");
        WriteIntField(call, attributes, "flags", (old & ~mask) | (flags & mask));
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("addFlags", "(I)V", [](dx::IntrinsicContext& call) {
        return CallAndroidMethod(call.vm, call.receiver, "setFlags", "(II)V", {call.arguments[0], call.arguments[0]});
    });
    builder.VirtualMethod("clearFlags", "(I)V", [](dx::IntrinsicContext& call) {
        return CallAndroidMethod(call.vm, call.receiver, "setFlags", "(II)V", {dx::VmValue::Int(0), call.arguments[0]});
    });
    builder.VirtualMethod("setSoftInputMode", "(I)V", [context](dx::IntrinsicContext& call) {
        const auto mode = call.arguments[0].AsInt();
        if (mode != 0) {
            WriteIntField(call, Attributes(call, context), "softInputMode", mode);
        }
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("setType", "(I)V", [context](dx::IntrinsicContext& call) {
        WriteIntField(call, Attributes(call, context), "type", call.arguments[0].AsInt());
        return dx::VmValue::Void();
    });
    builder.FinalMethod("getAttributes", "()Landroid/view/WindowManager$LayoutParams;",
                        [context](dx::IntrinsicContext& call) { return dx::VmValue::Ref(Attributes(call, context)); });
    builder.VirtualMethod(
        "setAttributes", "(Landroid/view/WindowManager$LayoutParams;)V", [context](dx::IntrinsicContext& call) {
            const auto source = call.arguments[0].ref;
            if (!source.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "attributes == null"};
            const auto target = Attributes(call, context);
            const auto copy = [&](const char* name, const char* type) {
                auto& to = WindowSlot(call.vm, target, name, type);
                // AOSP copyFrom keeps an already assigned token/package name.
                if ((std::string_view(name) == "token" || std::string_view(name) == "packageName") && to.bits) return;
                to = WindowSlot(call.vm, source, name, type);
                if (std::string_view(type) == "J") {
                    const auto source_field =
                        call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(source), name, type);
                    const auto target_field =
                        call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(target), name, type);
                    call.vm.Model().InstanceSlots(target)[call.vm.Linker().Field(*target_field).slot + 1] =
                        call.vm.Model().InstanceSlots(source)[call.vm.Linker().Field(*source_field).slot + 1];
                }
            };
            copy("width", "I");
            copy("height", "I");
            for (const auto& [name, type] : kWindowFields) {
                if (std::string_view(name) != "memoryType") copy(name, type);
            }
            return dx::VmValue::Void();
        });
    const auto owner = builder.BoundInstanceField("mOgplayOwner", "Landroid/app/Activity;", dx::kAccPrivate);
    builder.VirtualMethod("getLayoutInflater", "()Landroid/view/LayoutInflater;", [owner](dx::IntrinsicContext& call) {
        const auto activity = dx::IntrinsicCall(call).GetRef(owner);
        if (!activity.IsValid())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Window has no Activity owner"};
        const auto service = call.vm.NewStringUtf8("layout_inflater");
        const auto roots = call.vm.ProtectReferences(std::array{activity, service});
        return CallAndroidMethod(call.vm, activity, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                                 {dx::VmValue::Ref(service)});
    });
    builder.VirtualMethod(
        "superDispatchTouchEvent", "(Landroid/view/MotionEvent;)Z", [context, owner](dx::IntrinsicContext& call) {
            if (dx::IntrinsicCall(call).GetRef(owner) != context->activity) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("runtime.ui_pointer_dispatch", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "background Window touch dispatch"};
            }
            return dx::VmValue::Int(DispatchContentTouchEvent(call.vm, *context, call.arguments[0].ref));
        });
    const auto decor = builder.BoundInstanceField("mDecor", "Landroid/view/View;", dx::kAccPrivate);
    const auto content_parent =
        builder.BoundInstanceField("mContentParent", "Landroid/widget/FrameLayout;", dx::kAccPrivate);
    context->ui_java_window_content = content_parent;
    const auto background =
        builder.BoundInstanceField("mBackgroundDrawable", "Landroid/graphics/drawable/Drawable;", dx::kAccPrivate);
    builder.VirtualMethod("setBackgroundDrawable", "(Landroid/graphics/drawable/Drawable;)V",
                          [background, content_parent](dx::IntrinsicContext& call) {
                              const dx::IntrinsicCall fields(call);
                              const auto drawable = call.arguments[0].ref;
                              if (fields.GetRef(background) == drawable) return dx::VmValue::Void();
                              fields.SetRef(background, drawable);
                              const auto parent = fields.GetRef(content_parent);
                              if (parent.IsValid())
                                  return CallAndroidMethod(call.vm, parent, "setBackgroundDrawable",
                                                           "(Landroid/graphics/drawable/Drawable;)V",
                                                           {dx::VmValue::Ref(drawable)});
                              return dx::VmValue::Void();
                          });
    const auto parent_for = [context, owner, content_parent, background](dx::IntrinsicContext& call) {
        const dx::IntrinsicCall fields(call);
        auto parent = fields.GetRef(content_parent);
        if (parent.IsValid()) return parent;
        const auto activity = fields.GetRef(owner);
        if (!activity.IsValid())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Window has no Activity owner"};
        const auto type = call.vm.Linker().ResolveDescriptor("Landroid/widget/FrameLayout;");
        const auto checked = [&call](const dx::VmCallOutcome& result) {
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor,
                                      result.exception_message, result.exception};
        };
        checked(call.vm.EnsureClassInitialized(type));
        parent = call.vm.NewIntrinsicInstance("Landroid/widget/FrameLayout;");
        const auto roots = call.vm.ProtectReferences(std::array{parent});
        const auto constructor = call.vm.Linker().FindDirectMethod(type, "<init>", "(Landroid/content/Context;)V");
        if (!constructor) throw std::logic_error("Window content FrameLayout constructor is missing");
        checked(call.vm.Call(*constructor, std::array{dx::VmValue::Ref(parent), dx::VmValue::Ref(activity)}));
        const auto node = *FindViewUiNode(*context, parent.Value());
        {
            static_cast<void>(CallAndroidMethod(call.vm, parent, "setId", "(I)V", {dx::VmValue::Int(0x01020002)}));
            fields.SetRef(content_parent, parent);
            for (auto& record : context->activity_stack) {
                if (record.object != activity) continue;
                record.window_root = node;
                // JavaTraversal publishes the lifecycle fact with the batch;
                // materializing a Window must not change its visibility.
            }
            const auto drawable = fields.GetRef(background);
            if (drawable.IsValid())
                static_cast<void>(CallAndroidMethod(call.vm, parent, "setBackgroundDrawable",
                                                    "(Landroid/graphics/drawable/Drawable;)V",
                                                    {dx::VmValue::Ref(drawable)}));
            return parent;
        }
    };
    builder.VirtualMethod("findViewById", "(I)Landroid/view/View;", [parent_for](dx::IntrinsicContext& call) {
        const auto id = call.arguments[0].AsInt();
        if (id == -1) return dx::VmValue::Ref(dx::VmObjectRef{});
        const auto parent = parent_for(call);
        const auto roots = call.vm.ProtectReferences(std::array{parent});
        return CallAndroidMethod(call.vm, parent, "findViewById", "(I)Landroid/view/View;", {dx::VmValue::Int(id)});
    });
    const auto mutate = [context, owner, parent_for](const bool replace, const bool xml) {
        return dx::IntrinsicHandler([context, owner, parent_for, replace, xml](dx::IntrinsicContext& call) {
            const auto activity = dx::IntrinsicCall(call).GetRef(owner);
            if (!context->activity_stack.empty() && activity != context->activity) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.activity_result", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "background Activity cannot mutate foreground content"};
            }
            if (!xml && !call.arguments[0].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "content view is null"};
            const auto parent = parent_for(call);
            const auto roots = call.vm.ProtectReferences(std::array{activity, parent});
            const auto node = *FindViewUiNode(*context, parent.Value());
            dx::VmObjectRef staged;
            if (xml) {
                // Inflate into an unpublished parent. A failed merge must not mutate the live root.
                const auto type = call.vm.Linker().ResolveDescriptor("Landroid/widget/FrameLayout;");
                staged = call.vm.NewIntrinsicInstance("Landroid/widget/FrameLayout;");
                const auto staged_root = call.vm.ProtectReferences(std::array{staged});
                const auto init = call.vm.Linker().FindDirectMethod(type, "<init>", "(Landroid/content/Context;)V");
                const auto initialized =
                    call.vm.Call(*init, std::array{dx::VmValue::Ref(staged), dx::VmValue::Ref(activity)});
                if (initialized.exception.IsValid())
                    throw dx::VmJavaThrow{call.vm.Linker().Class(initialized.exception_class).descriptor,
                                          initialized.exception_message, initialized.exception};
                const auto inflater =
                    CallAndroidMethod(call.vm, call.receiver, "getLayoutInflater", "()Landroid/view/LayoutInflater;")
                        .ref;
                const auto inflater_root = call.vm.ProtectReferences(std::array{inflater});
                static_cast<void>(
                    CallAndroidMethod(call.vm, inflater, "inflate", "(ILandroid/view/ViewGroup;Z)Landroid/view/View;",
                                      {call.arguments[0], dx::VmValue::Ref(staged), dx::VmValue::Int(1)}));
            }
            const auto staged_root = call.vm.ProtectReferences(std::array{staged});
            const auto revision = ++context->ui_content_revision;
            ++context->ui_content_mutation_depth;
            struct MutationScope {
                DexVmAndroidContext& context;
                ~MutationScope() { --context.ui_content_mutation_depth; }
            } scope{*context};
            const auto current = [&] {
                return context->ui_content_revision == revision && context->ui_tree.Get(node) &&
                       (context->activity_stack.empty() || context->activity == activity);
            };
            if (replace) {
                const auto count =
                    static_cast<std::size_t>(CallAndroidMethod(call.vm, parent, "getChildCount", "()I").AsInt());
                if (count != 0) {
                    static_cast<void>(CallAndroidMethod(call.vm, parent, "removeAllViews", "()V"));
                }
                if (!current()) return dx::VmValue::Void();
                context->content_view = dx::VmObjectRef{};
            }
            if (xml) {
                while (CallAndroidMethod(call.vm, staged, "getChildCount", "()I").AsInt() != 0) {
                    const auto child = CallAndroidMethod(call.vm, staged, "getChildAt", "(I)Landroid/view/View;",
                                                         {dx::VmValue::Int(0)})
                                           .ref;
                    const auto child_root = call.vm.ProtectReferences(std::array{child});
                    static_cast<void>(CallAndroidMethod(call.vm, staged, "removeView", "(Landroid/view/View;)V",
                                                        {dx::VmValue::Ref(child)}));
                    static_cast<void>(CallAndroidMethod(call.vm, parent, "addView", "(Landroid/view/View;)V",
                                                        {dx::VmValue::Ref(child)}));
                }
            }
            if (!xml) {
                auto params = call.arguments[1].ref;
                if (!params.IsValid())
                    params = NewAndroidLayoutParams(call.vm, "Landroid/widget/FrameLayout$LayoutParams;", -1, -1);
                const auto params_root = call.vm.ProtectReferences(std::array{params});
                static_cast<void>(CallAndroidMethod(call.vm, parent, "addView",
                                                    "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                                                    {call.arguments[0], dx::VmValue::Ref(params)}));
            }
            if (!current()) return dx::VmValue::Void();
            {
                context->content_view =
                    CallAndroidMethod(call.vm, parent, "getChildAt", "(I)Landroid/view/View;", {dx::VmValue::Int(0)})
                        .ref;
            }
            static_cast<void>(CallAndroidMethod(call.vm, activity, "onContentChanged", "()V"));
            if (!current()) return dx::VmValue::Void();
            return dx::VmValue::Void();
        });
    };
    builder.VirtualMethod("setContentView", "(I)V", mutate(true, true));
    builder.VirtualMethod("setContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                          mutate(true, false));
    builder.VirtualMethod("addContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                          mutate(false, false));
    builder.VirtualMethod("setContentView", "(Landroid/view/View;)V", [](dx::IntrinsicContext& call) {
        const auto params = NewAndroidLayoutParams(call.vm, "Landroid/view/ViewGroup$LayoutParams;", -1, -1);
        const auto roots = call.vm.ProtectReferences(std::array{params});
        return CallAndroidMethod(call.vm, call.receiver, "setContentView",
                                 "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                                 {call.arguments[0], dx::VmValue::Ref(params)});
    });
    builder.VirtualMethod("getDecorView", "()Landroid/view/View;",
                          [context, decor, owner, parent_for](dx::IntrinsicContext& call) {
                              dx::IntrinsicCall fields(call);
                              {
                                  // The managed fullscreen Window has one real Java root; a detached
                                  // facade cannot own a second observer or report window coordinates.
                                  const auto root = parent_for(call);
                                  fields.SetRef(decor, root);
                                  context->view_adapter_references[root.Value()].window_owner = fields.GetRef(owner);
                                  return dx::VmValue::Ref(root);
                              }
                          });
    const auto wu_mContext =
        builder.BoundInstanceField("mContext", "Landroid/content/Context;", dx::kAccPrivate | dx::kAccFinal);
    builder.VirtualMethod("setBackgroundDrawableResource", "(I)V", [wu_mContext](dx::IntrinsicContext& call) {
        const auto context_object = dx::IntrinsicCall(call).GetRef(wu_mContext);
        const auto resources =
            CallAndroidMethod(call.vm, context_object, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto resources_root = call.vm.ProtectReferences(std::array{resources});
        const auto drawable = CallAndroidMethod(call.vm, resources, "getDrawable",
                                                "(I)Landroid/graphics/drawable/Drawable;", {call.arguments[0]})
                                  .ref;
        const auto drawable_root = call.vm.ProtectReferences(std::array{drawable});
        return CallAndroidMethod(call.vm, call.receiver, "setBackgroundDrawable",
                                 "(Landroid/graphics/drawable/Drawable;)V", {dx::VmValue::Ref(drawable)});
    });
    const auto wu_mIsActive = builder.BoundInstanceField("mIsActive", "Z", dx::kAccPrivate);
    const auto wu_mHasChildren = builder.BoundInstanceField("mHasChildren", "Z", dx::kAccPrivate);
    const auto wu_mCloseOnTouchOutside = builder.BoundInstanceField("mCloseOnTouchOutside", "Z", dx::kAccPrivate);
    const auto wu_mSetCloseOnTouchOutside = builder.BoundInstanceField("mSetCloseOnTouchOutside", "Z", dx::kAccPrivate);
    const auto wu_mForcedWindowFlags = builder.BoundInstanceField("mForcedWindowFlags", "I", dx::kAccPrivate);
    const auto wu_mFeatures = builder.BoundInstanceField("mFeatures", "I", dx::kAccPrivate);
    const auto wu_mLocalFeatures = builder.BoundInstanceField("mLocalFeatures", "I", dx::kAccPrivate);
    const auto wu_mHaveWindowFormat = builder.BoundInstanceField("mHaveWindowFormat", "Z", dx::kAccPrivate);
    const auto wu_mHaveDimAmount = builder.BoundInstanceField("mHaveDimAmount", "Z", dx::kAccPrivate);
    const auto wu_mDefaultWindowFormat = builder.BoundInstanceField("mDefaultWindowFormat", "I", dx::kAccPrivate);
    const auto wu_mHasSoftInputMode = builder.BoundInstanceField("mHasSoftInputMode", "Z", dx::kAccPrivate);
    // AOSP 4.4 ROM line 390.
    builder.Constructor(
        "(Landroid/content/Context;)V",
        [wu_attributes, wu_mContext, wu_mIsActive, wu_mHasChildren, wu_mCloseOnTouchOutside, wu_mSetCloseOnTouchOutside,
         wu_mForcedWindowFlags, wu_mFeatures, wu_mLocalFeatures, wu_mHaveWindowFormat, wu_mHaveDimAmount,
         wu_mDefaultWindowFormat, wu_mHasSoftInputMode](dx::IntrinsicContext& c) {
            const auto attributes = c.vm.NewIntrinsicInstance("Landroid/view/WindowManager$LayoutParams;");
            const auto root = c.vm.ProtectReferences(std::array{attributes});
            const auto target = c.vm.Linker().FindDirectMethod(
                c.vm.Linker().ResolveDescriptor("Landroid/view/WindowManager$LayoutParams;"), "<init>", "()V");
            const auto result = c.vm.Call(*target, std::vector<dx::VmValue>{dx::VmValue::Ref(attributes)});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            dx::IntrinsicCall api(c);
            for (const auto& [field, value] :
                 std::array<std::pair<dx::IntrinsicFieldHandle, int>, 11>{{{wu_mIsActive, 0},
                                                                           {wu_mHasChildren, 0},
                                                                           {wu_mCloseOnTouchOutside, 0},
                                                                           {wu_mSetCloseOnTouchOutside, 0},
                                                                           {wu_mForcedWindowFlags, 0},
                                                                           {wu_mFeatures, 65},
                                                                           {wu_mLocalFeatures, 65},
                                                                           {wu_mHaveWindowFormat, 0},
                                                                           {wu_mHaveDimAmount, 0},
                                                                           {wu_mDefaultWindowFormat, -1},
                                                                           {wu_mHasSoftInputMode, 0}}})
                api.SetInt(field, value);
            api.SetRef(wu_attributes, attributes);
            api.SetRef(wu_mContext, c.arguments[0].ref);
            return dx::VmValue::Void();
        },
        dx::kAccPublic);

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_WindowManager_LayoutParams.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_WindowManager_LayoutParams(const Context& context) {
    static_cast<void>(context);
    auto builder =
        dx::IntrinsicClassBuilder::Class("Landroid/view/WindowManager$LayoutParams;",
                                         "Landroid/view/ViewGroup$LayoutParams;", {"Landroid/os/Parcelable;"});
    for (const auto& [name, type] : kWindowFields)
        builder.InstanceField(name, type, std::string_view(name) == "mTitle" ? dx::kAccPrivate : dx::kAccPublic);
    for (const auto signature : {"()V", "(I)V", "(II)V", "(III)V", "(IIIII)V", "(IIIIIII)V"}) {
        builder.Constructor(signature, [](dx::IntrinsicContext& call) {
            InitializeWindowAttributes(call.vm, call.receiver);
            const auto count = call.arguments.size();
            const auto set = [&](const char* name, std::size_t argument) {
                WindowSlot(call.vm, call.receiver, name,
                           "I") = {static_cast<std::uint32_t>(call.arguments[argument].AsInt()), dx::SlotTag::cat1};
            };
            if (count >= 5) {
                set("width", 0);
                set("height", 1);
            }
            if (count == 7) {
                set("x", 2);
                set("y", 3);
            }
            if (count) set("type", count >= 5 ? count - 3 : 0);
            if (count >= 2) set("flags", count >= 5 ? count - 2 : 1);
            if (count >= 3) set("format", count - 1);
            return dx::VmValue::Void();
        });
    }
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_WindowManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_WindowManager(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/WindowManager;", {"Landroid/view/ViewManager;"});
    builder.UnimplementedVirtual("getDefaultDisplay", "()Landroid/view/Display;", dx::kAccPublic | dx::kAccAbstract);
    builder.UnimplementedVirtual("removeViewImmediate", "(Landroid/view/View;)V", dx::kAccPublic | dx::kAccAbstract);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_WindowManagerImpl.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_WindowManagerImpl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/WindowManagerImpl;", "Ljava/lang/Object;",
                                                    {"Landroid/view/WindowManager;"});
    builder.FinalMethod("getDefaultDisplay", "()Landroid/view/Display;",
                        WindowmanagerGetDefaultDisplayHandler(context));
    const auto owner_field = builder.BoundInstanceField("mOwnerActivity", "Landroid/app/Activity;", dx::kAccPrivate);
    context->window_manager_owner_field = owner_field;
    const auto mutate = [context, owner_field](dx::IntrinsicContext& call, const bool update) {
        const auto view = call.arguments[0].ref;
        if (!view.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "view must not be null"};
        const auto params = call.arguments[1].ref;
        if (!params.IsValid() || !call.vm.Linker().IsAssignable(
                                     call.vm.Linker().ResolveDescriptor("Landroid/view/WindowManager$LayoutParams;"),
                                     call.vm.Model().ObjectClass(params)))
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Params must be WindowManager.LayoutParams"};
        const auto owner = dx::IntrinsicCall(call).GetRef(owner_field);
        const auto record = std::find_if(context->activity_stack.begin(), context->activity_stack.end(),
                                         [&](const auto& item) { return item.object == owner; });
        const auto bad_token = [] {
            throw dx::VmJavaThrow{"Landroid/view/WindowManager$BadTokenException;",
                                  "Unable to add window -- token is not valid; is your activity running?"};
        };
        if (!owner.IsValid() || record == context->activity_stack.end() || context->process_stopping ||
            CallAndroidMethod(call.vm, owner, "isFinishing", "()Z").AsInt() ||
            CallAndroidMethod(call.vm, owner, "isDestroyed", "()Z").AsInt())
            bad_token();
        const auto integer = [&](const char* name) {
            return static_cast<std::int32_t>(WindowSlot(call.vm, params, name, "I").bits);
        };
        const auto unsupported = [&] {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.window_content", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "Application window layout is not supported"};
        };
        const auto type = integer("type"), flags = integer("flags");
        if (type < 1 || type > 99 || integer("width") != -1 || integer("height") != -1 || integer("gravity") != 0 ||
            integer("x") != 0 || integer("y") != 0 || (flags & ~0x5b8) != 0)
            unsupported();
        for (const auto name : {"horizontalWeight", "verticalWeight", "horizontalMargin", "verticalMargin"})
            if (WindowSlot(call.vm, params, name, "F").bits != 0) unsupported();
        const auto token = dx::VmObjectRef{WindowSlot(call.vm, params, "token", "Landroid/os/IBinder;").bits};
        if (token.IsValid() && token != record->app_window_token) bad_token();
        const auto found = std::find_if(context->application_windows.begin(), context->application_windows.end(),
                                        [&](const auto& item) { return item.root == view && !item.removed; });
        const auto describe = [&] {
            return call.vm.Linker().Class(call.vm.Model().ObjectClass(view)).descriptor + "@" +
                   std::to_string(view.Value());
        };
        if (!update && found != context->application_windows.end())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  "View " + describe() + " has already been added to the window manager."};
        if (update && (found == context->application_windows.end() || found->owner != owner))
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "View=" + describe() + " not attached to window manager"};
        if (!update) {
            if (CallAndroidMethod(call.vm, view, "getParent", "()Landroid/view/ViewParent;").ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "The specified child already has a parent"};
            const auto count =
                std::ranges::count_if(context->application_windows, [](const auto& item) { return !item.removed; });
            if (static_cast<std::size_t>(count) + context->dialog_presentations.size() >= 8) unsupported();
            WindowSlot(call.vm, params, "token", "Landroid/os/IBinder;") = {record->app_window_token.Value(),
                                                                            dx::SlotTag::ref};
            auto title = dx::VmObjectRef{WindowSlot(call.vm, params, "mTitle", "Ljava/lang/CharSequence;").bits};
            if (!title.IsValid() || CallAndroidMethod(call.vm, title, "length", "()I").AsInt() == 0) {
                title = call.vm.NewStringUtf8(context->application_label &&
                                                      std::holds_alternative<std::string>(*context->application_label)
                                                  ? std::get<std::string>(*context->application_label)
                                                  : context->package_name);
                WindowSlot(call.vm, params, "mTitle", "Ljava/lang/CharSequence;") = {title.Value(), dx::SlotTag::ref};
            }
            if (!WindowSlot(call.vm, params, "packageName", "Ljava/lang/String;").bits) {
                const auto package = call.vm.NewStringUtf8(context->package_name);
                WindowSlot(call.vm, params, "packageName", "Ljava/lang/String;") = {package.Value(), dx::SlotTag::ref};
            }
        }
        // No managed state is committed until the guest setter succeeds.
        static_cast<void>(CallAndroidMethod(call.vm, view, "setLayoutParams",
                                            "(Landroid/view/ViewGroup$LayoutParams;)V", {dx::VmValue::Ref(params)}));
        if (update) {
            found->params = params;
            found->flags = flags;
            found->format = integer("format");
        } else {
            static_cast<void>(EnsureViewUiNode(*context, view, UiClassForObject(call.vm, view)));
            context->ui_window_schedules[view].window_token = dx::VmObjectRef{};
            ++context->ui_mount_generations[view];
            context->application_windows.push_back({owner,
                                                    view,
                                                    params,
                                                    context->next_application_window_generation++,
                                                    flags,
                                                    integer("format"),
                                                    false,
                                                    {}});
        }
        ++context->application_window_revision;
        context->ui_window_schedules[view].requests.insert(view);
        return dx::VmValue::Void();
    };
    builder.VirtualMethod("addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                          [mutate](dx::IntrinsicContext& call) { return mutate(call, false); });
    builder.VirtualMethod("updateViewLayout", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                          [mutate](dx::IntrinsicContext& call) { return mutate(call, true); });
    const auto remove = [context, owner_field](dx::IntrinsicContext& call) {
        const auto view = call.arguments[0].ref;
        if (!view.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "view must not be null"};
        const auto owner = dx::IntrinsicCall(call).GetRef(owner_field);
        const auto found =
            std::find_if(context->application_windows.begin(), context->application_windows.end(),
                         [&](const auto& item) { return item.root == view && item.owner == owner && !item.removed; });
        if (found == context->application_windows.end())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "View=" + call.vm.Linker().Class(call.vm.Model().ObjectClass(view)).descriptor + "@" +
                                      std::to_string(view.Value()) + " not attached to window manager"};
        if (auto* logger = call.vm.Log()) {
            const auto info =
                dx::IntrinsicCall(call).GetRef(context->view_java_measure_fields->unsupported_state[1], view);
            float alpha = 1.0F;
            if (info.IsValid()) {
                const auto field =
                    call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(info), "mAlpha", "F");
                alpha = std::bit_cast<float>(static_cast<std::uint32_t>(
                    call.vm.Model().InstanceSlots(info)[call.vm.Linker().Field(*field).slot].bits));
            }
            logger->Write(core::LogLevel::info, "android.window", "application window removed", {},
                          {{"root", static_cast<std::uint64_t>(view.Value())},
                           {"owner", static_cast<std::uint64_t>(owner.Value())},
                           {"alpha", static_cast<double>(alpha)},
                           {"uptime_ms", context->uptime_millis.load()}});
        }
        found->removed = true;
        ++context->application_window_revision;
        return dx::VmValue::Void();
    };
    builder.VirtualMethod("removeView", "(Landroid/view/View;)V", remove);
    builder.VirtualMethod("removeViewImmediate", "(Landroid/view/View;)V", remove);

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_InputQueue(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/InputQueue;", "Ljava/lang/Object;");
    const auto pointer = builder.BoundInstanceField("mPtr", "I", dx::kAccPrivate);
    builder.FinalMethod("getNativePtr", "()I", [pointer](dx::IntrinsicContext& call) {
        const auto value = dx::IntrinsicCall(call).GetInt(pointer);
        if (value == 0) throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "InputQueue is disposed"};
        return dx::VmValue::Int(value);
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime {
bool EnsureAndroidUiTraversal(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                              const AndroidUiTraversalReason reason, const AndroidUiTraversalPhase phase,
                              const std::optional<ui::Size> viewport) {
    dexvm::VmExecutionLockScope lock(vm.ExecutionLock());
    const auto diagnostic = [&](const std::string& why) {
        RecordAndroidTraversalDiagnostic(vm, context, AndroidUiTraversalRoot(vm, context), why);
    };
    if (vm.CurrentContextToken() != 1U || context.ui_java_owner_thread != std::this_thread::get_id()) {
        RecordAndroidTraversalDiagnostic(vm, context, dexvm::VmObjectRef{}, "owner thread rejected");
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "View traversal requires owner thread"};
    }
    if ((reason != AndroidUiTraversalReason::OwnerTick && reason != AndroidUiTraversalReason::BeforeInput &&
         reason != AndroidUiTraversalReason::BeforePresent &&
         reason != AndroidUiTraversalReason::BeforeSurfaceCallbacks &&
         !(context.ui_layout_dispatching && reason == AndroidUiTraversalReason::GeometryRead)) ||
        (phase != AndroidUiTraversalPhase::ResolveOnly && phase != AndroidUiTraversalPhase::ResolveAndNotifySizes)) {
        diagnostic("unsupported traversal phase");
        if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_traversal_phase", 0);
        throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "Traversal safe point is not enabled"};
    }
    if (context.ui_layout_dispatching) {
        if (reason == AndroidUiTraversalReason::GeometryRead) {
            diagnostic("temporary callback geometry");
            static_cast<void>(ReadAndroidUiGeometry(vm, context));
            return false;
        }
        if (reason == AndroidUiTraversalReason::BeforeInput) {
            diagnostic("reentrant input rejected");
            if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_reentrant_input", 0);
            throw dexvm::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                     "Input during Java traversal unsupported"};
        }
        diagnostic("layout callback barrier");
        return false;
    }
    if (context.input_presentation || context.surface_event_dispatching) {
        diagnostic(context.input_presentation ? "input lease barrier" : "surface callback barrier");
        return false;
    }
    if (reason == AndroidUiTraversalReason::OwnerTick) ++context.ui_owner_tick;
    // ViewRootImpl's scheduleTraversals boundary: only Java requests,
    // first attachment, or changed window inputs enter performTraversals.
    // Do not execute guest getters or compare a projection on a clean frame.
    const auto scheduled_root = AndroidUiTraversalRoot(vm, context);
    const auto found = context.ui_window_schedules.find(scheduled_root);
    if (found != context.ui_window_schedules.end()) {
        const auto& scheduled = found->second;
        const auto requested_metrics = viewport.value_or(ui::Size{static_cast<std::int32_t>(context.surface_width),
                                                                  static_cast<std::int32_t>(context.surface_height)});
        dexvm::IntrinsicContext call{vm, scheduled_root, {}};
        const auto* host = context.ui_tree.Get(context.ui_tree.Root());
        if (scheduled.published &&
            scheduled.published ==
                (context.application_window_root_override.IsValid()
                     ? std::find_if(context.application_windows.begin(), context.application_windows.end(),
                                    [&](const auto& item) { return item.root == scheduled_root && !item.removed; })
                           ->projection
                     : context.ui_tree.PublishedProjection()) &&
            scheduled.generation == context.ui_tree.Generation() &&
            scheduled.content_revision == context.ui_content_revision && scheduled.metrics == requested_metrics &&
            scheduled.density == context.ui_density && scheduled.requests.empty() &&
            scheduled.deferred_until_tick == 0 && !scheduled.draw_requested && !host->layout_dirty &&
            !host->draw_dirty &&
            (dexvm::IntrinsicCall(call).GetInt(context.view_java_measure_fields->flags[0]) & 0x1000) == 0) {
            diagnostic("clean snapshot");
            return false;
        }
    }
    diagnostic("traversal entered");
    // Materialize the Window's stable FrameLayout even for an empty Activity.
    if (context.activity.IsValid() && !context.application_window_root_override.IsValid()) {
        const auto window =
            android_intrinsics::CallAndroidMethod(vm, context.activity, "getWindow", "()Landroid/view/Window;").ref;
        const auto window_root = vm.ProtectReferences(std::array{window});
        static_cast<void>(android_intrinsics::CallAndroidMethod(vm, window, "findViewById", "(I)Landroid/view/View;",
                                                                {dexvm::VmValue::Int(0x01020002)}));
    }
    const auto metrics = viewport.value_or(
        ui::Size{static_cast<std::int32_t>(context.surface_width), static_cast<std::int32_t>(context.surface_height)});
    if (metrics.width < 0 || metrics.height < 0 || metrics.width > 0x3fffffff || metrics.height > 0x3fffffff) {
        diagnostic("invalid metrics");
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Invalid traversal viewport"};
    }
    const auto root = AndroidUiTraversalRoot(vm, context);
    const auto content = context.content_view;
    const auto node = FindViewUiNode(context, root.Value());
    const auto generation = context.ui_tree.Generation();
    const auto revision = context.ui_content_revision;
    const auto width = context.surface_width;
    const auto height = context.surface_height;
    const auto density = context.ui_density;
    const auto roots = vm.ProtectReferences(std::array{root});
    const auto application_revision = context.application_window_revision;
    const auto current = [&] {
        return (!context.application_window_root_override.IsValid() ||
                application_revision == context.application_window_revision) &&
               root == AndroidUiTraversalRoot(vm, context) && content == context.content_view &&
               revision == context.ui_content_revision && generation == context.ui_tree.Generation() &&
               width == context.surface_width && height == context.surface_height && density == context.ui_density &&
               node && FindViewUiNode(context, root.Value()) == node;
    };
    if (!current()) {
        diagnostic("unbound window root");
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Traversal requires a bound window root"};
    }
    if (!metrics.width || !metrics.height) diagnostic("zero metrics");
    auto& schedule = context.ui_window_schedules[root];
    const bool same_identity = schedule.generation == generation && schedule.content_revision == revision;
    if (schedule.generation != generation) {
        const auto token = schedule.window_token;
        schedule = {};
        schedule.window_token = token;
    }
    if (schedule.deferred_until_tick > context.ui_owner_tick) {
        diagnostic("deferred next tick");
        return false;
    }
    dexvm::IntrinsicContext root_call{vm, root, {}};
    const bool layout_requested =
        (dexvm::IntrinsicCall(root_call).GetInt(context.view_java_measure_fields->flags[0]) & 0x1000) != 0;
    if (same_identity && schedule.published &&
        schedule.published ==
            (context.application_window_root_override.IsValid()
                 ? std::find_if(context.application_windows.begin(), context.application_windows.end(),
                                [&](const auto& item) { return item.root == root && !item.removed; })
                       ->projection
                 : context.ui_tree.PublishedProjection()) &&
        schedule.requests.empty() && !layout_requested && schedule.metrics == metrics && schedule.density == density &&
        !context.ui_tree.Get(context.ui_tree.Root())->layout_dirty) {
        diagnostic("clean snapshot");
        if (schedule.draw_requested || context.ui_tree.Get(context.ui_tree.Root())->draw_dirty) {
            const auto target = *context.ui_tree.ProjectionTarget();
            CommitAndroidMeasuredRootProjection(vm, context, root, target, context.ui_measure_epoch + 1);
            schedule.draw_requested = false;
            if (context.application_window_root_override.IsValid()) {
                const auto window = std::find_if(context.application_windows.begin(), context.application_windows.end(),
                                                 [&](const auto& item) { return item.root == root && !item.removed; });
                if (window != context.application_windows.end()) schedule.published = window->projection;
            } else {
                context.ui_tree.ClearDrawDirty();
                schedule.published = context.ui_tree.PublishedProjection();
            }
        }
        return false;
    }
    schedule.generation = generation;
    try {
        // First traversal attaches the surviving Java subtree, not onCreate/setContentView.
        android_intrinsics::MergeManagedViewTreeObservers(vm, context, root);
        // An interrupted attempt cannot certify the next barrier as clean, even
        // when the Java layout epilogue already cleared FORCE_LAYOUT.
        schedule.published.reset();
        auto& requests = schedule.requests;
        context.ui_layout_dispatching = true;
        context.ui_java_active_metrics = metrics;

        struct Reset {
            DexVmAndroidContext& context;

            ~Reset() {
                context.ui_layout_dispatching = false;
                context.ui_java_layout_pass = 0;
                context.ui_java_active_metrics.reset();
            }
        } reset{context};

        const auto pending = std::move(requests);
        requests.clear();
        std::vector<dexvm::VmObjectRef> pending_refs;
        for (const auto requester : pending)
            if (AndroidViewBelongsToRoot(vm, context, requester, true)) pending_refs.push_back(requester);
        const auto pending_roots = vm.ProtectReferences(pending_refs);
        for (const auto requester : pending_refs) {
            if (AndroidViewBelongsToRoot(vm, context, requester, true))
                static_cast<void>(android_intrinsics::CallAndroidMethod(vm, requester, "requestLayout", "()V"));
            if (!current()) {
                diagnostic("callback changed identity or metrics");
                return false;
            }
        }
        requests.clear();
        dexvm::IntrinsicContext call{vm, root, {}};
        dexvm::IntrinsicCall fields(call);
        const auto& tokens = *context.view_java_measure_fields;
        for (unsigned pass = 1; pass <= 2; ++pass) {
            static_cast<void>(android_intrinsics::CallAndroidMethod(
                vm, root, "measure", "(II)V",
                {dexvm::VmValue::Int(metrics.width | 0x40000000), dexvm::VmValue::Int(metrics.height | 0x40000000)}));
            if (!current()) {
                diagnostic("callback changed identity or metrics");
                return false;
            }
            context.ui_java_layout_pass = pass;
            static_cast<void>(android_intrinsics::CallAndroidMethod(
                vm, root, "layout", "(IIII)V",
                {dexvm::VmValue::Int(0), dexvm::VmValue::Int(0),
                 dexvm::VmValue::Int(fields.GetInt(tokens.measured[0]) & 0x00ffffff),
                 dexvm::VmValue::Int(fields.GetInt(tokens.measured[1]) & 0x00ffffff)}));
            context.ui_java_layout_pass = 0;
            if (!current()) {
                diagnostic("callback changed identity or metrics");
                return false;
            }
            if (pass == 2) {
                std::erase_if(requests, [&](const auto requester) {
                    return !AndroidViewBelongsToRoot(vm, context, requester, true);
                });
                break;
            }
            const auto requested = std::move(requests);
            requests.clear();
            std::vector<dexvm::VmObjectRef> valid;
            for (const auto requester : requested) {
                const bool attached = AndroidViewBelongsToRoot(vm, context, requester);
                if (attached && AndroidViewBelongsToRoot(vm, context, requester, true) &&
                    (fields.GetInt(tokens.flags[0], requester) & 0x1000) != 0)
                    valid.push_back(requester);
            }
            // ViewRootImpl.getValidLayoutRequesters evaluates every requester
            // before clearing flags, including GONE/detached requesters. Clearing
            // an ancestor early must not hide a second valid requester.
            for (const auto requester : requested) {
                if (!vm.Model().IsValidRef(requester)) continue;
                auto view = requester;
                while (view.IsValid() && (fields.GetInt(tokens.flags[0], view) & 0x1000) != 0) {
                    fields.SetInt(tokens.flags[0], view, fields.GetInt(tokens.flags[0], view) & ~0x1000);
                    view = fields.GetRef(context.view_java_hierarchy_fields->parent, view);
                }
            }
            if (valid.empty()) break;
            const auto request_roots = vm.ProtectReferences(valid);
            for (const auto requester : valid) {
                static_cast<void>(android_intrinsics::CallAndroidMethod(vm, requester, "requestLayout", "()V"));
                if (!current()) {
                    diagnostic("callback changed identity or metrics");
                    return false;
                }
            }
            requests.clear();
        }
        if (!requests.empty()) {
            schedule.deferred_until_tick = context.ui_owner_tick + 1;
            if (schedule.deferred_ticks == 0 || schedule.watchdog_tick != context.ui_owner_tick) {
                schedule.watchdog_tick = context.ui_owner_tick;
                ++schedule.deferred_ticks;
            }
            if (schedule.deferred_ticks > 8) {
                diagnostic("layout watchdog rejected");
                const auto detail = "View layout did not converge: requester=" + std::to_string(root.Value()) +
                                    " epoch=" + std::to_string(context.ui_measure_epoch + 1);
                if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_layout_watchdog", 0);
                android_intrinsics::GuestLog(call, core::LogLevel::error, detail);
                throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", detail};
            }
        } else {
            schedule.deferred_ticks = 0;
            schedule.deferred_until_tick = 0;
        }
        const auto epoch = context.ui_measure_epoch + 1;
        const auto previous_target = context.ui_tree.ProjectionTarget();
        const auto metrics_revision =
            previous_target
                ? previous_target->metrics_revision + (previous_target->metrics.width != metrics.width ||
                                                               previous_target->metrics.height != metrics.height ||
                                                               context.ui_projection_density != density
                                                           ? 1U
                                                           : 0U)
                : 1U;
        const ui::UiProjectionTarget target{
            context.ui_tree.Root(), generation, revision, metrics_revision, {metrics.width, metrics.height}};
        if (!context.application_window_root_override.IsValid()) context.ui_tree.SetProjectionTarget(target);
        CommitAndroidMeasuredRootProjection(vm, context, root, target, epoch);
        schedule.draw_requested = false;
        if (context.application_window_root_override.IsValid()) {
            const auto window = std::find_if(context.application_windows.begin(), context.application_windows.end(),
                                             [&](const auto& item) { return item.root == root && !item.removed; });
            if (window == context.application_windows.end()) return false;
            schedule.published = window->projection;
        } else {
            context.ui_tree.ClearDrawDirty();
            schedule.published = context.ui_tree.PublishedProjection();
        }
        schedule.content_revision = revision;
        schedule.metrics = metrics;
        schedule.density = density;
        return true;
    } catch (...) {
        diagnostic("traversal exception");
        throw;
    }
}

bool DispatchAndroidViewSizes(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    return EnsureAndroidUiTraversal(vm, context, AndroidUiTraversalReason::BeforeSurfaceCallbacks,
                                    AndroidUiTraversalPhase::ResolveAndNotifySizes);
}

void DispatchAndroidGlobalLayout(dexvm::Interpreter& vm, DexVmAndroidContext& context) {
    {
        const dexvm::VmExecutionLockScope execution(vm.ExecutionLock());
        const auto root = AndroidUiTraversalRoot(vm, context);
        if (!root.IsValid() || context.ui_window_schedules[root].generation == 0) return;
        android_intrinsics::MergeManagedViewTreeObservers(vm, context, root);
        const auto observer = android_intrinsics::ManagedViewTreeObserver(vm, context, root);
        const auto protection = vm.ProtectReferences(std::array{root, observer});
        static_cast<void>(android_intrinsics::CallAndroidMethod(vm, observer, "dispatchOnGlobalLayout", "()V"));
        return;
    }
}
}  // namespace ogplay::runtime

namespace ogplay::runtime::android_intrinsics {
Decl Declare_test_AccessibilityManager(const Context& context) {
    auto builder =
        dx::IntrinsicClassBuilder::Class("Landroid/view/accessibility/AccessibilityManager;", "Ljava/lang/Object;");
    const auto singleton =
        builder.BoundStaticField("sInstance", "Landroid/view/accessibility/AccessibilityManager;", dx::kAccPrivate);
    const auto enabled = builder.BoundInstanceField("mIsEnabled", "Z", dx::kAccNone);
    builder.StaticMethod(
        "getInstance", "(Landroid/content/Context;)Landroid/view/accessibility/AccessibilityManager;",
        [context, singleton, enabled](dx::IntrinsicContext& call) {
            if (!context->UiAccessibilityUnavailable()) {
                if (auto* ledger = call.vm.Ledger())
                    ledger->RecordUnimplemented("dexvm.view_accessibility_provider", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "Test accessibility provider is not configured"};
            }
            if (!call.arguments[0].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "Accessibility Context is null"};
            dx::IntrinsicCall fields(call);
            auto instance = fields.GetRef(singleton);
            if (!instance.IsValid()) {
                instance = call.vm.NewIntrinsicInstance("Landroid/view/accessibility/AccessibilityManager;");
                const auto root = call.vm.ProtectReferences(std::array{instance});
                fields.SetInt(enabled, instance, 0);
                fields.SetRef(singleton, instance);
            }
            return dx::VmValue::Ref(instance);
        });
    builder.VirtualMethod("isEnabled", "()Z", [context, enabled](dx::IntrinsicContext& call) {
        if (!context->UiAccessibilityUnavailable()) {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_accessibility_provider", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "Test accessibility provider is not configured"};
        }
        return dx::VmValue::Int(dx::IntrinsicCall(call).GetInt(enabled));
    });
    return std::move(builder).Build();
}
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_test_ListenerInfo(const Context& context) {
    auto builder =
        dx::IntrinsicClassBuilder::Class("Landroid/view/View$ListenerInfo;", "Ljava/lang/Object;", {}, dx::kAccNone);
    context->view_java_listener_fields = std::array{
        builder.BoundInstanceField("mOnClickListener", "Landroid/view/View$OnClickListener;", dx::kAccPublic),
        builder.BoundInstanceField("mOnTouchListener", "Landroid/view/View$OnTouchListener;", dx::kAccPrivate),
        builder.BoundInstanceField("mOnLongClickListener", "Landroid/view/View$OnLongClickListener;",
                                   dx::kAccProtected),
        builder.BoundInstanceField("mOnFocusChangeListener", "Landroid/view/View$OnFocusChangeListener;",
                                   dx::kAccProtected)};
    return std::move(builder).Build();
}
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_LayoutInflater_BlinkLayout(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/LayoutInflater$BlinkLayout;",
                                                    "Landroid/widget/FrameLayout;", {}, 0);
    builder.Constructor(
        "(Landroid/content/Context;Landroid/util/AttributeSet;)V", [](dx::IntrinsicContext& call) -> dx::VmValue {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.view_inflater_blink", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "blink layout animation is unsupported"};
        });
    return std::move(builder).Build();
}
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
dx::VmObjectRef ManagedViewWindowRoot(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef view) {
    dx::IntrinsicContext call{vm, view, {}};
    dx::IntrinsicCall fields(call);
    for (std::size_t depth = 0; view.IsValid() && depth <= ui::UiTree::kMaxDepth; ++depth) {
        const auto attached = context.ui_window_schedules.find(view);
        if (attached != context.ui_window_schedules.end() && attached->second.generation != 0) {
            if (view == AndroidUiTraversalRoot(vm, context)) return view;
            for (const auto& record : context.activity_stack)
                if (record.java_root.IsValid() && record.java_root == view) return view;
            for (const auto& window : context.application_windows)
                if (!window.removed && window.root == view) return view;
        }
        view = fields.GetRef(context.view_java_hierarchy_fields->parent, view);
    }
    return dx::VmObjectRef{};
}

namespace {
dx::Slot& FloatingObserver(dx::Interpreter& vm, dx::VmObjectRef view) {
    const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(view), "mFloatingTreeObserver",
                                                      "Landroid/view/ViewTreeObserver;");
    if (!field || vm.Linker().Field(*field).is_unimplemented)
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Missing paired floating observer field"};
    return vm.Model().InstanceSlots(view)[vm.Linker().Field(*field).slot];
}

dx::VmObjectRef NewRomObserver(dx::Interpreter& vm) {
    const auto object = vm.NewIntrinsicInstance("Landroid/view/ViewTreeObserver;");
    const auto roots = vm.ProtectReferences(std::array{object});
    const auto method = vm.Linker().FindDirectMethod(vm.Model().ObjectClass(object), "<init>", "()V");
    const auto outcome = vm.Call(*method, std::array{dx::VmValue::Ref(object)});
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
    return object;
}
}  // namespace

dx::VmObjectRef ManagedViewTreeObserver(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef view) {
    if (vm.CurrentContextToken() != 1U || context.ui_java_owner_thread != std::this_thread::get_id())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Window observer requires owner thread"};
    const auto roots = vm.ProtectReferences(std::array{view});
    const auto root = ManagedViewWindowRoot(vm, context, view);
    if (!root.IsValid()) {
        auto observer = dx::VmObjectRef(FloatingObserver(vm, view).bits);
        if (!observer.IsValid()) {
            observer = NewRomObserver(vm);
            FloatingObserver(vm, view) = {observer.Value(), dx::SlotTag::ref};
        }
        return observer;
    }
    auto observer = context.view_tree_observers[root.Value()];
    if (!observer.IsValid()) {
        observer = NewRomObserver(vm);
        context.view_tree_observers[root.Value()] = observer;
    }
    const auto floating = dx::VmObjectRef(FloatingObserver(vm, view).bits);
    if (floating.IsValid()) {
        const auto protection = vm.ProtectReferences(std::array{observer, floating});
        static_cast<void>(CallAndroidMethod(vm, observer, "merge", "(Landroid/view/ViewTreeObserver;)V",
                                            {dx::VmValue::Ref(floating)}));
        FloatingObserver(vm, view) = {0, dx::SlotTag::ref};
    }
    return observer;
}

void MergeManagedViewTreeObservers(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef subtree) {
    if (!subtree.IsValid() || !ManagedViewWindowRoot(vm, context, subtree).IsValid()) return;
    const auto roots = vm.ProtectReferences(std::array{subtree});
    if (FloatingObserver(vm, subtree).bits) static_cast<void>(ManagedViewTreeObserver(vm, context, subtree));
    const auto group = vm.Linker().ResolveDescriptor("Landroid/view/ViewGroup;");
    if (!vm.Linker().IsAssignable(group, vm.Model().ObjectClass(subtree))) return;
    const auto count = CallAndroidMethod(vm, subtree, "getChildCount", "()I").AsInt();
    for (int index = 0; index < count; ++index) {
        const auto child =
            CallAndroidMethod(vm, subtree, "getChildAt", "(I)Landroid/view/View;", {dx::VmValue::Int(index)}).ref;
        MergeManagedViewTreeObservers(vm, context, child);
    }
}
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
dx::VmObjectRef ManagedWindowToken(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef view) {
    if (vm.CurrentContextToken() != 1U || context.ui_java_owner_thread != std::this_thread::get_id())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Window token requires owner thread"};
    const auto root = ManagedViewWindowRoot(vm, context, view);
    if (!root.IsValid()) return dx::VmObjectRef{};
    auto& schedule = context.ui_window_schedules.at(root);
    if (!schedule.window_token.IsValid()) {
        const auto token = vm.NewIntrinsicInstance("Logplay/runtime/ManagedWindowToken;");
        context.managed_window_tokens.emplace(token.Value(),
                                              DexVmAndroidContext::ManagedTokenState{root, schedule.generation});
        schedule.window_token = token;
    }
    return schedule.window_token;
}

Decl Declare_managed_WindowToken(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Logplay/runtime/ManagedWindowToken;", "Ljava/lang/Object;",
                                              {"Landroid/os/IBinder;"}, dx::kAccPublic | dx::kAccFinal);
    const auto alive = [context](dx::IntrinsicContext& c) {
        const auto state = context->managed_window_tokens.find(c.receiver.Value());
        if (state == context->managed_window_tokens.end()) return dx::VmValue::Int(0);
        const auto window = context->ui_window_schedules.find(state->second.root);
        const bool valid = window != context->ui_window_schedules.end() && window->second.window_token == c.receiver &&
                           ManagedViewWindowRoot(c.vm, *context, state->second.root) == state->second.root;
        return dx::VmValue::Int(valid);
    };
    b.VirtualMethod("isBinderAlive", "()Z", alive);
    b.VirtualMethod("pingBinder", "()Z", alive);
    const auto unsupported = [](dx::IntrinsicContext& c) -> dx::VmValue {
        if (auto* ledger = c.vm.Ledger()) ledger->RecordUnimplemented("dexvm.managed_window_token_ipc", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "managed window token is process-local; Binder operations are unsupported"};
    };
    b.VirtualMethod("transact", "(ILandroid/os/Parcel;Landroid/os/Parcel;I)Z", unsupported);
    b.VirtualMethod("getInterfaceDescriptor", "()Ljava/lang/String;", unsupported);
    b.VirtualMethod("queryLocalInterface", "(Ljava/lang/String;)Landroid/os/IInterface;", unsupported);
    b.VirtualMethod("dump", "(Ljava/io/FileDescriptor;[Ljava/lang/String;)V", unsupported);
    b.VirtualMethod("dumpAsync", "(Ljava/io/FileDescriptor;[Ljava/lang/String;)V", unsupported);
    b.VirtualMethod("linkToDeath", "(Landroid/os/IBinder$DeathRecipient;I)V", unsupported);
    b.VirtualMethod("unlinkToDeath", "(Landroid/os/IBinder$DeathRecipient;I)Z", unsupported);
    return std::move(b).Build();
}
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime {
dexvm::VmObjectRef AndroidActivityWindowManager(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                dexvm::VmObjectRef owner) {
    const auto record = std::find_if(context.activity_stack.begin(), context.activity_stack.end(),
                                     [&](const auto& item) { return item.object == owner; });
    if (record == context.activity_stack.end())
        throw dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Activity has no managed window"};
    if (!record->window_manager.IsValid()) {
        const auto manager = vm.NewIntrinsicInstance("Landroid/view/WindowManagerImpl;");
        const auto roots = vm.ProtectReferences(std::array{manager});
        dexvm::IntrinsicContext call{vm, manager, {}};
        dexvm::IntrinsicCall(call).SetRef(*context.window_manager_owner_field, owner);
        const auto token = vm.NewIntrinsicInstance("Logplay/runtime/ManagedWindowToken;");
        record->app_window_token = token;
        record->window_manager = manager;
        const auto window =
            android_intrinsics::CallAndroidMethod(vm, owner, "getWindow", "()Landroid/view/Window;").ref;
        const auto decor =
            android_intrinsics::CallAndroidMethod(vm, window, "getDecorView", "()Landroid/view/View;").ref;
        context.ui_window_schedules[decor].window_token = token;
        context.managed_window_tokens.emplace(
            token.Value(), DexVmAndroidContext::ManagedTokenState{decor, context.ui_tree.Generation()});
    }
    return record->window_manager;
}
}  // namespace ogplay::runtime

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_WindowManager_BadTokenException(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/WindowManager$BadTokenException;",
                                                    "Ljava/lang/RuntimeException;");
    builder.Constructor("()V", [](dx::IntrinsicContext& call) {
        call.vm.InitializeThrowable(call.receiver);
        return dx::VmValue::Void();
    });
    builder.Constructor("(Ljava/lang/String;)V", [](dx::IntrinsicContext& call) {
        call.vm.InitializeThrowable(call.receiver, call.arguments[0].ref);
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_animation_PropertyValuesHolder(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/animation/PropertyValuesHolder;");
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

Decl Declare_android_view_DisplayEventReceiver(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/DisplayEventReceiver;", "Ljava/lang/Object;");
    builder.StaticMethod(
        "nativeInit", "(Landroid/view/DisplayEventReceiver;Landroid/os/MessageQueue;)I",
        [context](dx::IntrinsicContext& call) {
            const auto receiver = call.arguments[0].ref, queue = call.arguments[1].ref;
            std::scoped_lock lock(context->scheduler_mutex);
            const auto looper = std::find_if(context->loopers.begin(), context->loopers.end(), [&](const auto& item) {
                return queue.IsValid() && item.second.queue == queue && !item.second.quitting;
            });
            if (!receiver.IsValid() || context->scheduler_shutdown || context->process_stopping ||
                looper == context->loopers.end())
                throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;", "MessageQueue is not initialized."};
            if (!looper->second.main || looper->second.context_token != call.vm.CurrentContextToken()) {
                if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.choreographer_frame_source", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "DisplayEventReceiver requires the main Looper thread"};
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

Decl Declare_android_hardware_display_DisplayManagerGlobal(const Context& context) {
    auto builder =
        dx::IntrinsicClassBuilder::Class("Landroid/hardware/display/DisplayManagerGlobal;", "Ljava/lang/Object;");
    const auto singleton =
        builder.BoundStaticField("sInstance", "Landroid/hardware/display/DisplayManagerGlobal;", dx::kAccPrivate);
    builder.StaticMethod(
        "getInstance", "()Landroid/hardware/display/DisplayManagerGlobal;", [singleton](dx::IntrinsicContext& call) {
            dx::IntrinsicCall fields(call);
            auto instance = fields.GetRef(singleton);
            if (!instance.IsValid()) {
                instance = call.vm.NewIntrinsicInstance("Landroid/hardware/display/DisplayManagerGlobal;");
                const auto roots = call.vm.ProtectReferences(std::array{instance});
                // Preserve Java-owned lock/cache/listener initialization; the
                // supported display query uses injected facts instead of Binder.
                const auto init = call.vm.Linker().FindDirectMethod(call.vm.Model().ObjectClass(instance), "<init>",
                                                                    "(Landroid/hardware/display/IDisplayManager;)V");
                if (!init) throw std::logic_error("DisplayManagerGlobal constructor is not linked");
                const auto outcome =
                    call.vm.Call(*init, std::array{dx::VmValue::Ref(instance), dx::VmValue::Ref(dx::VmObjectRef{})});
                if (outcome.exception.IsValid())
                    throw dx::VmJavaThrow{call.vm.Linker().Class(outcome.exception_class).descriptor,
                                          outcome.exception_message, outcome.exception};
                fields.SetRef(singleton, instance);
            }
            return dx::VmValue::Ref(instance);
        });
    builder.VirtualMethod("getDisplayInfo", "(I)Landroid/view/DisplayInfo;", [context](dx::IntrinsicContext& call) {
        if (call.arguments[0].AsInt() != 0) return dx::VmValue::Ref(dx::VmObjectRef{});
        if (!std::isfinite(context->display_refresh_rate_hz) || context->display_refresh_rate_hz <= 0) {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.choreographer_frame_source", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "Logical display refresh rate is unavailable"};
        }
        const auto info = call.vm.NewIntrinsicInstance("Landroid/view/DisplayInfo;");
        const auto roots = call.vm.ProtectReferences(std::array{info});
        const auto init = call.vm.Linker().FindDirectMethod(call.vm.Model().ObjectClass(info), "<init>", "()V");
        if (!init) throw std::logic_error("DisplayInfo constructor is not linked");
        const auto initialized = call.vm.Call(*init, std::array{dx::VmValue::Ref(info)});
        if (initialized.exception.IsValid())
            throw dx::VmJavaThrow{call.vm.Linker().Class(initialized.exception_class).descriptor,
                                  initialized.exception_message, initialized.exception};
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
        write("logicalDensityDpi", "I", static_cast<std::uint32_t>(DeviceDensityDpi(context)));
        return dx::VmValue::Ref(info);
    });
    return std::move(builder).Build();
}
}  // namespace ogplay::runtime::android_intrinsics
