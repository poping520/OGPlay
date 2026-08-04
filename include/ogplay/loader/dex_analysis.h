#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "ogplay/loader/dex_class_data.h"

namespace ogplay::loader {

enum class DexJavaThickness : std::uint8_t {
    none,
    thin,
    moderate,
    thick,
};

struct DexNativeLibraryFact final {
    std::string name;
    std::vector<std::string> exported_symbols;
};

struct DexEngineSignature final {
    std::string id;
    std::vector<std::string> library_names;
    std::vector<std::string> required_symbols;
};

struct DexL1Report final {
    std::size_t class_count{};
    std::size_t application_class_count{};
    std::size_t declared_method_count{};
    std::size_t native_method_count{};
    std::uint64_t instruction_units{};
    std::uint64_t rendering_instruction_units{};
    std::size_t rendering_override_count{};
    DexJavaThickness java_thickness{DexJavaThickness::none};
    bool dex_execution_candidate{};
    std::vector<std::string> engine_fingerprints;
};

enum class DexAnalysisErrorReason : std::uint8_t {
    invalid_class_data,
    invalid_signature,
    duplicate_signature,
};

class DexAnalysisError final : public std::runtime_error {
public:
    DexAnalysisError(DexAnalysisErrorReason reason, const char* message);
    [[nodiscard]] DexAnalysisErrorReason Reason() const noexcept;

private:
    DexAnalysisErrorReason reason_;
};

[[nodiscard]] DexL1Report AnalyzeDexL1(
    const DexImage& image, std::span<const DexClassData> class_data,
    std::span<const DexNativeLibraryFact> libraries = {},
    std::span<const DexEngineSignature> signatures = {});

}  // namespace ogplay::loader
