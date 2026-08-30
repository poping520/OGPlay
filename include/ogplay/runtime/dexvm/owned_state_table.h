#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <type_traits>
#include <utility>

#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::runtime::dexvm {

enum class OwnedStateTracePolicy : std::uint8_t {
    no_guest_references,
    trace_guest_references,
};

enum class OwnedStateClonePolicy : std::uint8_t {
    do_not_clone,
    clone_state,
};

// Host state whose identity and lifetime are exactly those of one guest
// object.  Raw handle values never cross this API: owner lookup, tracing,
// sweeping and cloning all use VmObjectRef.
template <class State>
class OwnedStateTable final {
public:
    using TraceState =
        std::function<void(const State&, const VmRootVisitor&)>;
    using CloneState = std::function<State(const State&)>;

    OwnedStateTable(std::string name, OwnedStateTracePolicy trace_policy,
                    OwnedStateClonePolicy clone_policy =
                        OwnedStateClonePolicy::do_not_clone,
                    TraceState trace = {}, CloneState clone = {})
        : name_(std::move(name)),
          trace_policy_(trace_policy),
          clone_policy_(clone_policy),
          trace_(std::move(trace)),
          clone_(std::move(clone)) {
        if (name_.empty()) {
            throw DexVmError(DexVmErrorReason::invalid_operand,
                             "owned state table needs a unique name");
        }
        if (trace_policy_ == OwnedStateTracePolicy::trace_guest_references &&
            !trace_) {
            throw DexVmError(DexVmErrorReason::invalid_operand,
                             "owned state table trace policy needs a hook: " +
                                 name_);
        }
        if (clone_policy_ == OwnedStateClonePolicy::clone_state && !clone_) {
            if constexpr (std::is_copy_constructible_v<State>) {
                clone_ = [](const State& state) { return state; };
            } else {
                throw DexVmError(
                    DexVmErrorReason::invalid_operand,
                    "non-copyable owned state needs a clone hook: " + name_);
            }
        }
    }

    [[nodiscard]] State* Find(const VmObjectRef owner) {
        const auto found = states_.find(owner);
        return found == states_.end() ? nullptr : &found->second;
    }
    [[nodiscard]] const State* Find(const VmObjectRef owner) const {
        const auto found = states_.find(owner);
        return found == states_.end() ? nullptr : &found->second;
    }
    [[nodiscard]] State& Ensure(const VmObjectRef owner) {
        RequireOwner(owner);
        return states_[owner];
    }
    void InsertOrAssign(const VmObjectRef owner, State state) {
        RequireOwner(owner);
        states_.insert_or_assign(owner, std::move(state));
    }
    [[nodiscard]] bool Contains(const VmObjectRef owner) const {
        return states_.contains(owner);
    }
    [[nodiscard]] bool Erase(const VmObjectRef owner) {
        return states_.erase(owner) != 0U;
    }
    [[nodiscard]] std::size_t Size() const noexcept { return states_.size(); }

    [[nodiscard]] IntrinsicStateTableHooks Hooks() {
        IntrinsicStateTableHooks hooks;
        hooks.name = name_;
        if (trace_policy_ == OwnedStateTracePolicy::trace_guest_references) {
            hooks.trace = [this](const VmObjectRef owner,
                                 const VmRootVisitor& visit) {
                const auto* state = Find(owner);
                if (state != nullptr) trace_(*state, visit);
            };
        }
        hooks.sweep = [this](const VmObjectRef owner) {
            static_cast<void>(Erase(owner));
        };
        if (clone_policy_ == OwnedStateClonePolicy::clone_state) {
            hooks.clone = [this](const VmObjectRef source,
                                 const VmObjectRef destination) {
                const auto* state = Find(source);
                if (state != nullptr) InsertOrAssign(destination, clone_(*state));
            };
        }
        return hooks;
    }

private:
    static void RequireOwner(const VmObjectRef owner) {
        if (!owner.IsValid()) {
            throw DexVmError(DexVmErrorReason::invalid_operand,
                             "owned state table owner is null");
        }
    }

    std::string name_;
    OwnedStateTracePolicy trace_policy_;
    OwnedStateClonePolicy clone_policy_;
    TraceState trace_;
    CloneState clone_;
    std::map<VmObjectRef, State> states_;
};

}  // namespace ogplay::runtime::dexvm
