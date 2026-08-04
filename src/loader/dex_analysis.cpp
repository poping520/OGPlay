#include "ogplay/loader/dex_analysis.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string_view>
#include <utility>

namespace ogplay::loader {
namespace {

constexpr std::uint32_t kAccNative = 0x0100;

[[noreturn]] void Fail(const DexAnalysisErrorReason reason,
                       const char* message) {
    throw DexAnalysisError(reason, message);
}

[[nodiscard]] bool StartsWith(const std::string& value,
                              const std::string_view prefix) {
    return value.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), value.begin());
}

[[nodiscard]] bool IsIntrinsicClass(const std::string& descriptor) {
    return StartsWith(descriptor, "Landroid/") ||
           StartsWith(descriptor, "Ljava/") ||
           StartsWith(descriptor, "Ljavax/") ||
           StartsWith(descriptor, "Ldalvik/");
}

[[nodiscard]] std::string Ascii(const DexString& string) {
    std::string result;
    result.reserve(string.value.size());
    for (const auto unit : string.value) {
        if (unit > 0x7f) return {};
        result.push_back(static_cast<char>(unit));
    }
    return result;
}

[[nodiscard]] std::string NormalizeLibraryName(std::string value) {
    const auto separator = value.find_last_of("/\\");
    if (separator != std::string::npos) value.erase(0, separator + 1);
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

[[nodiscard]] bool MatchesSignature(const DexNativeLibraryFact& library,
                                    const DexEngineSignature& signature) {
    const auto name = NormalizeLibraryName(library.name);
    const bool name_matches = signature.library_names.empty() ||
        std::any_of(signature.library_names.begin(),
                    signature.library_names.end(),
                    [&name](const std::string& candidate) {
                        return NormalizeLibraryName(candidate) == name;
                    });
    if (!name_matches) return false;
    return std::all_of(
        signature.required_symbols.begin(), signature.required_symbols.end(),
        [&library](const std::string& required) {
            return std::find(library.exported_symbols.begin(),
                             library.exported_symbols.end(), required) !=
                   library.exported_symbols.end();
        });
}

[[nodiscard]] DexJavaThickness Classify(const DexL1Report& report) {
    if (report.application_class_count == 0) return DexJavaThickness::none;
    if (report.application_class_count >= 64 ||
        report.instruction_units >= 8192 ||
        report.rendering_instruction_units >= 512) {
        return DexJavaThickness::thick;
    }
    if (report.application_class_count >= 16 ||
        report.instruction_units >= 2048 ||
        report.rendering_instruction_units >= 128) {
        return DexJavaThickness::moderate;
    }
    return DexJavaThickness::thin;
}

void CountMethod(const DexImage& image, const DexEncodedMethod& method,
                 DexL1Report& report) {
    if (method.method_index >= image.methods.size()) {
        Fail(DexAnalysisErrorReason::invalid_class_data,
             "DEX analysis method index is invalid");
    }
    ++report.declared_method_count;
    if ((method.access_flags & kAccNative) != 0) {
        ++report.native_method_count;
    }
    if (!method.code.has_value()) return;
    report.instruction_units += method.code->instruction_units;
    const auto name_index = image.methods[method.method_index].name_string_index;
    if (name_index >= image.strings.size()) {
        Fail(DexAnalysisErrorReason::invalid_class_data,
             "DEX analysis method name index is invalid");
    }
    const auto name = Ascii(image.strings[name_index]);
    if (name == "onDrawFrame" || name == "onDraw") {
        ++report.rendering_override_count;
        report.rendering_instruction_units += method.code->instruction_units;
    }
}

void ValidateSignatures(const std::span<const DexEngineSignature> signatures) {
    std::set<std::string> identifiers;
    for (const auto& signature : signatures) {
        if (signature.id.empty() ||
            (signature.library_names.empty() &&
             signature.required_symbols.empty())) {
            Fail(DexAnalysisErrorReason::invalid_signature,
                 "DEX engine signature has no identity or evidence");
        }
        if (!identifiers.insert(signature.id).second) {
            Fail(DexAnalysisErrorReason::duplicate_signature,
                 "DEX engine signature identity is duplicated");
        }
    }
}

}  // namespace

DexAnalysisError::DexAnalysisError(const DexAnalysisErrorReason reason,
                                   const char* message)
    : std::runtime_error(message), reason_(reason) {}
DexAnalysisErrorReason DexAnalysisError::Reason() const noexcept {
    return reason_;
}

DexL1Report AnalyzeDexL1(
    const DexImage& image, const std::span<const DexClassData> class_data,
    const std::span<const DexNativeLibraryFact> libraries,
    const std::span<const DexEngineSignature> signatures) {
    if (class_data.size() != image.classes.size()) {
        Fail(DexAnalysisErrorReason::invalid_class_data,
             "DEX analysis class_data count does not match class definitions");
    }
    ValidateSignatures(signatures);
    DexL1Report report;
    report.class_count = image.classes.size();
    std::set<std::uint32_t> seen_classes;
    for (const auto& data : class_data) {
        if (data.class_def_index >= image.classes.size() ||
            !seen_classes.insert(data.class_def_index).second) {
            Fail(DexAnalysisErrorReason::invalid_class_data,
                 "DEX analysis class_data index is invalid or duplicated");
        }
        const auto& definition = image.classes[data.class_def_index];
        if (definition.class_type_index >= image.types.size()) {
            Fail(DexAnalysisErrorReason::invalid_class_data,
                 "DEX analysis class type index is invalid");
        }
        if (IsIntrinsicClass(
                image.types[definition.class_type_index].descriptor)) {
            continue;
        }
        ++report.application_class_count;
        for (const auto& method : data.direct_methods) {
            CountMethod(image, method, report);
        }
        for (const auto& method : data.virtual_methods) {
            CountMethod(image, method, report);
        }
    }
    report.java_thickness = Classify(report);
    report.dex_execution_candidate =
        report.java_thickness == DexJavaThickness::moderate ||
        report.java_thickness == DexJavaThickness::thick;

    for (const auto& signature : signatures) {
        if (std::any_of(libraries.begin(), libraries.end(),
                        [&signature](const DexNativeLibraryFact& library) {
                            return MatchesSignature(library, signature);
                        })) {
            report.engine_fingerprints.push_back(signature.id);
        }
    }
    return report;
}

}  // namespace ogplay::loader
