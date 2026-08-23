#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace ogplay::runtime::detail {

template <typename Signature>
class BoundaryCallback;

template <typename Result, typename... Arguments>
class BoundaryCallback<Result(Arguments...)> final {
public:
    BoundaryCallback() = default;

    template <typename Callback>
        requires (!std::is_same_v<std::remove_cvref_t<Callback>,
                                  BoundaryCallback>)
    BoundaryCallback(Callback&& callback) {
        using Stored = std::decay_t<Callback>;
        auto stored = std::make_shared<Stored>(
            std::forward<Callback>(callback));
        state_ = stored;
        invoke_ = +[](const void* state,
                      Arguments... arguments) -> Result {
            return (*static_cast<const Stored*>(state))(
                std::forward<Arguments>(arguments)...);
        };
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return invoke_ != nullptr;
    }

    Result operator()(Arguments... arguments) const {
        return invoke_(state_.get(),
                       std::forward<Arguments>(arguments)...);
    }

private:
    using InvokeFn = Result (*)(const void*, Arguments...);
    std::shared_ptr<const void> state_;
    InvokeFn invoke_{};
};

}  // namespace ogplay::runtime::detail
