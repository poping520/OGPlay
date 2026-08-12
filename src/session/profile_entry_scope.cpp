#include "ogplay/session/profile_entry_scope.h"

#include <bit>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::session {
namespace {

[[nodiscard]] std::string Descriptor(std::string binary_name) {
    for (auto& character : binary_name) {
        if (character == '.') character = '/';
    }
    return "L" + binary_name + ";";
}

[[noreturn]] void Fail(const std::string& message) {
    throw ProfileEntryScopeError(message);
}

void RequireInitialized(const runtime::dexvm::VmCallOutcome& outcome,
                        const ProfileStaticPreset& preset) {
    if (!outcome.exception.IsValid()) return;
    Fail("Profile preset class initialization failed: " +
         preset.class_name + ": " + outcome.exception_message);
}

[[nodiscard]] std::uint64_t PresetBits(
    const ProfileStaticPreset& preset,
    runtime::dexvm::Interpreter& vm) {
    return std::visit(
        [&](const auto& value) -> std::uint64_t {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, bool>) {
                return value ? 1U : 0U;
            } else if constexpr (std::is_same_v<Value, std::int64_t>) {
                return static_cast<std::uint64_t>(value);
            } else if constexpr (std::is_same_v<Value, double>) {
                if (preset.type == "F") {
                    return std::bit_cast<std::uint32_t>(
                        static_cast<float>(value));
                }
                return std::bit_cast<std::uint64_t>(value);
            } else {
                return vm.NewStringUtf8(value).Value();
            }
        },
        preset.value);
}

}  // namespace

std::string ResolveProfileLaunchDescriptor(
    const TitleProfile& profile,
    const std::optional<std::string>& manifest_launcher) {
    if (profile.runtime.entry.has_value()) {
        return Descriptor(profile.runtime.entry->launch_activity);
    }
    if (!manifest_launcher.has_value()) {
        Fail("dex_activity requires a manifest launcher or Profile entry override");
    }
    return Descriptor(*manifest_launcher);
}

void ApplyProfileStaticPresets(const TitleProfile& profile,
                               runtime::dexvm::Interpreter& vm,
                               core::Logger* logger) {
    for (const auto& preset : profile.runtime.presets) {
        const auto owner = Descriptor(preset.class_name);
        const auto java_class = vm.Linker().FindClass(owner);
        if (!java_class.has_value()) {
            Fail("Profile preset class is not in the dex: " + owner);
        }
        RequireInitialized(vm.EnsureClassInitialized(*java_class), preset);
        try {
            vm.SetStaticFieldBits(owner, preset.field, preset.type,
                                  PresetBits(preset, vm));
        } catch (const runtime::dexvm::DexVmError& error) {
            Fail("Profile preset failed for " + preset.class_name + "." +
                 preset.field + ": " + error.what());
        }
        if (logger != nullptr) {
            logger->Write(
                core::LogLevel::info, "session.profile_entry",
                "applied static field preset", {},
                {{"class", preset.class_name}, {"field", preset.field},
                 {"type", preset.type}, {"reason", preset.reason}},
                {.mode = core::RateLimitMode::none});
        }
    }
}

}  // namespace ogplay::session
