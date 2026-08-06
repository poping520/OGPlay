#include "ogplay/gles/gles_dispatch.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <utility>

#include "ogplay/gles/generated/gles2_catalog.h"

namespace ogplay::gles {
namespace {

namespace catalog = generated::gles2;

[[nodiscard]] std::string UnimplementedMessage(const GlesThunkId id,
                                               const std::string_view name,
                                               const std::uint64_t thread_id) {
    std::ostringstream stream;
    stream << "unimplemented GLES2 call " << name << " (thunk " << id
           << ", guest thread " << thread_id << ')';
    return stream.str();
}

[[nodiscard]] std::size_t CheckedIndex(const GlesThunkId id) {
    const auto index = static_cast<std::size_t>(id);
    if (index >= catalog::kFunctions.size()) {
        throw GlesDispatchError("GLES2 thunk id is outside the generated catalog");
    }
    return index;
}

}  // namespace

GlesUnimplementedError::GlesUnimplementedError(const GlesThunkId id,
                                               const std::string_view name,
                                               const std::uint64_t thread_id)
    : GlesDispatchError(UnimplementedMessage(id, name, thread_id)), id_(id),
      name_(name), thread_id_(thread_id) {}

GlesThunkId GlesUnimplementedError::Id() const noexcept {
    return id_;
}

std::string_view GlesUnimplementedError::Name() const noexcept {
    return name_;
}

std::uint64_t GlesUnimplementedError::ThreadId() const noexcept {
    return thread_id_;
}

GlesDispatchTable::GlesDispatchTable()
    : handlers_(catalog::kFunctions.size()),
      unimplemented_(catalog::kFunctions.size()) {
    constexpr auto kThunkCapacity =
        static_cast<std::size_t>((std::numeric_limits<GlesThunkId>::max)()) +
        1U;
    if (catalog::kFunctions.size() > kThunkCapacity) {
        throw GlesDispatchError("generated GLES2 catalog exceeds thunk id range");
    }
}

std::size_t GlesDispatchTable::FunctionCount() noexcept {
    return catalog::kFunctions.size();
}

std::optional<GlesThunkId> GlesDispatchTable::Find(
    const std::string_view name) noexcept {
    const auto found = std::lower_bound(
        catalog::kFunctions.begin(), catalog::kFunctions.end(), name,
        [](const catalog::FunctionSpec& candidate,
           const std::string_view requested) {
            return candidate.name < requested;
        });
    if (found == catalog::kFunctions.end() || found->name != name) {
        return std::nullopt;
    }
    return static_cast<GlesThunkId>(
        std::distance(catalog::kFunctions.begin(), found));
}

GlesFunctionInfo GlesDispatchTable::Describe(const GlesThunkId id) {
    const auto index = CheckedIndex(id);
    const auto& function = catalog::kFunctions[index];
    std::size_t pointer_count{};
    for (std::size_t parameter = 0; parameter < function.parameter_count;
         ++parameter) {
        if (catalog::kParameters[function.parameter_offset + parameter]
                .indirection != 0) {
            ++pointer_count;
        }
    }
    return {.id = id,
            .name = function.name,
            .return_type = function.return_type,
            .parameter_count = function.parameter_count,
            .pointer_parameter_count = pointer_count};
}

void GlesDispatchTable::Bind(const std::string_view name, GlesHandler handler) {
    const auto id = Find(name);
    if (!id) {
        throw GlesDispatchError("cannot bind a name outside the GLES2 catalog");
    }
    if (!handler) {
        throw GlesDispatchError("cannot bind an empty GLES2 handler");
    }
    std::scoped_lock lock(mutex_);
    auto& slot = handlers_[static_cast<std::size_t>(*id)];
    if (slot) {
        throw GlesDispatchError("GLES2 handler is already bound");
    }
    slot = std::move(handler);
}

bool GlesDispatchTable::IsBound(const GlesThunkId id) const {
    const auto index = CheckedIndex(id);
    std::scoped_lock lock(mutex_);
    return static_cast<bool>(handlers_[index]);
}

GlesGuestValue GlesDispatchTable::Invoke(
    const GlesThunkId id, const std::span<const GlesGuestValue> arguments,
    const std::uint64_t thread_id) {
    const auto info = Describe(id);
    if (arguments.size() != info.parameter_count) {
        throw GlesDispatchError("GLES2 call has the wrong guest argument count");
    }

    GlesHandler handler;
    {
        std::scoped_lock lock(mutex_);
        handler = handlers_[static_cast<std::size_t>(id)];
        if (!handler) {
            auto& state = unimplemented_[static_cast<std::size_t>(id)];
            if (state.hits == 0) {
                state.first_thread = thread_id;
            }
            ++state.hits;
            state.last_thread = thread_id;
        }
    }
    if (!handler) {
        throw GlesUnimplementedError(id, info.name, thread_id);
    }
    return handler(arguments, thread_id);
}

std::vector<GlesUnimplementedCall>
GlesDispatchTable::UnimplementedCalls() const {
    std::scoped_lock lock(mutex_);
    std::vector<GlesUnimplementedCall> result;
    for (std::size_t index = 0; index < unimplemented_.size(); ++index) {
        const auto& state = unimplemented_[index];
        if (state.hits == 0) {
            continue;
        }
        result.push_back(
            {.id = static_cast<GlesThunkId>(index),
             .name = catalog::kFunctions[index].name,
             .hits = state.hits,
             .first_thread = state.first_thread,
             .last_thread = state.last_thread});
    }
    return result;
}

}  // namespace ogplay::gles
