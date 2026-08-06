#include "ogplay/gles/gles_dispatch.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <utility>

#include "ogplay/gles/generated/gles2_catalog.h"
#include "ogplay/gles/generated/gles1_catalog.h"
#include "ogplay/gles/generated/gles1_extensions_catalog.h"

namespace ogplay::gles {
namespace {

namespace catalog = generated::gles2;

template <typename Functions>
std::optional<GlesThunkId> FindInCatalog(
    const Functions& functions, const std::string_view name) noexcept {
    const auto found = std::lower_bound(
        functions.begin(), functions.end(), name,
        [](const auto& candidate, const std::string_view requested) {
            return candidate.name < requested;
        });
    if (found == functions.end() || found->name != name) return std::nullopt;
    return static_cast<GlesThunkId>(std::distance(functions.begin(), found));
}

template <typename Functions, typename Parameters>
GlesFunctionInfo DescribeInCatalog(const Functions& functions,
                                   const Parameters& parameters,
                                   const GlesThunkId id,
                                   const std::string_view api_name) {
    const auto index = static_cast<std::size_t>(id);
    if (index >= functions.size()) {
        throw GlesDispatchError(std::string(api_name) +
                                " thunk id is outside the generated catalog");
    }
    const auto& function = functions[index];
    std::size_t pointer_count{};
    for (std::size_t parameter = 0; parameter < function.parameter_count;
         ++parameter) {
        if (parameters[function.parameter_offset + parameter].indirection != 0) {
            ++pointer_count;
        }
    }
    return {.id = id,
            .name = function.name,
            .return_type = function.return_type,
            .parameter_count = function.parameter_count,
            .pointer_parameter_count = pointer_count};
}

[[nodiscard]] std::string UnimplementedMessage(const GlesApi api,
                                               const GlesThunkId id,
                                               const std::string_view name,
                                               const std::uint64_t thread_id) {
    std::ostringstream stream;
    stream << "unimplemented " << (api == GlesApi::gles2 ? "GLES2" : "GLES1")
           << " call " << name << " (thunk " << id
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
    : GlesDispatchError(UnimplementedMessage(GlesApi::gles2, id, name, thread_id)), id_(id),
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

GlesDispatchTable::GlesDispatchTable(const GlesApi api)
    : api_(api), handlers_(GlesFunctionCount(api)),
      unimplemented_(GlesFunctionCount(api)) {
    constexpr auto kThunkCapacity =
        static_cast<std::size_t>((std::numeric_limits<GlesThunkId>::max)()) +
        1U;
    if (GlesFunctionCount(api) > kThunkCapacity) {
        throw GlesDispatchError("generated GLES catalog exceeds thunk id range");
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
    const auto id = FindGlesFunction(api_, name);
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
    const auto index = static_cast<std::size_t>(id);
    static_cast<void>(DescribeGlesFunction(api_, id));
    std::scoped_lock lock(mutex_);
    return static_cast<bool>(handlers_[index]);
}

GlesGuestValue GlesDispatchTable::Invoke(
    const GlesThunkId id, const std::span<const GlesGuestValue> arguments,
    const std::uint64_t thread_id) {
    const auto info = DescribeGlesFunction(api_, id);
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
        if (api_ == GlesApi::gles2) {
            throw GlesUnimplementedError(id, info.name, thread_id);
        }
        throw GlesDispatchError(UnimplementedMessage(api_, id, info.name, thread_id));
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
             .name = DescribeGlesFunction(
                         api_, static_cast<GlesThunkId>(index)).name,
             .hits = state.hits,
             .first_thread = state.first_thread,
             .last_thread = state.last_thread});
    }
    return result;
}

std::size_t GlesFunctionCount(const GlesApi api) noexcept {
    switch (api) {
    case GlesApi::gles1: return generated::gles1::kFunctions.size();
    case GlesApi::gles1_extensions:
        return generated::gles1_extensions::kFunctions.size();
    case GlesApi::gles2: return generated::gles2::kFunctions.size();
    }
    return 0;
}

std::optional<GlesThunkId> FindGlesFunction(
    const GlesApi api, const std::string_view name) noexcept {
    switch (api) {
    case GlesApi::gles1:
        return FindInCatalog(generated::gles1::kFunctions, name);
    case GlesApi::gles1_extensions:
        return FindInCatalog(generated::gles1_extensions::kFunctions, name);
    case GlesApi::gles2:
        return FindInCatalog(generated::gles2::kFunctions, name);
    }
    return std::nullopt;
}

GlesFunctionInfo DescribeGlesFunction(const GlesApi api,
                                      const GlesThunkId id) {
    switch (api) {
    case GlesApi::gles1:
        return DescribeInCatalog(generated::gles1::kFunctions,
                                 generated::gles1::kParameters, id, "GLES1");
    case GlesApi::gles1_extensions:
        return DescribeInCatalog(generated::gles1_extensions::kFunctions,
                                 generated::gles1_extensions::kParameters, id,
                                 "GLES1 extensions");
    case GlesApi::gles2:
        return DescribeInCatalog(generated::gles2::kFunctions,
                                 generated::gles2::kParameters, id, "GLES2");
    }
    throw GlesDispatchError("unknown GLES API catalog");
}

}  // namespace ogplay::gles
