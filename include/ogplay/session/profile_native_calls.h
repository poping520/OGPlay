#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/loader/elf.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/session/title_profile.h"

namespace ogplay::session {

struct ProfileNativeCallTarget final {
    std::size_t call_index{};
    std::string export_name;
    memory::GuestAddress address;
};

struct ProfileNativeClassReference final {
    std::string class_name;
    runtime::JniReference instance;
    runtime::JniReference static_class;
};

struct ProfileNativeInputArguments final {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t pointer{};
    std::uint32_t key{};
};

struct ProfileNativeInvocationContext final {
    memory::GuestAddress environment;
    ProfileSurface surface;
    std::optional<ProfileNativeInputArguments> input;
};

struct ProfileNativeInvocation final {
    std::size_t call_index{};
    std::string export_name;
    memory::GuestAddress address;
    std::array<std::uint32_t, 4> registers{};
    std::vector<std::uint32_t> stack_words;
};

class ProfileNativeCallError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::vector<ProfileNativeCallTarget> ResolveProfileNativeCalls(
    std::span<const ProfileNativeCall> calls,
    const loader::Elf32SymbolTable& root_symbols,
    memory::GuestAddress root_load_bias);

[[nodiscard]] std::vector<ProfileNativeInvocation>
BuildProfileNativeInvocations(
    std::span<const ProfileNativeCall> calls,
    std::span<const ProfileNativeCallTarget> targets,
    ProfileNativeCallPhase phase,
    std::span<const ProfileNativeClassReference> class_references,
    const ProfileNativeInvocationContext& context);

}  // namespace ogplay::session
