// Compile upstream implementation separately from project code/warnings.
#include <webview/webview.h>
#include "webview_backend.h"

void* ogplay::hal::windows::CreateWebViewWithDataDirectory(void* window, const std::filesystem::path& directory) {
    webview::webview* view{};
    const auto error = webview::detail::api_filter(
        [&]() -> webview::result<webview::webview*> {
            return new webview::webview{false, window, directory.native()};
        },
        [&](webview::webview* created) { view = created; });
    return error == WEBVIEW_ERROR_OK ? view : nullptr;
}
