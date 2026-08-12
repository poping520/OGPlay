// java.* P1 intrinsic handlers: System (arraycopy/streams), Math, boxed
// values and the collection minimum set. String/StringBuilder handlers live
// in intrinsics_string.cpp. Native halves follow AOSP
// vm/native/java_lang_System.cpp (arraycopy ordering) at the pinned
// baseline; pure-library behaviour follows the class-library documentation
// (07 §2; libcore is not vendored).

#include <bit>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>

#include "interpreter_internal.h"

namespace ogplay::runtime::dexvm {
namespace {

[[nodiscard]] std::u16string Value(IntrinsicContext& context,
                                   const VmObjectRef ref) {
    if (!ref.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "null string argument"};
    }
    return context.vm.Model().StringValue(ref);
}

[[nodiscard]] VmValue Make(IntrinsicContext& context,
                           const std::u16string& value) {
    return VmValue::Ref(context.vm.Model().NewString(value));
}

[[nodiscard]] std::u16string Widen(const std::string& value) {
    return std::u16string(value.begin(), value.end());
}

[[nodiscard]] std::string Narrow(const std::u16string& value) {
    std::string out;
    out.reserve(value.size());
    for (const auto unit : value) {
        out.push_back(unit < 0x80 ? static_cast<char>(unit) : '?');
    }
    return out;
}

// Boxed-value slot access: boxed intrinsics declare a single "value" field
// at slot 0 (wide values occupy slots 0..1).
[[nodiscard]] std::uint64_t BoxedBits(IntrinsicContext& context,
                                      const bool wide) {
    const auto slots = context.vm.Model().InstanceSlots(context.receiver);
    if (slots.size() < (wide ? 2U : 1U)) {
        throw VmJavaThrow{"Ljava/lang/IllegalStateException;",
                          "boxed value slot is missing"};
    }
    auto bits = static_cast<std::uint64_t>(slots[0].bits);
    if (wide) bits |= static_cast<std::uint64_t>(slots[1].bits) << 32U;
    return bits;
}

void SetBoxedBits(IntrinsicContext& context, const VmObjectRef receiver,
                  const std::uint64_t bits, const bool wide) {
    const auto slots = context.vm.Model().InstanceSlots(receiver);
    slots[0] = {static_cast<std::uint32_t>(bits),
                wide ? SlotTag::wide_lo : SlotTag::cat1};
    if (wide) {
        slots[1] = {static_cast<std::uint32_t>(bits >> 32U),
                    SlotTag::wide_hi};
    }
}

[[nodiscard]] VmValue MakeBoxed(IntrinsicContext& context,
                                const char* descriptor,
                                const std::uint64_t bits, const bool wide) {
    const auto instance = context.vm.NewIntrinsicInstance(descriptor);
    SetBoxedBits(context, instance, bits, wide);
    return VmValue::Ref(instance);
}

void GuestLine(IntrinsicContext& context, const std::string& line) {
    auto* logger = context.vm.Log();
    if (logger == nullptr) return;
    logger->Write(core::LogLevel::info, "runtime.dexvm.guest", line);
}

void RegisterSystemHandlers(IntrinsicRegistry& registry) {
    registry.Register("core.system.clinit", [](IntrinsicContext& context) {
        auto& vm = context.vm;
        vm.SetIntrinsicStaticRef(
            "Ljava/lang/System;", "out", "Ljava/io/PrintStream;",
            vm.NewIntrinsicInstance("Ljava/io/PrintStream;"));
        vm.SetIntrinsicStaticRef(
            "Ljava/lang/System;", "err", "Ljava/io/PrintStream;",
            vm.NewIntrinsicInstance("Ljava/io/PrintStream;"));
        return VmValue::Void();
    });
    registry.Register("core.system.gc", [](IntrinsicContext&) {
        // GC-A never collects (04 §5); the call is legal and does nothing.
        return VmValue::Void();
    });
    // Session-lifetime property table shared by set/get within one
    // registry (one interpreter instance).
    registry.Register(
        "core.system.set_property",
        [properties = std::make_shared<
             std::unordered_map<std::string, std::string>>()](
            IntrinsicContext& context) {
            const auto key =
                context.vm.StringUtf8(context.arguments[0].ref);
            const auto value =
                context.vm.StringUtf8(context.arguments[1].ref);
            const auto previous = properties->find(key);
            VmValue result = VmValue::Ref(VmObjectRef{});
            if (previous != properties->end()) {
                result =
                    VmValue::Ref(context.vm.NewStringUtf8(previous->second));
            }
            (*properties)[key] = value;
            return result;
        });
    // Deterministic PRNG family: unseeded instances take sequential seeds
    // so a replayed session reproduces exactly (unified-Clock policy).
    {
        const auto engines = std::make_shared<
            std::unordered_map<std::uint32_t, std::mt19937_64>>();
        const auto counter = std::make_shared<std::uint64_t>(0x5DEECE66DULL);
        const auto engine_of =
            [engines](IntrinsicContext& context) -> std::mt19937_64& {
            const auto found =
                engines->find(context.receiver.Value());
            if (found == engines->end()) {
                throw VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  "Random instance was never constructed"};
            }
            return found->second;
        };
        registry.Register("core.random.init",
                          [engines, counter](IntrinsicContext& context) {
            (*engines)[context.receiver.Value()] =
                std::mt19937_64((*counter)++);
            return VmValue::Void();
        });
        registry.Register("core.random.init_seed",
                          [engines](IntrinsicContext& context) {
            (*engines)[context.receiver.Value()] = std::mt19937_64(
                static_cast<std::uint64_t>(context.arguments[0].AsLong()));
            return VmValue::Void();
        });
        registry.Register("core.random.next_double",
                          [engine_of](IntrinsicContext& context) {
            auto& engine = engine_of(context);
            VmValue out;
            out.kind = VmValue::Kind::wide;
            const double value =
                static_cast<double>(engine() >> 11U) * 0x1.0p-53;
            out.wide = std::bit_cast<std::uint64_t>(value);
            return out;
        });
        registry.Register("core.random.next_float",
                          [engine_of](IntrinsicContext& context) {
            auto& engine = engine_of(context);
            const float value =
                static_cast<float>(engine() >> 40U) * 0x1.0p-24F;
            VmValue out = VmValue::Int(0);
            out.cat1 = std::bit_cast<std::uint32_t>(value);
            return out;
        });
        registry.Register("core.random.next_int",
                          [engine_of](IntrinsicContext& context) {
            return VmValue::Int(
                static_cast<std::int32_t>(engine_of(context)()));
        });
        registry.Register("core.random.next_long",
                          [engine_of](IntrinsicContext& context) {
            return VmValue::Long(
                static_cast<std::int64_t>(engine_of(context)()));
        });
        registry.Register("core.random.next_int_bound",
                          [engine_of](IntrinsicContext& context) {
            const auto bound = context.arguments[0].AsInt();
            if (bound <= 0) {
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "bound must be positive"};
            }
            return VmValue::Int(static_cast<std::int32_t>(
                engine_of(context)() %
                static_cast<std::uint64_t>(bound)));
        });
    }
    registry.Register("core.throwable.print_stack_trace",
                      [](IntrinsicContext& context) {
        auto* logger = context.vm.Log();
        if (logger != nullptr) {
            const auto message =
                context.vm.ThrowableMessage(context.receiver);
            logger->Write(core::LogLevel::warn, "runtime.dexvm.guest",
                          "printStackTrace: " +
                              (message.IsValid()
                                   ? context.vm.StringUtf8(message)
                                   : std::string("<no message>")));
        }
        return VmValue::Void();
    });
    registry.Register("core.object.wait_timed", [](IntrinsicContext&) {
        // Cooperative single-thread model (04 §3): other threads only run
        // at lifecycle boundaries, so a timed wait cannot observe progress
        // within this tick; it returns as an elapsed timeout.
        return VmValue::Void();
    });
    registry.Register("core.system.arraycopy",
                      [](IntrinsicContext& context) {
        auto& model = context.vm.Model();
        const auto source = context.arguments[0].ref;
        const auto source_pos = context.arguments[1].AsInt();
        const auto target = context.arguments[2].ref;
        const auto target_pos = context.arguments[3].AsInt();
        const auto length = context.arguments[4].AsInt();
        if (!source.IsValid() || !target.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "arraycopy on null array"};
        }
        if (length < 0 || source_pos < 0 || target_pos < 0 ||
            source_pos > model.ArrayLength(source) - length ||
            target_pos > model.ArrayLength(target) - length) {
            throw VmJavaThrow{
                "Ljava/lang/ArrayIndexOutOfBoundsException;",
                "arraycopy range is out of bounds"};
        }
        const auto source_kind = model.Kind(source);
        const auto target_kind = model.Kind(target);
        if (source_kind == VmObjectKind::object_array &&
            target_kind == VmObjectKind::object_array) {
            // Overlap-safe copy: buffer the source range first.
            std::vector<VmObjectRef> staged;
            staged.reserve(static_cast<std::size_t>(length));
            for (std::int32_t index = 0; index < length; ++index) {
                staged.push_back(
                    model.GetObjectElement(source, source_pos + index));
            }
            for (std::int32_t index = 0; index < length; ++index) {
                model.SetObjectElement(target, target_pos + index,
                                       staged[static_cast<std::size_t>(
                                           index)]);
            }
            return VmValue::Void();
        }
        if (source_kind == VmObjectKind::primitive_array &&
            target_kind == VmObjectKind::primitive_array &&
            model.PrimitiveArrayKind(source) ==
                model.PrimitiveArrayKind(target)) {
            std::vector<std::uint64_t> staged;
            staged.reserve(static_cast<std::size_t>(length));
            for (std::int32_t index = 0; index < length; ++index) {
                staged.push_back(
                    model.GetPrimitiveElement(source, source_pos + index));
            }
            for (std::int32_t index = 0; index < length; ++index) {
                model.SetPrimitiveElement(target, target_pos + index,
                                          staged[static_cast<std::size_t>(
                                              index)]);
            }
            return VmValue::Void();
        }
        throw VmJavaThrow{"Ljava/lang/ArrayStoreException;",
                          "arraycopy element types are incompatible"};
    });
    registry.Register("core.printstream.println_string",
                      [](IntrinsicContext& context) {
        const auto argument = context.arguments[0].ref;
        GuestLine(context, argument.IsValid()
                               ? Narrow(Value(context, argument))
                               : std::string("null"));
        return VmValue::Void();
    });
    registry.Register("core.printstream.println_int",
                      [](IntrinsicContext& context) {
        GuestLine(context, std::to_string(context.arguments[0].AsInt()));
        return VmValue::Void();
    });
    registry.Register("core.printstream.println_empty",
                      [](IntrinsicContext& context) {
        GuestLine(context, "");
        return VmValue::Void();
    });
    registry.Register("core.printstream.print_string",
                      [](IntrinsicContext& context) {
        const auto argument = context.arguments[0].ref;
        GuestLine(context, argument.IsValid()
                               ? Narrow(Value(context, argument))
                               : std::string("null"));
        return VmValue::Void();
    });
}

void RegisterMathHandlers(IntrinsicRegistry& registry) {
    registry.Register("core.math.abs_int", [](IntrinsicContext& context) {
        const auto value = context.arguments[0].AsInt();
        return VmValue::Int(value < 0 ? -value : value);
    });
    registry.Register("core.math.abs_long", [](IntrinsicContext& context) {
        const auto value = context.arguments[0].AsLong();
        return VmValue::Long(value < 0 ? -value : value);
    });
    registry.Register("core.math.abs_float", [](IntrinsicContext& context) {
        return VmValue::Float(std::fabs(context.arguments[0].AsFloat()));
    });
    registry.Register("core.math.abs_double", [](IntrinsicContext& context) {
        return VmValue::Double(std::fabs(context.arguments[0].AsDouble()));
    });
    registry.Register("core.math.max_int", [](IntrinsicContext& context) {
        return VmValue::Int(std::max(context.arguments[0].AsInt(),
                                     context.arguments[1].AsInt()));
    });
    registry.Register("core.math.min_int", [](IntrinsicContext& context) {
        return VmValue::Int(std::min(context.arguments[0].AsInt(),
                                     context.arguments[1].AsInt()));
    });
    registry.Register("core.math.max_float", [](IntrinsicContext& context) {
        return VmValue::Float(std::fmax(context.arguments[0].AsFloat(),
                                        context.arguments[1].AsFloat()));
    });
    registry.Register("core.math.min_float", [](IntrinsicContext& context) {
        return VmValue::Float(std::fmin(context.arguments[0].AsFloat(),
                                        context.arguments[1].AsFloat()));
    });
    registry.Register("core.math.sqrt", [](IntrinsicContext& context) {
        return VmValue::Double(std::sqrt(context.arguments[0].AsDouble()));
    });
    registry.Register("core.math.sin", [](IntrinsicContext& context) {
        return VmValue::Double(std::sin(context.arguments[0].AsDouble()));
    });
    registry.Register("core.math.cos", [](IntrinsicContext& context) {
        return VmValue::Double(std::cos(context.arguments[0].AsDouble()));
    });
    registry.Register("core.math.atan2", [](IntrinsicContext& context) {
        return VmValue::Double(std::atan2(context.arguments[0].AsDouble(),
                                          context.arguments[1].AsDouble()));
    });
    registry.Register("core.math.pow", [](IntrinsicContext& context) {
        return VmValue::Double(std::pow(context.arguments[0].AsDouble(),
                                        context.arguments[1].AsDouble()));
    });
    registry.Register("core.math.floor", [](IntrinsicContext& context) {
        return VmValue::Double(std::floor(context.arguments[0].AsDouble()));
    });
    registry.Register("core.math.ceil", [](IntrinsicContext& context) {
        return VmValue::Double(std::ceil(context.arguments[0].AsDouble()));
    });
}

void RegisterBoxedHandlers(IntrinsicRegistry& registry) {
    registry.Register("core.integer.init", [](IntrinsicContext& context) {
        SetBoxedBits(context, context.receiver,
                     static_cast<std::uint32_t>(
                         context.arguments[0].AsInt()),
                     false);
        return VmValue::Void();
    });
    registry.Register("core.integer.value_of",
                      [](IntrinsicContext& context) {
        return MakeBoxed(context, "Ljava/lang/Integer;",
                         static_cast<std::uint32_t>(
                             context.arguments[0].AsInt()),
                         false);
    });
    registry.Register("core.integer.int_value",
                      [](IntrinsicContext& context) {
        return VmValue::Int(static_cast<std::int32_t>(
            BoxedBits(context, false)));
    });
    registry.Register("core.integer.parse_int",
                      [](IntrinsicContext& context) {
        const auto text = Narrow(Value(context, context.arguments[0].ref));
        char* end = nullptr;
        const auto value = std::strtol(text.c_str(), &end, 10);
        if (end == text.c_str() || *end != '\0') {
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "invalid int: " + text};
        }
        return VmValue::Int(static_cast<std::int32_t>(value));
    });
    registry.Register("core.integer.to_string",
                      [](IntrinsicContext& context) {
        return Make(context, Widen(std::to_string(static_cast<std::int32_t>(
                                 BoxedBits(context, false)))));
    });
    registry.Register("core.integer.to_string_static",
                      [](IntrinsicContext& context) {
        return Make(context,
                    Widen(std::to_string(context.arguments[0].AsInt())));
    });
    registry.Register("core.long.init", [](IntrinsicContext& context) {
        SetBoxedBits(context, context.receiver, context.arguments[0].wide,
                     true);
        return VmValue::Void();
    });
    registry.Register("core.long.value_of", [](IntrinsicContext& context) {
        return MakeBoxed(context, "Ljava/lang/Long;",
                         context.arguments[0].wide, true);
    });
    registry.Register("core.long.long_value",
                      [](IntrinsicContext& context) {
        return VmValue::Long(
            static_cast<std::int64_t>(BoxedBits(context, true)));
    });
    registry.Register("core.long.parse_long",
                      [](IntrinsicContext& context) {
        const auto text = Narrow(Value(context, context.arguments[0].ref));
        char* end = nullptr;
        const auto value = std::strtoll(text.c_str(), &end, 10);
        if (end == text.c_str() || *end != '\0') {
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "invalid long: " + text};
        }
        return VmValue::Long(value);
    });
    registry.Register("core.long.to_string", [](IntrinsicContext& context) {
        return Make(context, Widen(std::to_string(static_cast<std::int64_t>(
                                 BoxedBits(context, true)))));
    });
    registry.Register("core.boolean.init", [](IntrinsicContext& context) {
        SetBoxedBits(context, context.receiver,
                     context.arguments[0].AsInt() != 0 ? 1U : 0U, false);
        return VmValue::Void();
    });
    registry.Register("core.boolean.value_of",
                      [](IntrinsicContext& context) {
        return MakeBoxed(context, "Ljava/lang/Boolean;",
                         context.arguments[0].AsInt() != 0 ? 1U : 0U, false);
    });
    registry.Register("core.boolean.boolean_value",
                      [](IntrinsicContext& context) {
        return VmValue::Int(BoxedBits(context, false) != 0 ? 1 : 0);
    });
    registry.Register("core.float.clinit", [](IntrinsicContext& context) {
        auto& vm = context.vm;
        vm.SetIntrinsicStaticRef(
            "Ljava/lang/Float;", "TYPE", "Ljava/lang/Class;",
            vm.Model().ClassObject(vm.Linker().ResolveDescriptor("F")));
        return VmValue::Void();
    });
    registry.Register("core.float.init", [](IntrinsicContext& context) {
        SetBoxedBits(context, context.receiver, context.arguments[0].cat1,
                     false);
        return VmValue::Void();
    });
    registry.Register("core.float.value_of", [](IntrinsicContext& context) {
        return MakeBoxed(context, "Ljava/lang/Float;",
                         context.arguments[0].cat1, false);
    });
    registry.Register("core.float.float_value",
                      [](IntrinsicContext& context) {
        VmValue out;
        out.kind = VmValue::Kind::cat1;
        out.cat1 = static_cast<std::uint32_t>(BoxedBits(context, false));
        return out;
    });
    registry.Register("core.double.init", [](IntrinsicContext& context) {
        SetBoxedBits(context, context.receiver, context.arguments[0].wide,
                     true);
        return VmValue::Void();
    });
    registry.Register("core.double.value_of",
                      [](IntrinsicContext& context) {
        return MakeBoxed(context, "Ljava/lang/Double;",
                         context.arguments[0].wide, true);
    });
    registry.Register("core.double.double_value",
                      [](IntrinsicContext& context) {
        VmValue out;
        out.kind = VmValue::Kind::wide;
        out.wide = BoxedBits(context, true);
        return out;
    });
}

void RegisterCollectionHandlers(IntrinsicRegistry& registry) {
    const auto map_find = [](IntrinsicContext& context,
                             const VmObjectRef key) -> std::int64_t {
        const auto& entries = context.vm.MapStorage(context.receiver);
        for (std::size_t index = 0; index < entries.size(); ++index) {
            if (context.vm.JavaEquals(entries[index].first, key)) {
                return static_cast<std::int64_t>(index);
            }
        }
        return -1;
    };
    registry.Register("core.map.init", [](IntrinsicContext& context) {
        context.vm.MapStorage(context.receiver).clear();
        return VmValue::Void();
    });
    registry.Register("core.map.init_capacity",
                      [](IntrinsicContext& context) {
        context.vm.MapStorage(context.receiver).clear();
        return VmValue::Void();
    });
    registry.Register("core.map.put", [map_find](IntrinsicContext& context) {
        auto& entries = context.vm.MapStorage(context.receiver);
        const auto key = context.arguments[0].ref;
        const auto value = context.arguments[1].ref;
        const auto found = map_find(context, key);
        if (found >= 0) {
            const auto previous =
                entries[static_cast<std::size_t>(found)].second;
            entries[static_cast<std::size_t>(found)].second = value;
            return VmValue::Ref(previous);
        }
        entries.emplace_back(key, value);
        return VmValue::Ref(VmObjectRef{});
    });
    registry.Register("core.map.get", [map_find](IntrinsicContext& context) {
        const auto found = map_find(context, context.arguments[0].ref);
        if (found < 0) return VmValue::Ref(VmObjectRef{});
        return VmValue::Ref(context.vm.MapStorage(context.receiver)
                                [static_cast<std::size_t>(found)]
                                    .second);
    });
    registry.Register("core.map.contains_key",
                      [map_find](IntrinsicContext& context) {
        return VmValue::Int(
            map_find(context, context.arguments[0].ref) >= 0 ? 1 : 0);
    });
    registry.Register("core.map.remove",
                      [map_find](IntrinsicContext& context) {
        auto& entries = context.vm.MapStorage(context.receiver);
        const auto found = map_find(context, context.arguments[0].ref);
        if (found < 0) return VmValue::Ref(VmObjectRef{});
        const auto previous =
            entries[static_cast<std::size_t>(found)].second;
        entries.erase(entries.begin() + found);
        return VmValue::Ref(previous);
    });
    registry.Register("core.map.size", [](IntrinsicContext& context) {
        return VmValue::Int(static_cast<std::int32_t>(
            context.vm.MapStorage(context.receiver).size()));
    });
    registry.Register("core.map.clear", [](IntrinsicContext& context) {
        context.vm.MapStorage(context.receiver).clear();
        return VmValue::Void();
    });
    registry.Register("core.map.is_empty", [](IntrinsicContext& context) {
        return VmValue::Int(
            context.vm.MapStorage(context.receiver).empty() ? 1 : 0);
    });

    const auto check_index = [](const std::vector<VmObjectRef>& elements,
                                const std::int32_t index) {
        if (index < 0 ||
            static_cast<std::size_t>(index) >= elements.size()) {
            throw VmJavaThrow{
                "Ljava/lang/IndexOutOfBoundsException;",
                "index " + std::to_string(index) + ", size " +
                    std::to_string(elements.size())};
        }
    };
    registry.Register("core.list.init", [](IntrinsicContext& context) {
        context.vm.ListStorage(context.receiver).clear();
        return VmValue::Void();
    });
    registry.Register("core.list.init_capacity",
                      [](IntrinsicContext& context) {
        context.vm.ListStorage(context.receiver).clear();
        return VmValue::Void();
    });
    registry.Register("core.list.add", [](IntrinsicContext& context) {
        context.vm.ListStorage(context.receiver)
            .push_back(context.arguments[0].ref);
        return VmValue::Int(1);
    });
    registry.Register("core.list.add_element",
                      [](IntrinsicContext& context) {
        context.vm.ListStorage(context.receiver)
            .push_back(context.arguments[0].ref);
        return VmValue::Void();
    });
    registry.Register("core.list.get",
                      [check_index](IntrinsicContext& context) {
        auto& elements = context.vm.ListStorage(context.receiver);
        const auto index = context.arguments[0].AsInt();
        check_index(elements, index);
        return VmValue::Ref(elements[static_cast<std::size_t>(index)]);
    });
    registry.Register("core.list.set",
                      [check_index](IntrinsicContext& context) {
        auto& elements = context.vm.ListStorage(context.receiver);
        const auto index = context.arguments[0].AsInt();
        check_index(elements, index);
        const auto previous = elements[static_cast<std::size_t>(index)];
        elements[static_cast<std::size_t>(index)] =
            context.arguments[1].ref;
        return VmValue::Ref(previous);
    });
    registry.Register("core.list.set_element_at",
                      [check_index](IntrinsicContext& context) {
        auto& elements = context.vm.ListStorage(context.receiver);
        const auto index = context.arguments[1].AsInt();
        check_index(elements, index);
        elements[static_cast<std::size_t>(index)] =
            context.arguments[0].ref;
        return VmValue::Void();
    });
    registry.Register("core.list.remove_at",
                      [check_index](IntrinsicContext& context) {
        auto& elements = context.vm.ListStorage(context.receiver);
        const auto index = context.arguments[0].AsInt();
        check_index(elements, index);
        elements.erase(elements.begin() + index);
        return VmValue::Void();
    });
    registry.Register("core.list.remove_returning",
                      [check_index](IntrinsicContext& context) {
        auto& elements = context.vm.ListStorage(context.receiver);
        const auto index = context.arguments[0].AsInt();
        check_index(elements, index);
        const auto previous = elements[static_cast<std::size_t>(index)];
        elements.erase(elements.begin() + index);
        return VmValue::Ref(previous);
    });
    registry.Register("core.list.size", [](IntrinsicContext& context) {
        return VmValue::Int(static_cast<std::int32_t>(
            context.vm.ListStorage(context.receiver).size()));
    });
    registry.Register("core.list.clear", [](IntrinsicContext& context) {
        context.vm.ListStorage(context.receiver).clear();
        return VmValue::Void();
    });
    registry.Register("core.list.is_empty", [](IntrinsicContext& context) {
        return VmValue::Int(
            context.vm.ListStorage(context.receiver).empty() ? 1 : 0);
    });
    registry.Register("core.list.contains", [](IntrinsicContext& context) {
        const auto& elements = context.vm.ListStorage(context.receiver);
        for (const auto element : elements) {
            if (context.vm.JavaEquals(element, context.arguments[0].ref)) {
                return VmValue::Int(1);
            }
        }
        return VmValue::Int(0);
    });
    registry.Register("core.list.copy_into", [](IntrinsicContext& context) {
        auto& model = context.vm.Model();
        const auto& elements = context.vm.ListStorage(context.receiver);
        const auto target = context.arguments[0].ref;
        if (!target.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "copyInto target array is null"};
        }
        if (static_cast<std::size_t>(model.ArrayLength(target)) <
            elements.size()) {
            throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;",
                              "copyInto target array is too small"};
        }
        for (std::size_t index = 0; index < elements.size(); ++index) {
            model.SetObjectElement(target, static_cast<JniSize>(index),
                                   elements[index]);
        }
        return VmValue::Void();
    });
    // Iterator over the shared list storage. State lives in the instance
    // slots (list ref, cursor); mutation during iteration follows the
    // storage (no fail-fast modCount is claimed).
    registry.Register("core.list.iterator", [](IntrinsicContext& context) {
        auto& vm = context.vm;
        const auto instance =
            vm.NewIntrinsicInstance("Ljava/util/CollectionIterator;");
        const auto slots = vm.Model().InstanceSlots(instance);
        slots[0] = {context.receiver.Value(), SlotTag::ref};
        slots[1] = {0, SlotTag::cat1};
        return VmValue::Ref(instance);
    });
    registry.Register("core.iterator.has_next",
                      [](IntrinsicContext& context) {
        const auto slots = context.vm.Model().InstanceSlots(
            context.receiver);
        const auto& elements =
            context.vm.ListStorage(VmObjectRef(slots[0].bits));
        return VmValue::Int(
            static_cast<std::size_t>(slots[1].bits) < elements.size() ? 1
                                                                      : 0);
    });
    registry.Register("core.iterator.next", [](IntrinsicContext& context) {
        const auto slots = context.vm.Model().InstanceSlots(
            context.receiver);
        const auto& elements =
            context.vm.ListStorage(VmObjectRef(slots[0].bits));
        const auto index = static_cast<std::size_t>(slots[1].bits);
        if (index >= elements.size()) {
            throw VmJavaThrow{"Ljava/util/NoSuchElementException;",
                              "iterator is exhausted"};
        }
        slots[1] = {static_cast<std::uint32_t>(index + 1), SlotTag::cat1};
        return VmValue::Ref(elements[index]);
    });
}

}  // namespace

void RegisterJavaCoreBuiltins(IntrinsicRegistry& registry) {
    RegisterJavaStringBuiltins(registry);
    RegisterSystemHandlers(registry);
    RegisterMathHandlers(registry);
    RegisterBoxedHandlers(registry);
    RegisterCollectionHandlers(registry);
}

}  // namespace ogplay::runtime::dexvm
