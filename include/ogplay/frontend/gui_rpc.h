#pragma once

#include <functional>
#include "ogplay/agent/json_rpc.h"
#include "ogplay/frontend/gui_launch.h"
#include "ogplay/frontend/gui_view_model.h"

namespace ogplay::frontend {

// Host operations are explicit callbacks; the RPC model has no SDL/WebView dependency.
struct GuiRpcHost final {
    std::function<LibraryViewContext(const std::vector<LibraryEntry>&)> context;
    std::function<void(const LaunchPlan&)> launch;
    std::function<void(const std::filesystem::path&)> open_directory;
};

class GuiRpcService final {
public:
    GuiRpcService(LibraryStore& store, std::filesystem::path cli, GuiRpcHost host);
    GuiRpcService(const GuiRpcService&) = delete;
    GuiRpcService& operator=(const GuiRpcService&) = delete;
    GuiRpcService(GuiRpcService&&) = delete;
    GuiRpcService& operator=(GuiRpcService&&) = delete;
    [[nodiscard]] std::string Handle(std::string_view request);
private:
    [[nodiscard]] agent::ControlResponse Request(std::string_view method, core::JsonValue params);
    LibraryStore& store_;
    std::filesystem::path cli_;
    GuiRpcHost host_;
    agent::JsonRpcAdapter adapter_;
};

}  // namespace ogplay::frontend
