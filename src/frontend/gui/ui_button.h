#pragma once

#include "imgui.h"

namespace ogplay::frontend {

// Draws a button and rejects a second visible button with the same effective
// ImGui ID in the current frame. The ID already includes the current window
// and PushID stack, so equal visible captions remain valid in distinct scopes.
[[nodiscard]] bool GuiButton(const char* label,
                             const ImVec2& size = ImVec2(0.0F, 0.0F));

}  // namespace ogplay::frontend
