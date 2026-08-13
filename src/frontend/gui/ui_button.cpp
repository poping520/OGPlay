#include "ui_button.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace ogplay::frontend {

bool GuiButton(const char* label, const ImVec2& size) {
    struct FrameIds final {
        int frame{-1};
        std::vector<ImGuiID> ids;
    };
    static thread_local FrameIds seen;
    const auto frame = ImGui::GetFrameCount();
    if (seen.frame != frame) {
        seen.frame = frame;
        seen.ids.clear();
    }
    const auto id = ImGui::GetID(label);
    if (std::find(seen.ids.begin(), seen.ids.end(), id) != seen.ids.end()) {
        throw std::runtime_error(
            "duplicate visible ImGui button ID: " + std::string(label));
    }
    seen.ids.push_back(id);
    return ImGui::Button(label, size);
}

}  // namespace ogplay::frontend
