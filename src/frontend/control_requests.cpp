#include "ogplay/frontend/control_requests.h"

#include <stdexcept>
#include <string>

#include "ogplay/agent/control_editor.h"
#include "ogplay/agent/mcp_protocol.h"
#include "ogplay/core/encoding.h"

namespace ogplay::frontend {
namespace {

void RequireParams(const core::JsonValue params, const std::initializer_list<std::string_view> allowed) {
    if (!params.IsValid()) return;
    if (!params.IsObject()) throw std::invalid_argument("params must be an object");
    for (const auto key : params.Keys()) {
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            throw std::invalid_argument("unknown parameter " + std::string(key));
    }
}

[[nodiscard]] session::ControlLayer EffectiveLayer(const input::ControlConfig& config) {
    session::ControlLayer layer;
    layer.zones = config.zones;
    for (const auto& binding : config.bindings) layer.bindings.push_back({binding, false});
    return layer;
}

[[nodiscard]] agent::ControlResponse Get(const ControlRequestContext& context) {
    core::JsonWriter writer;
    const auto root = writer.Object();
    const auto surface = writer.Object();
    writer.AddUnsignedInteger(surface, "width", context.surface_width);
    writer.AddUnsignedInteger(surface, "height", context.surface_height);
    writer.Add(root, "surface", surface);
    writer.AddBool(root, "frozen", context.frozen);
    writer.AddString(root, "mode", context.mapper.Mode());
    if (context.config_path) writer.AddString(root, "config_path", context.config_path->generic_string());
    else writer.AddNull(root, "config_path");
    writer.AddBool(root, "persistent", context.config_path.has_value());
    writer.Add(root, "user", session::WriteControlLayerJson(writer, context.user));
    writer.Add(root, "effective", session::WriteControlLayerJson(writer, EffectiveLayer(context.mapper.Config())));
    const auto layout = writer.Array();
    for (const auto& zone : context.mapper.Layout()) {
        const auto item = writer.Object();
        writer.AddString(item, "name", zone.name);
        writer.AddReal(item, "center_x", zone.center_x);
        writer.AddReal(item, "center_y", zone.center_y);
        writer.AddReal(item, "half_width", zone.half_width);
        writer.AddReal(item, "half_height", zone.half_height);
        writer.AddBool(item, "circle", zone.circle);
        writer.Append(layout, item);
    }
    writer.Add(root, "layout", layout);
    const auto templates = writer.Array();
    for (const auto& item : session::ListControlTemplates(context.templates_directory))
        writer.Append(templates, session::WriteControlTemplateJson(writer, item));
    writer.Add(root, "templates", templates);
    return agent::ControlResult(writer.Serialize(root));
}

[[nodiscard]] agent::ControlResponse Frozen(const bool frozen) {
    return agent::ControlResult(frozen ? R"({"frozen":true})" : R"({"frozen":false})");
}

[[nodiscard]] agent::ControlResponse Frame(const ControlRequestContext& context) {
    const auto latest = context.frames != nullptr ? context.frames->Latest() : std::nullopt;
    if (!latest) return agent::ControlError(-32011, "no presented frame yet");
    const auto jpeg = agent::EncodeFrameJpeg(*latest);
    core::JsonWriter writer;
    const auto root = writer.Object();
    writer.AddUnsignedInteger(root, "width", latest->width);
    writer.AddUnsignedInteger(root, "height", latest->height);
    writer.AddUnsignedInteger(root, "sequence", latest->sequence);
    writer.AddString(root, "mime_type", "image/jpeg");
    writer.AddString(root, "data", core::EncodeBase64(std::as_bytes(std::span{jpeg})));
    return agent::ControlResult(writer.Serialize(root));
}

[[nodiscard]] agent::ControlResponse Apply(const core::JsonValue params, const ControlRequestContext& context) {
    RequireParams(params, {"layer", "save"});
    const auto layer_json = params.IsValid() ? params.Member("layer") : std::nullopt;
    if (!layer_json) throw std::invalid_argument("layer is required");
    bool save = true;
    if (const auto flag = params.Member("save")) {
        if (!flag->Bool()) throw std::invalid_argument("save must be a boolean");
        save = *flag->Bool();
    }
    auto layer = session::DecodeControlLayerJson(*layer_json);
    auto config = session::BuildControlConfig(context.profile_input, &layer, context.templates_directory);
    context.deliver(context.mapper.Reconfigure(std::move(config)));
    context.user = layer;
    const bool saved = save && context.config_path.has_value();
    if (saved) session::SaveControlLayer(*context.config_path, layer);
    core::JsonWriter writer;
    const auto root = writer.Object();
    writer.AddBool(root, "applied", true);
    writer.AddBool(root, "saved", saved);
    if (context.config_path) writer.AddString(root, "config_path", context.config_path->generic_string());
    else writer.AddNull(root, "config_path");
    return agent::ControlResult(writer.Serialize(root));
}

}  // namespace

agent::ControlResponse HandleControlRequest(const std::string_view method, const core::JsonValue params,
                                            const ControlRequestContext& context) {
    try {
        if (method != "control.status" && context.seen) context.seen(false);
        if (method == "control.get") {
            RequireParams(params, {});
            return Get(context);
        }
        if (method == "control.freeze") {
            RequireParams(params, {});
            if (!context.frozen) {
                context.frozen = true;
                context.freeze();
            }
            return Frozen(true);
        }
        if (method == "control.resume") {
            RequireParams(params, {});
            context.frozen = false;
            return Frozen(false);
        }
        if (method == "control.frame") {
            RequireParams(params, {});
            return Frame(context);
        }
        if (method == "control.status") {
            RequireParams(params, {"client"});
            bool launcher = false;
            if (const auto client = params.IsValid() ? params.Member("client") : std::nullopt) {
                const auto name = client->String();
                if (!name || (*name != "launcher" && *name != "editor"))
                    throw std::invalid_argument("client must be launcher or editor");
                launcher = *name == "launcher";
            }
            if (context.seen) context.seen(launcher);
            core::JsonWriter writer;
            const auto root = writer.Object();
            writer.AddBool(root, "frozen", context.frozen);
            writer.AddUnsignedInteger(root, "edit_requests", context.edit_requests);
            return agent::ControlResult(writer.Serialize(root));
        }
        if (method == "control.apply") return Apply(params, context);
        return agent::ControlError(-32601, "unknown key editor method");
    } catch (const input::ControlConfigError& error) {
        return agent::ControlError(-32010, error.what());
    } catch (const std::invalid_argument& error) {
        return agent::ControlError(-32602, error.what());
    }
}

}  // namespace ogplay::frontend
