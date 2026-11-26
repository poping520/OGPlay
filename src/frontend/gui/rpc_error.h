#pragma once

#include <stdexcept>
#include <string>
#include "ogplay/app/json.h"
#include "ogplay/app/ui_message.h"

namespace ogplay::frontend {
// GUI failures name a message of the web UI catalogs (host.*); see app::UiMessage.
using GuiTextError = app::UiError<std::runtime_error>;
using GuiArgumentError = app::UiError<std::invalid_argument>;

// A UI message on the wire: {"text": diagnostic text, "id": catalog ID, "params": {...}}; id and params are left out
// when the failure carries no message, and the page then shows text as it is.
inline app::JsonWriter::Value UiTextJson(app::JsonWriter& writer, std::string_view text, const app::UiMessage* ui) {
    const auto value = writer.Object();
    writer.AddString(value, "text", text);
    if (ui) {
        writer.AddString(value, "id", ui->id);
        if (!ui->params.empty()) {
            const auto params = writer.Object();
            for (const auto& [key, param] : ui->params)
                writer.AddString(params, key, param);
            writer.Add(value, "params", params);
        }
    }
    return value;
}

inline app::JsonWriter::Value UiTextJson(app::JsonWriter& writer, const std::exception& error) {
    return UiTextJson(writer, error.what(), app::FindUiMessage(error));
}

inline app::JsonWriter::Value UiTextJson(app::JsonWriter& writer, const app::UiMessage& ui) {
    return UiTextJson(writer, ui.id, &ui);
}
}  // namespace ogplay::frontend
