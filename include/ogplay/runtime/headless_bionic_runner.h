#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/loader/module_loader.h"
#include "ogplay/runtime/syscall.h"

namespace ogplay::runtime {

struct HeadlessSyscallCall final {
    std::uint64_t thread_id{};
    std::uint32_t number{};
    std::array<std::uint32_t, 7> arguments{};
    std::int32_t result{};
};

struct HeadlessBionicRunRequest final {
    std::uint32_t api{};
    std::string root_module;
    std::string entry_symbol;
    std::span<const loader::Elf32ModuleInput> modules;
    std::string vfs_path;
    std::uint64_t maximum_ticks{UINT64_C(200000000)};
    std::function<void(const HeadlessSyscallCall&)> syscall_observer;
};

struct HeadlessBionicRunReport final {
    std::int32_t entry_result{};
    std::uint64_t ticks_consumed{};
    std::size_t guest_child_count{};
    std::vector<std::byte> output;
    std::vector<core::UnimplementedHit> unimplemented;
    std::vector<HeadlessSyscallCall> root_syscalls;
    std::vector<GuestVmaAnnotation> vma_annotations;
};

class HeadlessBionicRunError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] HeadlessBionicRunReport RunHeadlessBionicEntry(
    const HeadlessBionicRunRequest& request);

}  // namespace ogplay::runtime
