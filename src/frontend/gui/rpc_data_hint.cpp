#include "frontend/gui/rpc.h"
#include "ogplay/app/data_hint.h"
#include "ogplay/app/library.h"

#include <stdexcept>

namespace ogplay::frontend {
using namespace ogplay::app;
namespace {
std::string Text(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
std::filesystem::path Path(std::string_view text) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
std::string RequiredText(app::JsonValue params, std::string_view key) {
    const auto value = params.Member(key);
    const auto text = value ? value->String() : std::nullopt;
    if (!text || text->empty() || text->find('\0') != std::string_view::npos)
        throw std::invalid_argument(std::string(key) + " requires a nonempty string");
    return std::string(*text);
}
app::JsonWriter::Value Strings(app::JsonWriter& writer, const std::vector<std::string>& values) {
    const auto array = writer.Array();
    for (const auto& value : values) writer.Append(array, writer.String(value));
    return array;
}
}  // namespace

// ADR-0146: after a game exits, compare what it failed to find with the data pack.
app::ConsoleResponse GuiRpcService::DataHintRequest(std::string_view method, app::JsonValue params) {
    const bool apply = method == "library.data_mount.apply";
    std::size_t known = params.Member("installation_id") ? 1U : 0U;
    if (params.Member("external_dir")) ++known;
    if (apply && params.Member("external_guest_dir")) ++known;
    if (known != params.Size()) throw std::invalid_argument("unknown parameter");
    const auto id = RequiredText(params, "installation_id");
    std::optional<std::filesystem::path> picked;
    if (params.Member("external_dir")) picked = Path(RequiredText(params, "external_dir"));
    if (ImportBusy()) throw std::runtime_error("正在入库，请稍后重试。");
    app::JsonWriter writer;
    const auto root = writer.Object(), result = writer.Object();
    if (apply) {
        const auto guest = RequiredText(params, "external_guest_dir");
        ValidateExternalGuestDirectory(guest);
        GameSettings changes;
        changes.values["external_guest_dir"] = guest;
        if (picked) changes.values["external_dir"] = Text(*picked);
        static_cast<void>(PatchGameSettings(store_, id, changes, {}));
        writer.AddBool(result, "applied", true);
        writer.Add(root, "result", result);
        return {true, writer.Serialize(root)};
    }
    const auto suggestion = SuggestDataMount(store_, id, picked);
    const auto& hint = suggestion.hint;
    if (hint.kind == DataMountHintKind::none) {
        writer.AddString(result, "kind", "none");
        writer.Add(root, "result", result);
        return {true, writer.Serialize(root)};
    }
    writer.AddString(result, "kind", hint.kind == DataMountHintKind::remount ? "remount" : "needs_data");
    writer.AddString(result, "current_guest_dir", suggestion.current_guest_dir);
    if (suggestion.external_dir) writer.AddString(result, "external_dir", Text(*suggestion.external_dir));
    else writer.AddNull(result, "external_dir");
    writer.AddBool(result, "owned", suggestion.owned);
    writer.AddUnsignedInteger(result, "missing", hint.missing);
    writer.AddBool(result, "truncated", suggestion.truncated);
    const auto candidates = writer.Array();
    for (const auto& candidate : hint.candidates) {
        const auto item = writer.Object();
        writer.AddString(item, "guest_dir", candidate.guest_dir);
        writer.AddUnsignedInteger(item, "explained", candidate.explained);
        writer.Add(item, "examples", Strings(writer, candidate.examples));
        writer.Append(candidates, item);
    }
    writer.Add(result, "candidates", candidates);
    writer.AddString(result, "search_root", hint.search_root);
    writer.Add(result, "examples", Strings(writer, hint.examples));
    writer.Add(root, "result", result);
    return {true, writer.Serialize(root)};
}

}  // namespace ogplay::frontend
