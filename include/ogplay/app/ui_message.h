#pragma once
#include <exception>
#include <map>
#include <string>
#include <utility>
#include "ogplay/app/export.h"

namespace ogplay::app {
// A message for the launcher UI: a semantic ID of the web UI catalogs ("host.error.importBusy") and its ICU
// arguments. The host never words UI text itself; what() of the carrying exception stays diagnostic text for CLI
// and logs.
struct UiMessage {
    std::string id;
    std::map<std::string, std::string> params{};
};

// Exceptions that carry a UiMessage expose it here, whatever standard exception they also are.
class OGPLAYCORE_API UiMessageSource {
public:
    virtual ~UiMessageSource();
    [[nodiscard]] virtual const UiMessage& Message() const noexcept = 0;
};

template <class Base>
class UiError final : public Base, public UiMessageSource {
public:
    UiError(UiMessage message, const std::string& what) : Base(what), message_(std::move(message)) {}

    // GUI-only failures need no other wording: the ID is the diagnostic text too.
    explicit UiError(UiMessage message) : Base(message.id), message_(std::move(message)) {}

    [[nodiscard]] const UiMessage& Message() const noexcept override { return message_; }

private:
    UiMessage message_;
};

// The message an exception carries, or nullptr when it carries none (or its ID is empty).
[[nodiscard]] OGPLAYCORE_API const UiMessage* FindUiMessage(const std::exception& error) noexcept;
}  // namespace ogplay::app
