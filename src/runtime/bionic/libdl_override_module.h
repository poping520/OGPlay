#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/core/boundary_binding.h"

namespace ogplay::runtime {

// Android's libdl exports are linker service stubs rather than standalone ELF
// implementations. Route them to the process-owned namespace while retaining
// guest pointers and per-thread dlerror storage in the boundary layer.
class LibdlOverrideModule final {
public:
    LibdlOverrideModule(BoundaryCallServices& calls,
                        const BionicDynamicLinkHooks hooks) noexcept
        : calls_(calls), hooks_(hooks) {}

    [[nodiscard]] BoundaryCallServices& CallServices() noexcept { return calls_; }

    void MapErrorArena() {
        calls_.address_space.Map(
            {kErrorArenaBegin, kErrorArenaBytes},
            memory::PageProtection::read | memory::PageProtection::write);
    }

    std::uint32_t Dlopen(const A32CallFrame& call) {
        const auto path = call.Argument(0) == 0U
                              ? std::string{}
                              : ReadCString(call.Argument(0), call.ThreadId(),
                                            "dlopen");
        if (hooks_.owner == nullptr || hooks_.open == nullptr) {
            SetError(call.ThreadId(), "dlopen service is unavailable");
            return 0U;
        }
        try {
            return hooks_.open(hooks_.owner, path, call.Argument(1),
                               call.ThreadId());
        } catch (const std::exception& error) {
            SetError(call.ThreadId(), error.what());
            return 0U;
        }
    }

    std::uint32_t Dlsym(const A32CallFrame& call) {
        const auto name = ReadCString(call.Argument(1), call.ThreadId(),
                                      "dlsym");
        if (hooks_.owner == nullptr || hooks_.symbol == nullptr) {
            SetError(call.ThreadId(), "dlsym service is unavailable");
            return 0U;
        }
        try {
            return hooks_.symbol(hooks_.owner, call.Argument(0), name,
                                 call.ThreadId());
        } catch (const std::exception& error) {
            SetError(call.ThreadId(), error.what());
            return 0U;
        }
    }

    std::uint32_t Dlclose(const A32CallFrame& call) {
        if (hooks_.owner == nullptr || hooks_.close == nullptr) {
            SetError(call.ThreadId(), "dlclose service is unavailable");
            return static_cast<std::uint32_t>(-1);
        }
        try {
            return static_cast<std::uint32_t>(
                hooks_.close(hooks_.owner, call.Argument(0), call.ThreadId()));
        } catch (const std::exception& error) {
            SetError(call.ThreadId(), error.what());
            return static_cast<std::uint32_t>(-1);
        }
    }

    std::uint32_t Dlerror(const A32CallFrame& call) {
        std::scoped_lock lock(error_mutex_);
        const auto pending = errors_.find(call.ThreadId());
        if (pending == errors_.end()) return 0U;
        if (pending->second.size() >= kErrorSlotBytes) {
            throw std::length_error("dlerror text exceeds its guest slot");
        }
        auto [slot, inserted] = error_slots_.try_emplace(
            call.ThreadId(), error_slots_.size());
        if (inserted && slot->second >= kMaximumErrorThreads) {
            error_slots_.erase(slot);
            throw std::length_error("dlerror guest thread capacity exceeded");
        }
        const auto address = kErrorArenaBegin.Add(slot->second * kErrorSlotBytes);
        std::vector<std::byte> bytes;
        bytes.reserve(pending->second.size() + 1U);
        for (const auto character : pending->second) {
            bytes.push_back(static_cast<std::byte>(
                static_cast<unsigned char>(character)));
        }
        bytes.push_back(std::byte{});
        calls_.address_space.Write(address, bytes, call.ThreadId());
        errors_.erase(pending);
        return address.Value();
    }

private:
    [[nodiscard]] std::string ReadCString(
        const std::uint32_t address, const std::uint64_t thread_id,
        const std::string_view operation) const {
        if (address == 0U) {
            throw std::invalid_argument(std::string(operation) +
                                        " requires a symbol name");
        }
        const auto length = calls_.address_space.CStringLength(
            memory::GuestAddress{address}, kMaximumInputBytes, thread_id);
        std::string value(length, '\0');
        calls_.address_space.Read(memory::GuestAddress{address},
                                  std::as_writable_bytes(std::span(value)),
                                  thread_id);
        return value;
    }

    void SetError(const std::uint64_t thread_id, const std::string_view error) {
        std::scoped_lock lock(error_mutex_);
        errors_.insert_or_assign(thread_id, error);
    }

    static constexpr memory::GuestAddress kErrorArenaBegin{0x71d00000U};
    static constexpr std::size_t kErrorSlotBytes = 4096U;
    static constexpr std::size_t kMaximumErrorThreads = 256U;
    static constexpr std::size_t kErrorArenaBytes =
        kErrorSlotBytes * kMaximumErrorThreads;
    static constexpr std::size_t kMaximumInputBytes = 4096U;

    BoundaryCallServices& calls_;
    BionicDynamicLinkHooks hooks_;
    std::mutex error_mutex_;
    std::unordered_map<std::uint64_t, std::string> errors_;
    std::unordered_map<std::uint64_t, std::size_t> error_slots_;
};

}  // namespace ogplay::runtime
