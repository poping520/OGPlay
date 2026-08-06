#pragma once

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "ogplay/loader/elf.h"
#include "ogplay/session/title_profile.h"

namespace ogplay::session {

struct ProfileNativeCallTarget final {
    std::size_t call_index{};
    std::string export_name;
    memory::GuestAddress address;
};

class ProfileNativeCallError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::vector<ProfileNativeCallTarget> ResolveProfileNativeCalls(
    std::span<const ProfileNativeCall> calls,
    const loader::Elf32SymbolTable& root_symbols,
    memory::GuestAddress root_load_bias);

}  // namespace ogplay::session
