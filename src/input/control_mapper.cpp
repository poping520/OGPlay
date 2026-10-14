#include "ogplay/input/control_mapper.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

namespace ogplay::input {
namespace {

constexpr std::int32_t kEscapeScancode = 41;
constexpr std::int32_t kAndroidKeyBack = 4;
constexpr std::int32_t kMaxAndroidKeyCode = 255;

struct NamedCode final {
    std::string_view name;
    std::int32_t code;
};

// USB HID usage ids of the keyboard page; SDL scancodes share these values.
constexpr std::array kKeyNames{
    NamedCode{"enter", 40}, NamedCode{"escape", 41}, NamedCode{"backspace", 42},
    NamedCode{"tab", 43}, NamedCode{"space", 44}, NamedCode{"minus", 45},
    NamedCode{"equals", 46}, NamedCode{"left_bracket", 47}, NamedCode{"right_bracket", 48},
    NamedCode{"backslash", 49}, NamedCode{"semicolon", 51}, NamedCode{"apostrophe", 52},
    NamedCode{"grave", 53}, NamedCode{"comma", 54}, NamedCode{"period", 55},
    NamedCode{"slash", 56}, NamedCode{"caps_lock", 57}, NamedCode{"insert", 73},
    NamedCode{"home", 74}, NamedCode{"page_up", 75}, NamedCode{"delete", 76},
    NamedCode{"end", 77}, NamedCode{"page_down", 78}, NamedCode{"right", 79},
    NamedCode{"left", 80}, NamedCode{"down", 81}, NamedCode{"up", 82},
    NamedCode{"left_ctrl", 224}, NamedCode{"left_shift", 225}, NamedCode{"left_alt", 226},
    NamedCode{"right_ctrl", 228}, NamedCode{"right_shift", 229}, NamedCode{"right_alt", 230},
};

constexpr std::array kMouseNames{
    NamedCode{"left", static_cast<std::int32_t>(hal::PointerButton::primary)},
    NamedCode{"middle", static_cast<std::int32_t>(hal::PointerButton::middle)},
    NamedCode{"right", static_cast<std::int32_t>(hal::PointerButton::secondary)},
    NamedCode{"x1", static_cast<std::int32_t>(hal::PointerButton::auxiliary_1)},
    NamedCode{"x2", static_cast<std::int32_t>(hal::PointerButton::auxiliary_2)},
};

[[nodiscard]] std::optional<std::int32_t> KeyScancode(const std::string_view name) {
    if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z') return 4 + (name[0] - 'a');
    if (name.size() == 1 && name[0] >= '1' && name[0] <= '9') return 30 + (name[0] - '1');
    if (name == "0") return 39;
    if (name.size() >= 2 && name.size() <= 3 && name[0] == 'f') {
        int number = 0;
        for (const char c : name.substr(1)) {
            if (c < '0' || c > '9') return std::nullopt;
            number = number * 10 + (c - '0');
        }
        if (number >= 1 && number <= 12) return 58 + number - 1;
        return std::nullopt;
    }
    for (const auto& entry : kKeyNames) if (entry.name == name) return entry.code;
    return std::nullopt;
}

[[nodiscard]] std::pair<float, float> AnchorFractions(const Anchor anchor) {
    switch (anchor) {
    case Anchor::top_left: return {0.0F, 0.0F};
    case Anchor::top: return {0.5F, 0.0F};
    case Anchor::top_right: return {1.0F, 0.0F};
    case Anchor::left: return {0.0F, 0.5F};
    case Anchor::center: return {0.5F, 0.5F};
    case Anchor::right: return {1.0F, 0.5F};
    case Anchor::bottom_left: return {0.0F, 1.0F};
    case Anchor::bottom: return {0.5F, 1.0F};
    case Anchor::bottom_right: return {1.0F, 1.0F};
    }
    throw ControlConfigError("unknown zone anchor");
}

[[nodiscard]] float Clamp(const float value, const std::uint32_t extent) {
    return std::clamp(value, 0.0F, std::nextafter(static_cast<float>(extent), 0.0F));
}

[[nodiscard]] bool Finite(const float value) { return std::isfinite(value); }

struct OwnerKey final {
    enum class Kind : std::uint8_t { mouse, touch, external, binding };
    Kind kind{};
    std::uint64_t a{}, b{};
    bool operator==(const OwnerKey&) const = default;
};

struct Contact final {
    OwnerKey owner;
    TouchPoint point;
};

struct Request final {
    OwnerKey owner;
    float x{}, y{}, pressure{1.0F};
};

// Direct contacts (mouse, host touch, external): latched keeps a press that was released
// before the next Flush() visible for exactly one frame.
struct DirectState final {
    bool held{}, latched{};
    float x{}, y{}, pressure{1.0F};
};

struct BindingRuntime final {
    std::vector<bool> held, latched;
    bool tap_pending{};
    std::uint64_t start_frame{};
    bool key_down{};
    std::uint64_t key_down_frame{};
    bool stick_active{};
    std::uint64_t stick_down_frame{};
    float offset_x{}, offset_y{};
};

struct ResolvedZone final {
    float center_x{}, center_y{}, half_width{}, half_height{};
};

struct QueuedOutput final {
    std::uint64_t not_before{};
    ControlOutput output;
};

struct Overlay final {
    std::string mode;
    std::size_t binding{};
};

void ValidateConfig(const ControlConfig& config) {
    std::vector<std::string_view> names;
    for (const auto& zone : config.zones) {
        if (zone.name.empty()) throw ControlConfigError("zone name must not be empty");
        if (std::ranges::find(names, zone.name) != names.end())
            throw ControlConfigError("duplicate zone: " + zone.name);
        names.push_back(zone.name);
        if (!Finite(zone.x) || !Finite(zone.y) || !Finite(zone.width) || !Finite(zone.height) ||
            !Finite(zone.radius) || zone.radius < 0 ||
            (zone.radius == 0 && (zone.width <= 0 || zone.height <= 0)))
            throw ControlConfigError("zone needs a positive radius or size: " + zone.name);
    }
    const auto has_zone = [&](const std::string& name) {
        return std::ranges::find(names, name) != names.end();
    };
    struct Claim final { std::string_view mode; InputSource source; };
    std::vector<Claim> claims;
    for (const auto& binding : config.bindings) {
        if (binding.mode.empty()) throw ControlConfigError("binding mode must not be empty");
        const auto expected = binding.action == ActionKind::stick ? 4U : 1U;
        if (binding.sources.size() != expected)
            throw ControlConfigError(binding.action == ActionKind::stick
                ? "stick binding needs up, left, down and right sources"
                : "binding needs exactly one source");
        switch (binding.action) {
        case ActionKind::button:
            if (!has_zone(binding.zone)) throw ControlConfigError("unknown zone: " + binding.zone);
            if ((binding.press == PressMode::repeat) != (binding.repeat_frames != 0) ||
                (binding.press == PressMode::repeat && binding.repeat_frames < 2))
                throw ControlConfigError("repeat buttons need repeat_frames of at least 2");
            break;
        case ActionKind::stick:
            if (!has_zone(binding.zone)) throw ControlConfigError("unknown zone: " + binding.zone);
            if (!Finite(binding.stick_radius) || binding.stick_radius <= 0 ||
                binding.stick_radius > 1)
                throw ControlConfigError("stick_radius must be in (0, 1]");
            if (binding.stick_settle_frames > 60 || binding.stick_ramp_frames < 1 ||
                binding.stick_ramp_frames > 60)
                throw ControlConfigError("stick settle frames must be 0..60 and ramp frames 1..60");
            break;
        case ActionKind::key:
            if (binding.android_key_code < 1 || binding.android_key_code > kMaxAndroidKeyCode)
                throw ControlConfigError("android key code out of range");
            break;
        case ActionKind::mode:
            if (binding.target_mode.empty() || binding.target_mode == kEveryMode)
                throw ControlConfigError("mode binding needs a concrete target mode");
            break;
        }
        for (const auto& source : binding.sources) {
            for (const auto& claim : claims) {
                if (claim.source != source) continue;
                if (claim.mode == binding.mode || claim.mode == kEveryMode ||
                    binding.mode == kEveryMode)
                    throw ControlConfigError("input source bound twice in mode " + binding.mode);
            }
            claims.push_back({binding.mode, source});
        }
    }
}

}  // namespace

ControlConfig DefaultControlConfig() {
    ControlConfig config;
    Binding back;
    back.mode = std::string{kEveryMode};
    back.action = ActionKind::key;
    back.sources = {{SourceKind::key, kEscapeScancode}};
    back.android_key_code = kAndroidKeyBack;
    config.bindings.push_back(std::move(back));
    return config;
}

InputSource ParseInputSource(const std::string_view text) {
    const auto colon = text.find(':');
    if (colon != std::string_view::npos) {
        const auto kind = text.substr(0, colon);
        const auto name = text.substr(colon + 1);
        if (kind == "key") {
            if (const auto code = KeyScancode(name)) return {SourceKind::key, *code};
        } else if (kind == "mouse") {
            for (const auto& entry : kMouseNames)
                if (entry.name == name) return {SourceKind::mouse_button, entry.code};
        }
    }
    throw ControlConfigError("unknown input source: " + std::string{text});
}

std::string FormatInputSource(const InputSource& source) {
    if (source.kind == SourceKind::mouse_button) {
        for (const auto& entry : kMouseNames)
            if (entry.code == source.code) return "mouse:" + std::string(entry.name);
        throw ControlConfigError("unknown mouse button " + std::to_string(source.code));
    }
    const auto code = source.code;
    if (code >= 4 && code <= 29) return "key:" + std::string(1, static_cast<char>('a' + code - 4));
    if (code >= 30 && code <= 38) return "key:" + std::string(1, static_cast<char>('1' + code - 30));
    if (code == 39) return "key:0";
    if (code >= 58 && code <= 69) return "key:f" + std::to_string(code - 57);
    for (const auto& entry : kKeyNames) if (entry.code == code) return "key:" + std::string(entry.name);
    throw ControlConfigError("key scancode has no name: " + std::to_string(code));
}

namespace {
constexpr std::array kAnchorNames{
        std::pair{"top_left", Anchor::top_left}, std::pair{"top", Anchor::top},
        std::pair{"top_right", Anchor::top_right}, std::pair{"left", Anchor::left},
        std::pair{"center", Anchor::center}, std::pair{"right", Anchor::right},
        std::pair{"bottom_left", Anchor::bottom_left}, std::pair{"bottom", Anchor::bottom},
        std::pair{"bottom_right", Anchor::bottom_right},
};
}  // namespace

Anchor ParseAnchor(const std::string_view text) {
    for (const auto& [name, anchor] : kAnchorNames) if (text == name) return anchor;
    throw ControlConfigError("unknown zone anchor: " + std::string{text});
}

std::string_view AnchorName(const Anchor anchor) {
    for (const auto& [name, value] : kAnchorNames) if (value == anchor) return name;
    throw ControlConfigError("unknown zone anchor");
}

struct ControlMapper::State final {
    ControlConfig config;
    std::vector<std::size_t> binding_zone;  // zone index per binding (button/stick)
    std::vector<ResolvedZone> zones;
    std::uint32_t width{}, height{};
    std::vector<BindingRuntime> runtime;
    std::string base_mode{kDefaultMode};
    std::vector<Overlay> overlays;
    DirectState mouse;
    std::uint32_t mouse_window{}, mouse_device{};
    std::vector<std::pair<std::pair<std::uint64_t, std::uint64_t>, DirectState>> touches;
    std::vector<std::pair<std::uint64_t, DirectState>> externals;
    std::vector<hal::InputEvent> passthrough_down;
    std::vector<QueuedOutput> queued;
    std::vector<Contact> contacts;
    std::vector<OwnerKey> rejected_owners;
    bool pending_cancel{};
    std::uint64_t frame{};
    std::uint32_t modifiers{};
    std::uint64_t rejected{};

    [[nodiscard]] bool SurfaceReady() const { return width != 0 && height != 0; }

    [[nodiscard]] bool IsActive(const Binding& binding) const {
        if (binding.mode == kEveryMode || binding.mode == base_mode) return true;
        return std::ranges::any_of(overlays, [&](const auto& o) { return o.mode == binding.mode; });
    }

    [[nodiscard]] std::optional<std::size_t> Route(const InputSource& source) const {
        std::optional<std::size_t> base, every;
        for (auto overlay = overlays.rbegin(); overlay != overlays.rend(); ++overlay) {
            for (std::size_t i = 0; i < config.bindings.size(); ++i) {
                const auto& b = config.bindings[i];
                if (b.mode == overlay->mode && std::ranges::find(b.sources, source) != b.sources.end())
                    return i;
            }
        }
        for (std::size_t i = 0; i < config.bindings.size(); ++i) {
            const auto& b = config.bindings[i];
            if (std::ranges::find(b.sources, source) == b.sources.end()) continue;
            if (b.mode == base_mode) base = i;
            else if (b.mode == kEveryMode) every = i;
        }
        return base ? base : every;
    }

    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> Engaged(
        const InputSource& source) const {
        for (std::size_t i = 0; i < config.bindings.size(); ++i) {
            const auto& sources = config.bindings[i].sources;
            for (std::size_t s = 0; s < sources.size(); ++s)
                if (sources[s] == source && runtime[i].held[s]) return std::pair{i, s};
        }
        return std::nullopt;
    }

    void Queue(ControlOutput output, const std::uint64_t not_before) {
        queued.push_back({not_before, std::move(output)});
    }

    void QueueAndroidKey(BindingRuntime& rt, const Binding& binding, const bool pressed,
                         const std::uint32_t device) {
        ControlOutput output;
        output.kind = ControlOutput::Kind::android_key;
        output.android_key = {binding.android_key_code, pressed, modifiers, device};
        if (pressed) {
            rt.key_down = true;
            rt.key_down_frame = frame;
            Queue(std::move(output), frame);
        } else if (rt.key_down) {
            rt.key_down = false;
            Queue(std::move(output), std::max(frame, rt.key_down_frame + 1));
        }
    }

    void Deactivate(const std::size_t index) {
        auto& rt = runtime[index];
        std::fill(rt.held.begin(), rt.held.end(), false);
        std::fill(rt.latched.begin(), rt.latched.end(), false);
        rt.tap_pending = false;
        rt.stick_active = false;
        rt.offset_x = rt.offset_y = 0;
        if (config.bindings[index].action == ActionKind::key)
            QueueAndroidKey(rt, config.bindings[index], false, 0);
        std::erase_if(overlays, [index](const auto& o) { return o.binding == index; });
    }

    void ApplyModeChange() {
        // Deactivating a hold-mode binding can remove further overlays; repeat until stable.
        for (bool changed = true; changed;) {
            changed = false;
            for (std::size_t i = 0; i < config.bindings.size(); ++i) {
                const auto& rt = runtime[i];
                const bool engaged = std::find(rt.held.begin(), rt.held.end(), true) != rt.held.end() ||
                                     std::find(rt.latched.begin(), rt.latched.end(), true) != rt.latched.end() ||
                                     rt.tap_pending || rt.key_down;
                if (engaged && !IsActive(config.bindings[i])) {
                    const auto before = overlays.size();
                    Deactivate(i);
                    changed = changed || overlays.size() != before;
                }
            }
        }
    }

    void Press(const std::size_t index, const std::size_t source, const std::uint32_t device) {
        const auto& binding = config.bindings[index];
        auto& rt = runtime[index];
        rt.held[source] = true;
        rt.latched[source] = true;
        switch (binding.action) {
        case ActionKind::button:
            if (binding.press == PressMode::tap) rt.tap_pending = true;
            if (binding.press == PressMode::repeat) rt.start_frame = frame;
            break;
        case ActionKind::stick: break;
        case ActionKind::key: QueueAndroidKey(rt, binding, true, device); break;
        case ActionKind::mode:
            if (binding.mode_switch == ModeSwitch::toggle) {
                base_mode = base_mode == binding.target_mode ? std::string{kDefaultMode}
                                                             : binding.target_mode;
            } else {
                overlays.push_back({binding.target_mode, index});
            }
            ApplyModeChange();
            break;
        }
    }

    void Release(const std::size_t index, const std::size_t source, const std::uint32_t device) {
        const auto& binding = config.bindings[index];
        auto& rt = runtime[index];
        rt.held[source] = false;
        if (binding.action == ActionKind::key) QueueAndroidKey(rt, binding, false, device);
        if (binding.action == ActionKind::mode && binding.mode_switch == ModeSwitch::hold) {
            std::erase_if(overlays, [index](const auto& o) { return o.binding == index; });
            ApplyModeChange();
        }
    }

    // Returns false when no binding claims the source.
    bool RouteSource(const InputSource& source, const bool pressed, const std::uint32_t device) {
        if (!pressed) {
            const auto engaged = Engaged(source);
            if (!engaged) return Route(source).has_value();
            Release(engaged->first, engaged->second, device);
            return true;
        }
        const auto index = Route(source);
        if (!index) return false;
        const auto& sources = config.bindings[*index].sources;
        const auto slot = static_cast<std::size_t>(
            std::ranges::find(sources, source) - sources.begin());
        if (!runtime[*index].held[slot]) Press(*index, slot, device);
        return true;
    }

    void AcceptKey(const hal::InputEvent& event) {
        const auto same = [&](const hal::InputEvent& e) {
            return e.code == event.code && e.device_id == event.device_id;
        };
        const auto down = std::ranges::find_if(passthrough_down, same);
        if (event.repeat) {
            if (down != passthrough_down.end()) Passthrough(event);
            return;
        }
        if (!event.pressed && down != passthrough_down.end()) {
            passthrough_down.erase(down);
            Passthrough(event);
            return;
        }
        if (RouteSource({SourceKind::key, event.code}, event.pressed, event.device_id)) return;
        if (event.pressed) {
            passthrough_down.push_back(event);
            Passthrough(event);
        }
    }

    void Passthrough(const hal::InputEvent& event) {
        ControlOutput output;
        output.kind = ControlOutput::Kind::passthrough_key;
        output.passthrough = event;
        Queue(std::move(output), frame);
    }

    void AcceptMouse(const hal::InputEvent& event, const hal::WindowState& window) {
        if (event.type == hal::InputEventType::pointer_button &&
            RouteSource({SourceKind::mouse_button, event.code}, event.pressed, event.device_id))
            return;
        if (!SurfaceReady()) return;
        const bool primary = event.code == static_cast<std::int32_t>(hal::PointerButton::primary);
        const auto mapped = hal::MapDisplayPoint(event.x, event.y, width, height,
                                                 window.width, window.height);
        if (event.type == hal::InputEventType::pointer_button) {
            if (!primary) return;
            if (event.pressed) {
                if (mouse.held || !mapped.inside) return;
                mouse = {true, true, Clamp(mapped.x, width), Clamp(mapped.y, height), 1.0F};
                mouse_window = event.window_id;
                mouse_device = event.device_id;
            } else if (mouse.held && event.window_id == mouse_window &&
                       event.device_id == mouse_device) {
                mouse.held = false;
            }
            return;
        }
        if (mouse.held && mapped.inside && event.window_id == mouse_window &&
            event.device_id == mouse_device) {
            mouse.x = Clamp(mapped.x, width);
            mouse.y = Clamp(mapped.y, height);
        }
    }

    void AcceptTouch(const hal::InputEvent& event, const hal::WindowState& window) {
        if (event.type == hal::InputEventType::touch_cancel) {
            Cancel();
            return;
        }
        if (!SurfaceReady()) return;
        if (!Finite(event.pressure) || event.pressure < 0 || event.pressure > 1)
            throw std::invalid_argument("invalid touch pressure");
        const auto key = std::pair{event.touch_device_id, event.contact_id};
        auto found = std::ranges::find_if(touches, [&](const auto& t) { return t.first == key; });
        const auto mapped = hal::MapDisplayPoint(event.x, event.y, width, height,
                                                 window.width, window.height);
        if (event.type == hal::InputEventType::touch_down) {
            if (!mapped.inside || (found != touches.end() && found->second.held)) return;
            if (found == touches.end()) {
                touches.push_back({key, {}});
                found = touches.end() - 1;
            }
            found->second.held = true;
            found->second.latched = true;
        } else if (found == touches.end() || !found->second.held) {
            return;
        }
        found->second.x = Clamp(mapped.x, width);
        found->second.y = Clamp(mapped.y, height);
        found->second.pressure = event.pressure;
        if (event.type == hal::InputEventType::touch_up) found->second.held = false;
    }

    void Cancel() {
        pending_cancel = pending_cancel || !contacts.empty();
        mouse = {};
        touches.clear();
        externals.clear();
        for (std::size_t i = 0; i < runtime.size(); ++i) Deactivate(i);
        overlays.clear();
        for (auto event : passthrough_down) {
            event.pressed = false;
            event.repeat = false;
            Passthrough(event);
        }
        passthrough_down.clear();
    }

    [[nodiscard]] const Contact* FindContact(const OwnerKey& owner) const {
        const auto found = std::ranges::find_if(contacts, [&](const auto& c) { return c.owner == owner; });
        return found == contacts.end() ? nullptr : &*found;
    }

    void RequestBinding(const std::size_t index, std::vector<Request>& requests) const {
        const auto& binding = config.bindings[index];
        const auto& rt = runtime[index];
        if (binding.action != ActionKind::button && binding.action != ActionKind::stick) return;
        if (!SurfaceReady()) return;
        const auto& zone = zones[binding_zone[index]];
        const OwnerKey owner{OwnerKey::Kind::binding, index, 0};
        const auto on = [&](const std::size_t s) { return rt.held[s] || rt.latched[s]; };
        if (binding.action == ActionKind::button) {
            bool wanted = false;
            switch (binding.press) {
            case PressMode::tap: wanted = rt.tap_pending; break;
            case PressMode::hold: wanted = on(0); break;
            case PressMode::repeat:
                wanted = rt.latched[0] ||
                         (rt.held[0] && (frame - rt.start_frame) % binding.repeat_frames == 0);
                break;
            }
            if (wanted) requests.push_back({owner, zone.center_x, zone.center_y, 1.0F});
            return;
        }
        if (!rt.stick_active) return;
        requests.push_back({owner, Clamp(zone.center_x + rt.offset_x, width),
                            Clamp(zone.center_y + rt.offset_y, height), 1.0F});
    }

    // Advances each stick one frame: press at the origin, rest for settle frames, then move
    // toward the held direction by at most travel / ramp per frame.
    void UpdateSticks() {
        for (std::size_t index = 0; index < config.bindings.size(); ++index) {
            const auto& binding = config.bindings[index];
            if (binding.action != ActionKind::stick) continue;
            auto& rt = runtime[index];
            const auto on = [&](const std::size_t s) { return rt.held[s] || rt.latched[s]; };
            if (!SurfaceReady() || !(on(0) || on(1) || on(2) || on(3))) {
                rt.stick_active = false;
                rt.offset_x = rt.offset_y = 0;
                continue;
            }
            if (!rt.stick_active) {
                rt.stick_active = true;
                rt.stick_down_frame = frame;
                rt.offset_x = rt.offset_y = 0;
                continue;
            }
            if (frame - rt.stick_down_frame <= binding.stick_settle_frames) continue;
            const auto& zone = zones[binding_zone[index]];
            const float travel = std::min(zone.half_width, zone.half_height) * binding.stick_radius;
            const float dx = static_cast<float>(on(3)) - static_cast<float>(on(1));
            const float dy = static_cast<float>(on(2)) - static_cast<float>(on(0));
            const float length = std::hypot(dx, dy);
            const float target_x = length == 0 ? 0 : dx / length * travel;
            const float target_y = length == 0 ? 0 : dy / length * travel;
            const float rest_x = target_x - rt.offset_x;
            const float rest_y = target_y - rt.offset_y;
            const float distance = std::hypot(rest_x, rest_y);
            const float step = travel / static_cast<float>(binding.stick_ramp_frames);
            if (distance <= step) {
                rt.offset_x = target_x;
                rt.offset_y = target_y;
            } else {
                rt.offset_x += rest_x / distance * step;
                rt.offset_y += rest_y / distance * step;
            }
        }
    }

    [[nodiscard]] std::vector<Request> Requests() const {
        std::vector<Request> requests;
        if (mouse.held || mouse.latched)
            requests.push_back({{OwnerKey::Kind::mouse, 0, 0}, mouse.x, mouse.y, mouse.pressure});
        for (const auto& [key, touch] : touches) {
            if (touch.held || touch.latched)
                requests.push_back({{OwnerKey::Kind::touch, key.first, key.second},
                                    touch.x, touch.y, touch.pressure});
        }
        for (const auto& [id, external] : externals) {
            if (external.held || external.latched)
                requests.push_back({{OwnerKey::Kind::external, id, 0}, external.x, external.y, 1.0F});
        }
        for (std::size_t i = 0; i < config.bindings.size(); ++i) RequestBinding(i, requests);
        return requests;
    }

    [[nodiscard]] TouchSnapshot Snapshot(const TouchPhase phase, const std::size_t changed) const {
        TouchSnapshot snapshot{phase, changed, {}, modifiers};
        for (const auto& contact : contacts) snapshot.points.push_back(contact.point);
        return snapshot;
    }

    void EmitTouch(std::vector<ControlOutput>& outputs, TouchSnapshot snapshot) {
        ControlOutput output;
        output.kind = ControlOutput::Kind::touch;
        output.touch = std::move(snapshot);
        outputs.push_back(std::move(output));
    }

    [[nodiscard]] std::vector<ControlOutput> Flush() {
        std::vector<ControlOutput> outputs;
        if (pending_cancel) {
            if (!contacts.empty()) EmitTouch(outputs, Snapshot(TouchPhase::cancel, 0));
            contacts.clear();
            pending_cancel = false;
        }
        std::vector<QueuedOutput> later;
        for (auto& entry : queued) {
            if (entry.not_before <= frame) outputs.push_back(std::move(entry.output));
            else later.push_back(std::move(entry));
        }
        queued = std::move(later);

        UpdateSticks();
        const auto requests = Requests();
        const auto requested = [&](const OwnerKey& owner) {
            return std::ranges::find_if(requests, [&](const auto& r) { return r.owner == owner; });
        };
        bool moved = false;
        for (auto& contact : contacts) {
            const auto request = requested(contact.owner);
            if (request == requests.end()) continue;
            if (contact.point.x != request->x || contact.point.y != request->y ||
                contact.point.pressure != request->pressure) {
                contact.point.x = request->x;
                contact.point.y = request->y;
                contact.point.pressure = request->pressure;
                moved = true;
            }
        }
        if (moved) EmitTouch(outputs, Snapshot(TouchPhase::move, 0));
        for (std::size_t i = 0; i < contacts.size();) {
            if (requested(contacts[i].owner) != requests.end()) {
                ++i;
                continue;
            }
            EmitTouch(outputs, Snapshot(TouchPhase::up, i));
            contacts.erase(contacts.begin() + static_cast<std::ptrdiff_t>(i));
        }
        std::erase_if(rejected_owners, [&](const auto& owner) { return requested(owner) == requests.end(); });
        for (const auto& request : requests) {
            if (FindContact(request.owner) != nullptr) continue;
            if (contacts.size() >= kMaxContacts) {
                if (std::ranges::find(rejected_owners, request.owner) == rejected_owners.end()) {
                    rejected_owners.push_back(request.owner);
                    ++rejected;
                }
                continue;
            }
            std::int32_t id = 0;
            while (std::ranges::any_of(contacts, [id](const auto& c) { return c.point.id == id; })) ++id;
            contacts.push_back({request.owner, {id, request.x, request.y, request.pressure}});
            EmitTouch(outputs, Snapshot(TouchPhase::down, contacts.size() - 1));
        }

        mouse.latched = false;
        if (!mouse.held) mouse = {};
        for (auto& entry : touches) entry.second.latched = false;
        std::erase_if(touches, [](const auto& t) { return !t.second.held; });
        for (auto& entry : externals) entry.second.latched = false;
        std::erase_if(externals, [](const auto& e) { return !e.second.held; });
        for (auto& rt : runtime) {
            std::fill(rt.latched.begin(), rt.latched.end(), false);
            rt.tap_pending = false;
        }
        ++frame;
        return outputs;
    }
};

ControlMapper::ControlMapper(ControlConfig config) : state_(std::make_unique<State>()) {
    ValidateConfig(config);
    state_->binding_zone.resize(config.bindings.size());
    for (std::size_t i = 0; i < config.bindings.size(); ++i) {
        const auto& binding = config.bindings[i];
        state_->runtime.push_back({std::vector<bool>(binding.sources.size()),
                                   std::vector<bool>(binding.sources.size())});
        if (binding.action == ActionKind::button || binding.action == ActionKind::stick) {
            state_->binding_zone[i] = static_cast<std::size_t>(std::ranges::find_if(
                config.zones, [&](const auto& z) { return z.name == binding.zone; }) - config.zones.begin());
        }
    }
    state_->config = std::move(config);
}

ControlMapper::~ControlMapper() = default;

void ControlMapper::SetSurface(const std::uint32_t width, const std::uint32_t height) {
    if (width == 0 || height == 0) throw std::invalid_argument("guest surface must not be empty");
    auto& state = *state_;
    if (width == state.width && height == state.height) return;
    std::vector<ResolvedZone> zones;
    for (const auto& zone : state.config.zones) {
        const float box_width = zone.radius > 0 ? zone.radius * 2 : zone.width;
        const float box_height = zone.radius > 0 ? zone.radius * 2 : zone.height;
        const auto [fx, fy] = AnchorFractions(zone.anchor);
        const float left = fx * static_cast<float>(width) + zone.x - fx * box_width;
        const float top = fy * static_cast<float>(height) + zone.y - fy * box_height;
        if (left < 0 || top < 0 || left + box_width > static_cast<float>(width) ||
            top + box_height > static_cast<float>(height))
            throw ControlConfigError("zone " + zone.name + " exceeds the " + std::to_string(width) +
                                     "x" + std::to_string(height) + " guest surface");
        zones.push_back({left + box_width / 2, top + box_height / 2, box_width / 2, box_height / 2});
    }
    if (state.SurfaceReady()) state.Cancel();
    state.zones = std::move(zones);
    state.width = width;
    state.height = height;
}

void ControlMapper::Accept(const hal::InputEvent& event, const hal::WindowState& window) {
    using Type = hal::InputEventType;
    auto& state = *state_;
    switch (event.type) {
    case Type::input_reset:
    case Type::app_background: state.Cancel(); return;
    case Type::key:
        state.modifiers = event.key_modifiers;
        state.AcceptKey(event);
        return;
    case Type::pointer_button:
    case Type::pointer_motion:
        state.modifiers = event.key_modifiers;
        state.AcceptMouse(event, window);
        return;
    case Type::touch_down:
    case Type::touch_motion:
    case Type::touch_up:
    case Type::touch_cancel:
        state.modifiers = event.key_modifiers;
        state.AcceptTouch(event, window);
        return;
    default: return;
    }
}

void ControlMapper::SetExternalContact(const std::uint64_t id, const float x, const float y) {
    auto& state = *state_;
    if (!state.SurfaceReady() || !Finite(x) || !Finite(y))
        throw std::invalid_argument("external contact needs a guest surface and finite position");
    auto found = std::ranges::find_if(state.externals, [id](const auto& e) { return e.first == id; });
    if (found == state.externals.end()) {
        state.externals.push_back({id, {}});
        found = state.externals.end() - 1;
    }
    if (!found->second.held) found->second.latched = true;
    found->second.held = true;
    found->second.x = Clamp(x, state.width);
    found->second.y = Clamp(y, state.height);
}

void ControlMapper::ClearExternalContact(const std::uint64_t id) {
    auto& externals = state_->externals;
    const auto found = std::ranges::find_if(externals, [id](const auto& e) { return e.first == id; });
    if (found != externals.end()) found->second.held = false;
}

std::vector<ControlOutput> ControlMapper::Flush() { return state_->Flush(); }

void ControlMapper::Cancel() { state_->Cancel(); }

void ControlMapper::Reset() noexcept {
    auto& state = *state_;
    state.mouse = {};
    state.touches.clear();
    state.externals.clear();
    for (auto& rt : state.runtime) {
        std::fill(rt.held.begin(), rt.held.end(), false);
        std::fill(rt.latched.begin(), rt.latched.end(), false);
        rt.tap_pending = false;
        rt.key_down = false;
        rt.stick_active = false;
        rt.offset_x = rt.offset_y = 0;
    }
    state.overlays.clear();
    state.passthrough_down.clear();
    state.queued.clear();
    state.contacts.clear();
    state.rejected_owners.clear();
    state.pending_cancel = false;
}

std::vector<ControlOutput> ControlMapper::Reconfigure(ControlConfig config) {
    ControlMapper next(std::move(config));
    if (state_->SurfaceReady()) next.SetSurface(state_->width, state_->height);
    state_->Cancel();
    auto outputs = state_->Flush();
    next.state_->frame = state_->frame;
    std::swap(state_, next.state_);
    return outputs;
}

const ControlConfig& ControlMapper::Config() const noexcept { return state_->config; }

std::vector<ResolvedZoneView> ControlMapper::Layout() const {
    std::vector<ResolvedZoneView> layout;
    const auto& state = *state_;
    if (!state.SurfaceReady()) return layout;
    for (std::size_t i = 0; i < state.config.zones.size(); ++i) {
        const auto& zone = state.zones[i];
        layout.push_back({state.config.zones[i].name, zone.center_x, zone.center_y,
                          zone.half_width, zone.half_height, state.config.zones[i].radius > 0});
    }
    return layout;
}

bool ControlMapper::Active() const noexcept {
    const auto& state = *state_;
    if (!state.contacts.empty() || state.mouse.held || !state.touches.empty() ||
        !state.externals.empty())
        return true;
    return std::ranges::any_of(state.runtime, [](const auto& rt) {
        return std::find(rt.held.begin(), rt.held.end(), true) != rt.held.end();
    });
}

std::string_view ControlMapper::Mode() const noexcept { return state_->base_mode; }

std::uint64_t ControlMapper::RejectedContacts() const noexcept { return state_->rejected; }

}  // namespace ogplay::input
