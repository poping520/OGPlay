#include "ogplay/session/profile_native_calls.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/runtime/jni/jni_native_export.h"
#include "ogplay/runtime/jni/jni_signature.h"

namespace ogplay::session {
namespace {

[[nodiscard]] const loader::Elf32Symbol* FindExport(
    const loader::Elf32SymbolTable& symbols, const std::string_view name) {
    const loader::Elf32Symbol* result{};
    for (const auto& symbol : symbols.symbols) {
        if (symbol.name != name || !symbol.IsExported()) continue;
        if (result != nullptr) {
            throw ProfileNativeCallError("duplicate exported JNI symbol: " +
                                         std::string(name));
        }
        result = &symbol;
    }
    return result;
}

[[nodiscard]] const ProfileNativeClassReference& FindClassReference(
    const std::span<const ProfileNativeClassReference> references,
    const std::string_view class_name) {
    const ProfileNativeClassReference* result{};
    for (const auto& reference : references) {
        if (reference.class_name != class_name) continue;
        if (result != nullptr) {
            throw ProfileNativeCallError(
                "duplicate native class reference: " +
                std::string(class_name));
        }
        result = &reference;
    }
    if (result == nullptr) {
        throw ProfileNativeCallError(
            "missing native class reference: " + std::string(class_name));
    }
    return *result;
}

void ValidateInvocationInputs(
    const std::span<const ProfileNativeCall> calls,
    const std::span<const ProfileNativeCallTarget> targets,
    const std::span<const ProfileNativeClassReference> references,
    const ProfileNativeInvocationContext& context) {
    if (context.environment.IsNull()) {
        throw ProfileNativeCallError(
            "profile native invocation requires a guest JNIEnv");
    }
    if (context.surface.width == 0 || context.surface.height == 0 ||
        context.surface.width > 16384U || context.surface.height > 16384U) {
        throw ProfileNativeCallError(
            "profile native invocation surface is invalid");
    }
    if (targets.size() != calls.size()) {
        throw ProfileNativeCallError(
            "profile native call targets do not cover every call");
    }
    for (std::size_t index = 0; index < targets.size(); ++index) {
        if (targets[index].call_index != index ||
            targets[index].export_name.empty() ||
            targets[index].address.IsNull()) {
            throw ProfileNativeCallError(
                "profile native call target order or identity is invalid");
        }
    }
    for (const auto& reference : references) {
        if (reference.class_name.empty() ||
            std::none_of(calls.begin(), calls.end(),
                         [&reference](const ProfileNativeCall& call) {
                             return call.class_name == reference.class_name;
                         })) {
            throw ProfileNativeCallError(
                "native class reference is empty or not used by the profile");
        }
        static_cast<void>(
            FindClassReference(references, reference.class_name));
    }
    for (const auto& call : calls) {
        static_cast<void>(
            FindClassReference(references, call.class_name));
    }
}

[[nodiscard]] std::uint32_t ResolveArgument(
    const ProfileNativeArgument& argument,
    const ProfileNativeInvocationContext& context) {
    switch (argument.source) {
    case ProfileNativeArgumentSource::constant:
        return argument.value;
    case ProfileNativeArgumentSource::surface_width:
        return context.surface.width;
    case ProfileNativeArgumentSource::surface_height:
        return context.surface.height;
    case ProfileNativeArgumentSource::input_x:
    case ProfileNativeArgumentSource::input_y:
    case ProfileNativeArgumentSource::input_pointer:
    case ProfileNativeArgumentSource::input_key:
        if (!context.input.has_value()) {
            throw ProfileNativeCallError(
                "profile native input argument has no current input");
        }
        if (argument.source == ProfileNativeArgumentSource::input_x) {
            return context.input->x;
        }
        if (argument.source == ProfileNativeArgumentSource::input_y) {
            return context.input->y;
        }
        if (argument.source == ProfileNativeArgumentSource::input_pointer) {
            return context.input->pointer;
        }
        return context.input->key;
    }
    throw ProfileNativeCallError(
        "profile native argument source is invalid");
}

[[nodiscard]] bool IsInvocationIntegerKind(
    const runtime::JniTypeKind kind) noexcept {
    return kind == runtime::JniTypeKind::boolean ||
           kind == runtime::JniTypeKind::byte ||
           kind == runtime::JniTypeKind::character ||
           kind == runtime::JniTypeKind::short_integer ||
           kind == runtime::JniTypeKind::integer;
}

}  // namespace

std::vector<ProfileNativeCallTarget> ResolveProfileNativeCalls(
    const std::span<const ProfileNativeCall> calls,
    const loader::Elf32SymbolTable& root_symbols,
    const memory::GuestAddress root_load_bias) {
    std::vector<ProfileNativeCallTarget> result;
    result.reserve(calls.size());
    for (std::size_t index = 0; index < calls.size(); ++index) {
        const auto& call = calls[index];
        runtime::JniNativeExportNames names;
        try {
            names = runtime::BuildJniNativeExportNames(
                call.class_name, call.method, call.signature);
        } catch (const std::exception& error) {
            throw ProfileNativeCallError(
                "invalid profiled JNI call at index " + std::to_string(index) +
                ": " + error.what());
        }
        const auto* symbol = FindExport(root_symbols, names.short_name);
        std::string export_name = names.short_name;
        if (symbol == nullptr) {
            symbol = FindExport(root_symbols, names.long_name);
            export_name = names.long_name;
        }
        if (symbol == nullptr) {
            throw ProfileNativeCallError(
                "profiled JNI call has no root-library export: " +
                names.short_name + " or " + names.long_name);
        }
        constexpr std::uint16_t kSectionAbsolute = 0xfff1;
        auto address = symbol->value;
        if (symbol->section_index != kSectionAbsolute) {
            try {
                address = root_load_bias.Add(symbol->value.Value());
            } catch (const std::overflow_error&) {
                throw ProfileNativeCallError(
                    "profiled JNI export address overflowed: " + export_name);
            }
        }
        result.push_back({index, std::move(export_name), address});
    }
    return result;
}

std::vector<ProfileNativeInvocation> BuildProfileNativeInvocations(
    const std::span<const ProfileNativeCall> calls,
    const std::span<const ProfileNativeCallTarget> targets,
    const ProfileNativeCallPhase phase,
    const std::span<const ProfileNativeClassReference> class_references,
    const ProfileNativeInvocationContext& context) {
    ValidateInvocationInputs(calls, targets, class_references, context);
    std::vector<ProfileNativeInvocation> result;
    for (std::size_t index = 0; index < calls.size(); ++index) {
        const auto& call = calls[index];
        if (call.phase != phase) continue;
        runtime::JniMethodDescriptor descriptor;
        try {
            descriptor = runtime::ParseJniMethodDescriptor(call.signature);
        } catch (const runtime::JniSignatureError& error) {
            throw ProfileNativeCallError(
                "profile native invocation has invalid signature: " +
                std::string(error.what()));
        }
        if (descriptor.parameters.size() != call.arguments.size()) {
            throw ProfileNativeCallError(
                "profile native invocation argument count does not match signature");
        }
        if (std::any_of(
                descriptor.parameters.begin(), descriptor.parameters.end(),
                [](const runtime::JniTypeDescriptor& parameter) {
                    return !IsInvocationIntegerKind(parameter.kind);
                })) {
            throw ProfileNativeCallError(
                "profile native invocation requires integer JNI arguments");
        }
        const auto& reference =
            FindClassReference(class_references, call.class_name);
        const auto receiver =
            call.dispatch == ProfileNativeDispatch::instance
                ? reference.instance
                : reference.static_class;
        if (receiver.IsNull()) {
            throw ProfileNativeCallError(
                "profile native invocation receiver is null");
        }

        std::vector<std::uint32_t> words{
            context.environment.Value(), receiver.Value()};
        words.reserve(call.arguments.size() + 2U);
        for (const auto& argument : call.arguments) {
            words.push_back(ResolveArgument(argument, context));
        }
        ProfileNativeInvocation invocation{
            index, targets[index].export_name, targets[index].address};
        const auto register_count =
            std::min(words.size(), invocation.registers.size());
        std::copy_n(words.begin(), register_count,
                    invocation.registers.begin());
        if (words.size() > invocation.registers.size()) {
            invocation.stack_words.assign(
                words.begin() +
                    static_cast<std::vector<std::uint32_t>::difference_type>(
                        invocation.registers.size()),
                words.end());
            if ((invocation.stack_words.size() & 1U) != 0U) {
                invocation.stack_words.push_back(0U);
            }
        }
        result.push_back(std::move(invocation));
    }
    return result;
}

}  // namespace ogplay::session
