#pragma once

#include <filesystem>

namespace ogplay::hal::windows {
// Same opaque result as webview_create, with an explicit WebView2 data root.
void* CreateWebViewWithDataDirectory(void* window, const std::filesystem::path& directory);
}
