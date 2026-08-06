#include "ogplay/session/profile_native_calls.h"

#include <cstddef>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/runtime/jni/jni_native_export.h"

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

}  // namespace ogplay::session
