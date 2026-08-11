#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ogplay/loader/link_namespace.h"
#include "ogplay/runtime/execution/guest_thread_runner.h"

namespace ogplay::runtime {

struct JniGuestLibraryOnLoad final {
    std::size_t module_index{};
    std::string module_name;
    A32GuestCallFrame call;
};

[[nodiscard]] std::optional<JniGuestLibraryOnLoad>
BuildJniGuestLibraryOnLoad(
    const loader::Elf32LinkNamespace& link_namespace,
    std::string_view root_module, memory::GuestAddress java_vm);

void ValidateJniGuestLibraryOnLoadResult(std::uint32_t result);

}  // namespace ogplay::runtime
