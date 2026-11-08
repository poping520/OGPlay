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

// Wheel pinch geometry as fractions of the surface width. The primary finger lands on the
// cursor and stays there, so the user chooses what the first touch hits; only the second finger
// moves, horizontally, span away from it.
constexpr float kPinchNotchScale = 1.2F;  // span ratio per wheel notch
constexpr float kPinchStartSpan = 0.30F;
constexpr float kPinchMinSpan = 0.08F;
constexpr float kPinchMaxSpan = 0.60F;
constexpr float kPinchStepPerFrame = 0.02F;
constexpr std::uint64_t kPinchSettleFrames = 2;  // rest after DOWN before the first MOVE
constexpr std::uint64_t kPinchLingerFrames = 3;  // still frames before lifting
// A look finger stops this far (fraction of the surface's shorter side) inside the surface edge,
// and like a real finger moves at most kLookMaxStepPerFrame per frame. Motion waiting beyond
// kLookBacklogSteps such steps is dropped, so the view stops within a frame or two of the mouse
// instead of sliding on after it.
constexpr float kLookEdgeMargin = 0.03F;
constexpr float kLookMaxStepPerFrame = 0.35F;
constexpr float kLookBacklogSteps = 2.0F;

// A wheel- or trackpad-driven two-finger pinch. pending_log is the not yet applied log span
// ratio; a span limit lifts the fingers and the remainder continues from a fresh grip at the
// start span. held: a trackpad gesture is in progress, so the fingers stay down while it pauses.
struct PinchState final {
    bool active{}, held{};
    float anchor_x{}, anchor_y{};
    float side{1.0F};  // +1: second finger to the right of the cursor, -1: to the left
    float max_span{};
    float span{}, target_span{};
    float pending_log{};
    std::uint64_t down_frame{}, still_since{};
};

struct OwnerKey final {
    enum class Kind : std::uint8_t { mouse, touch, external, binding, pinch };
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
    // stick and look: finger down, its press frame and offset from the zone center.
    bool stick_active{};
    std::uint64_t stick_down_frame{};
    float offset_x{}, offset_y{};
    // look: motion not yet applied (guest pixels), a regrip due next frame, and which of two
    // finger identities is current, so a regrip lifts one finger and presses the next together.
    float look_dx{}, look_dy{};
    bool look_regrip{};
    std::uint64_t look_slot{};
    // Handoff regrip: the previous finger stays down one more frame at this offset.
    bool look_old_held{};
    float look_old_x{}, look_old_y{};
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
        for (const auto& source : binding.sources) {
            if ((source.kind == SourceKind::mouse_motion) != (binding.action == ActionKind::look))
                throw ControlConfigError("mouse:motion drives exactly the look action");
            if (binding.action == ActionKind::lock && source.kind != SourceKind::key)
                throw ControlConfigError("the pointer lock toggles from a key");
            if (source.kind == SourceKind::wheel && binding.action == ActionKind::stick)
                throw ControlConfigError("a stick cannot use the wheel");
        }
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
        case ActionKind::look:
            if (!has_zone(binding.zone)) throw ControlConfigError("unknown zone: " + binding.zone);
            for (const float sensitivity : {binding.look_sensitivity_x, binding.look_sensitivity_y}) {
                if (!Finite(sensitivity) || sensitivity <= 0 || sensitivity > 100)
                    throw ControlConfigError("look sensitivity must be in (0, 100]");
            }
            if (binding.look_settle_frames > 60) throw ControlConfigError("look settle frames must be 0..60");
            for (const float offset : {binding.look_max_offset_x, binding.look_max_offset_y}) {
                if (!Finite(offset) || offset < 0 || offset > 4096)
                    throw ControlConfigError("look max offsets must be 0..4096 guest pixels");
            }
            break;
        case ActionKind::lock: break;
        }
        if (binding.release_pointer && binding.action != ActionKind::button && binding.action != ActionKind::key)
            throw ControlConfigError("only buttons and keys can free the pointer");
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
    // Back usually opens a menu that needs the mouse; the user may untick this.
    back.release_pointer = true;
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
            if (name == "motion") return {SourceKind::mouse_motion, 0};
            for (const auto& entry : kMouseNames)
                if (entry.name == name) return {SourceKind::mouse_button, entry.code};
        } else if (kind == "wheel") {
            if (name == "up") return {SourceKind::wheel, 1};
            if (name == "down") return {SourceKind::wheel, -1};
        }
    }
    throw ControlConfigError("unknown input source: " + std::string{text});
}

std::string FormatInputSource(const InputSource& source) {
    if (source.kind == SourceKind::mouse_motion) return "mouse:motion";
    if (source.kind == SourceKind::wheel) {
        if (source.code == 1) return "wheel:up";
        if (source.code == -1) return "wheel:down";
        throw ControlConfigError("unknown wheel direction " + std::to_string(source.code));
    }
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
    PinchState pinch;
    float wheel_notches{};        // fractional wheel travel toward the next bound wheel press
    bool pointer_released{true};  // the cursor is free: look bindings pause
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

    [[nodiscard]] bool LookActive() const {
        return std::ranges::any_of(config.bindings,
                                   [&](const auto& b) { return b.action == ActionKind::look && IsActive(b); });
    }

    [[nodiscard]] bool WantsRelative() const { return !pointer_released && LookActive(); }

    // A look is set up but the pointer is free (not yet locked, or freed for a menu): the mouse,
    // its buttons and the wheel act directly instead of through bindings.
    [[nodiscard]] bool MouseFreed() const { return pointer_released && LookActive(); }

    bool TogglePointerLock() {
        if (!pointer_released) {
            pointer_released = true;
            return true;
        }
        if (!LookActive()) return false;
        pointer_released = false;
        return true;
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
        rt.look_dx = rt.look_dy = 0;
        rt.look_regrip = rt.look_old_held = false;
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
        if (binding.release_pointer && LookActive()) pointer_released = true;
        switch (binding.action) {
        case ActionKind::button:
            if (binding.press == PressMode::tap) rt.tap_pending = true;
            if (binding.press == PressMode::repeat) rt.start_frame = frame;
            break;
        case ActionKind::stick:
        case ActionKind::look: break;
        case ActionKind::lock: static_cast<void>(TogglePointerLock()); break;
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
        if (event.type == hal::InputEventType::pointer_button) {
            // A host-released pointer is for clicking menus: its buttons touch directly, though a
            // binding held from before the release still lets go through its binding.
            const InputSource source{SourceKind::mouse_button, event.code};
            if ((!MouseFreed() || Engaged(source)) && RouteSource(source, event.pressed, event.device_id)) return;
        }
        if (!SurfaceReady()) return;
        if (event.type == hal::InputEventType::pointer_motion && WantsRelative()) {
            if (const auto index = Route({SourceKind::mouse_motion, 0})) {
                AddLook(*index, event.delta_x, event.delta_y);
                return;
            }
        }
        // A captured pointer has no meaningful position: it presses nothing directly, though a
        // drag that began before the capture still ends normally.
        if (event.type == hal::InputEventType::pointer_button && event.pressed && WantsRelative()) return;
        const bool primary = event.code == static_cast<std::int32_t>(hal::PointerButton::primary);
        const auto mapped = hal::MapDisplayPoint(event.x, event.y, width, height,
                                                 window.width, window.height);
        if (event.type == hal::InputEventType::pointer_button) {
            if (!primary) return;
            if (event.pressed) {
                if (mouse.held || !mapped.inside) return;
                pinch = {};
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
            pinch = {};
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

    void AddLook(const std::size_t index, const float dx, const float dy) {
        if (!Finite(dx) || !Finite(dy)) return;
        const auto& binding = config.bindings[index];
        auto& rt = runtime[index];
        const float cap = static_cast<float>(std::min(width, height)) * kLookMaxStepPerFrame * kLookBacklogSteps;
        rt.look_dx = std::clamp(rt.look_dx + dx * binding.look_sensitivity_x, -cap, cap);
        const float vertical = dy * binding.look_sensitivity_y * (binding.look_invert_y ? -1.0F : 1.0F);
        rt.look_dy = std::clamp(rt.look_dy + vertical, -cap, cap);
    }

    // A wheel bound in the active modes presses its binding once per whole notch and leaves the
    // built-in pinch alone. Returns false when neither direction is bound.
    bool RouteWheel(const hal::InputEvent& event) {
        const InputSource up{SourceKind::wheel, 1};
        const InputSource down{SourceKind::wheel, -1};
        if (MouseFreed() || (!Route(up) && !Route(down))) return false;
        if (wheel_notches * event.delta_y < 0) wheel_notches = 0;
        wheel_notches += event.delta_y;
        for (int i = 0; i < 3 && std::abs(wheel_notches) >= 1; ++i) {
            const auto& source = wheel_notches > 0 ? up : down;
            wheel_notches -= std::copysign(1.0F, wheel_notches);
            RouteSource(source, true, event.device_id);
            RouteSource(source, false, event.device_id);
        }
        if (std::abs(wheel_notches) >= 1) wheel_notches = 0;
        return true;
    }

    // Only the pinch's own fingers may be down: a wheel turn never adds two fingers to a drag,
    // a held key contact or an external gesture.
    [[nodiscard]] bool PinchBlocked() const {
        return mouse.held || mouse.latched || !touches.empty() || !externals.empty() ||
               std::ranges::any_of(contacts, [](const auto& c) { return c.owner.kind != OwnerKey::Kind::pinch; });
    }

    [[nodiscard]] float PinchUnit() const { return static_cast<float>(width); }

    void AddPinchLog(const float log_ratio) {
        const float limit = std::log(kPinchMaxSpan / kPinchMinSpan);
        pinch.pending_log = std::clamp(pinch.pending_log + log_ratio, -limit, limit);
    }

    // Places the primary on the cursor while no fingers are down. The second finger goes right
    // while the start span fits there, otherwise to the roomier side; the widest span stays
    // inside the surface on that side.
    void AimPinch(const hal::MappedDisplayPoint& mapped) {
        if (pinch.active) return;
        pinch.anchor_x = Clamp(mapped.x, width);
        pinch.anchor_y = Clamp(mapped.y, height);
        const float right = std::nextafter(static_cast<float>(width), 0.0F) - pinch.anchor_x;
        const float left = pinch.anchor_x;
        pinch.side = right >= kPinchStartSpan * PinchUnit() || right >= left ? 1.0F : -1.0F;
        pinch.max_span = std::min(kPinchMaxSpan * PinchUnit(), pinch.side > 0 ? right : left);
    }

    void AcceptWheel(const hal::InputEvent& event, const hal::WindowState& window) {
        if (!Finite(event.delta_y) || event.delta_y == 0 || RouteWheel(event)) return;
        if (!SurfaceReady() || pinch.held || WantsRelative() || PinchBlocked()) return;
        const auto mapped = hal::MapDisplayPoint(event.x, event.y, width, height, window.width, window.height);
        if (!mapped.inside) return;
        AddPinchLog(event.delta_y * std::log(kPinchNotchScale));
        AimPinch(mapped);
    }

    // Trackpad pinch: begin grips at the cursor, each update applies its span ratio, end lets the
    // fingers lift after the usual still frames. Updates without an accepted begin are ignored.
    void AcceptPinch(const hal::InputEvent& event, const hal::WindowState& window) {
        using Type = hal::InputEventType;
        if (event.type == Type::pinch_begin) {
            if (!SurfaceReady() || pinch.held || WantsRelative() || PinchBlocked()) return;
            const auto mapped = hal::MapDisplayPoint(event.x, event.y, width, height, window.width, window.height);
            if (!mapped.inside) return;
            AimPinch(mapped);
            pinch.held = true;
        } else if (event.type == Type::pinch_update) {
            if (pinch.held && Finite(event.scale) && event.scale > 0) AddPinchLog(std::log(event.scale));
        } else {
            pinch.held = false;
        }
    }

    // Grips, rests, glides toward the wheel's span and lifts after a few still frames; a span
    // limit with wheel input left over lifts at once so the next frame grips again.
    void UpdatePinch() {
        if (!SurfaceReady()) {
            pinch = {};
            return;
        }
        if (!pinch.active) {
            if (pinch.pending_log == 0 && !pinch.held) return;
            if (PinchBlocked()) {
                pinch = {};
                return;
            }
            pinch.active = true;
            pinch.span = pinch.target_span = std::min(kPinchStartSpan * PinchUnit(), pinch.max_span);
            pinch.down_frame = pinch.still_since = frame;
            return;
        }
        if (frame - pinch.down_frame <= kPinchSettleFrames) return;
        const float unit = PinchUnit();
        const float wanted = pinch.target_span * std::exp(pinch.pending_log);
        const float target = std::clamp(wanted, std::min(kPinchMinSpan * unit, pinch.max_span), pinch.max_span);
        pinch.pending_log = wanted == target ? 0 : std::log(wanted / target);
        pinch.target_span = target;
        const float rest = pinch.target_span - pinch.span;
        const float step = kPinchStepPerFrame * unit;
        if (rest != 0) {
            pinch.span = std::abs(rest) <= step ? pinch.target_span : pinch.span + std::copysign(step, rest);
            pinch.still_since = frame;
            return;
        }
        if (pinch.pending_log != 0 || (!pinch.held && frame - pinch.still_since >= kPinchLingerFrames)) {
            pinch.active = false;  // the remainder, if any, grips again at the same anchor
        }
    }

    void Cancel() {
        pending_cancel = pending_cancel || !contacts.empty();
        mouse = {};
        pinch = {};
        wheel_notches = 0;
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
        if (binding.action != ActionKind::button && binding.action != ActionKind::stick &&
            binding.action != ActionKind::look)
            return;
        if (!SurfaceReady()) return;
        const auto& zone = zones[binding_zone[index]];
        const OwnerKey owner{OwnerKey::Kind::binding, index, binding.action == ActionKind::look ? rt.look_slot : 0};
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
        if (rt.look_old_held) {
            requests.push_back({{OwnerKey::Kind::binding, index, rt.look_slot ^ 1U},
                                Clamp(zone.center_x + rt.look_old_x, width),
                                Clamp(zone.center_y + rt.look_old_y, height),
                                1.0F});
        }
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

    // Advances each look finger one frame. The finger presses at the zone center once motion
    // arrives while the pointer is locked, rests settle frames, then follows the accumulated
    // motion, at most kLookMaxStepPerFrame of the shorter side per frame, anywhere on the surface
    // short of an edge margin (and within the max offsets). At a limit it stops there; next frame it
    // lifts and the frame after a fresh finger presses at the center (with handoff, the fresh finger
    // presses first and the old one lifts a frame later), and the remaining motion carries on. It
    // stays down until unlock or a mode change.
    void UpdateLooks() {
        for (std::size_t index = 0; index < config.bindings.size(); ++index) {
            const auto& binding = config.bindings[index];
            if (binding.action != ActionKind::look) continue;
            auto& rt = runtime[index];
            if (!SurfaceReady() || !IsActive(binding) || pointer_released) {
                rt.stick_active = rt.look_regrip = rt.look_old_held = false;
                rt.offset_x = rt.offset_y = rt.look_dx = rt.look_dy = 0;
                continue;
            }
            rt.look_old_held = false;  // a handed-off finger lifts the frame after the new one lands
            if (!rt.stick_active) {
                if (rt.look_dx == 0 && rt.look_dy == 0) continue;
                rt.stick_active = true;
                rt.stick_down_frame = frame;
                rt.offset_x = rt.offset_y = 0;
                continue;
            }
            if (rt.look_regrip) {
                rt.look_regrip = false;
                if (!binding.look_handoff) {
                    // Lift now and press again next frame: engines that sample touches per frame
                    // (Unity) merge a same-frame lift and press of one pointer id into a jump back.
                    rt.stick_active = false;
                    rt.offset_x = rt.offset_y = 0;
                    continue;
                }
                rt.look_old_held = true;
                rt.look_old_x = rt.offset_x;
                rt.look_old_y = rt.offset_y;
                rt.look_slot ^= 1U;
                rt.stick_down_frame = frame;
                rt.offset_x = rt.offset_y = 0;
                continue;
            }
            if (frame - rt.stick_down_frame <= binding.look_settle_frames) continue;
            if (rt.look_dx == 0 && rt.look_dy == 0) continue;
            const auto& zone = zones[binding_zone[index]];
            const float shorter = static_cast<float>(std::min(width, height));
            const float margin = shorter * kLookEdgeMargin;
            // Offset limits per axis: from the center to the surface margin, cut to the max offset.
            const auto limits = [&](const float center, const std::uint32_t extent, const float max_offset) {
                float low = margin - center;
                float high = static_cast<float>(extent) - margin - center;
                if (max_offset > 0) {
                    low = std::max(low, -max_offset);
                    high = std::min(high, max_offset);
                }
                return std::pair{std::min(low, 0.0F), std::max(high, 0.0F)};
            };
            // Take this frame's share of the motion, then stop at the limits; what does not fit
            // stays pending.
            const float length = std::hypot(rt.look_dx, rt.look_dy);
            const float share = std::min(1.0F, shorter * kLookMaxStepPerFrame / length);
            bool stopped = false;
            const auto follow = [&](float& offset, float& pending, const std::pair<float, float> range) {
                const float step = pending * share;
                const float wanted = offset + step;
                offset = std::clamp(wanted, range.first, range.second);
                pending -= offset - (wanted - step);
                stopped = stopped || wanted != offset;
            };
            follow(rt.offset_x, rt.look_dx, limits(zone.center_x, width, binding.look_max_offset_x));
            follow(rt.offset_y, rt.look_dy, limits(zone.center_y, height, binding.look_max_offset_y));
            if (std::abs(rt.look_dx) < 1e-3F) rt.look_dx = 0;
            if (std::abs(rt.look_dy) < 1e-3F) rt.look_dy = 0;
            rt.look_regrip = stopped && (rt.look_dx != 0 || rt.look_dy != 0);
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
        if (pinch.active) {
            // Primary first: it becomes ACTION_DOWN, the moving finger ACTION_POINTER_DOWN.
            requests.push_back({{OwnerKey::Kind::pinch, 0, 0}, pinch.anchor_x, pinch.anchor_y, 1.0F});
            requests.push_back({{OwnerKey::Kind::pinch, 1, 0},
                                Clamp(pinch.anchor_x + pinch.side * pinch.span, width),
                                pinch.anchor_y,
                                1.0F});
        }
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
        UpdateLooks();
        UpdatePinch();
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
        if (binding.action == ActionKind::button || binding.action == ActionKind::stick ||
            binding.action == ActionKind::look) {
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
    case Type::pointer_wheel: state.AcceptWheel(event, window); return;
    case Type::pinch_begin:
    case Type::pinch_update:
    case Type::pinch_end: state.AcceptPinch(event, window); return;
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
    state.pinch = {};
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
    state.pinch = {};
    state.wheel_notches = 0;
    state.touches.clear();
    state.externals.clear();
    for (auto& rt : state.runtime) {
        std::fill(rt.held.begin(), rt.held.end(), false);
        std::fill(rt.latched.begin(), rt.latched.end(), false);
        rt.tap_pending = false;
        rt.key_down = false;
        rt.stick_active = false;
        rt.offset_x = rt.offset_y = 0;
        rt.look_dx = rt.look_dy = 0;
        rt.look_regrip = rt.look_old_held = false;
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
    next.state_->pointer_released = state_->pointer_released;  // the lock survives a key edit
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

std::vector<ControlHint> ControlMapper::Hints() const {
    std::vector<ControlHint> hints;
    const auto& state = *state_;
    if (!state.SurfaceReady()) return hints;
    const auto layout = Layout();
    for (std::size_t i = 0; i < state.config.bindings.size(); ++i) {
        const auto& binding = state.config.bindings[i];
        if (!state.IsActive(binding) || binding.zone.empty()) continue;
        // A freed mouse drives no binding, so its mouse and wheel zones are not offered.
        if (state.MouseFreed() &&
            std::ranges::none_of(binding.sources, [](const auto& source) { return source.kind == SourceKind::key; }))
            continue;
        const auto& zone = layout[state.binding_zone[i]];
        const bool stick = binding.action == ActionKind::stick;
        auto found = std::ranges::find_if(hints, [&](const auto& hint) {
            return hint.zone.name == zone.name && hint.stick == stick;
        });
        if (found == hints.end() || stick) {
            hints.push_back({zone, stick, binding.sources});
        } else {
            found->sources.insert(found->sources.end(), binding.sources.begin(), binding.sources.end());
        }
    }
    return hints;
}

bool ControlMapper::Active() const noexcept {
    const auto& state = *state_;
    if (!state.contacts.empty() || state.mouse.held || !state.touches.empty() || !state.externals.empty() ||
        state.pinch.active || state.pinch.held || state.pinch.pending_log != 0)
        return true;
    return std::ranges::any_of(state.runtime, [](const auto& rt) {
        return std::find(rt.held.begin(), rt.held.end(), true) != rt.held.end() || rt.stick_active || rt.look_dx != 0 ||
               rt.look_dy != 0;
    });
}

bool ControlMapper::WantsRelativePointer() const noexcept {
    return state_->WantsRelative();
}

void ControlMapper::SetPointerReleased(const bool released) noexcept {
    state_->pointer_released = released;
}

bool ControlMapper::PointerReleased() const noexcept {
    return state_->pointer_released;
}

bool ControlMapper::TogglePointerLock() noexcept {
    return state_->TogglePointerLock();
}

bool ControlMapper::LookActive() const noexcept {
    return state_->LookActive();
}

std::vector<InputSource> ControlMapper::LockSources() const {
    std::vector<InputSource> sources;
    for (const auto& binding : state_->config.bindings) {
        if (binding.action == ActionKind::lock && state_->IsActive(binding))
            sources.insert(sources.end(), binding.sources.begin(), binding.sources.end());
    }
    return sources;
}

std::string_view ControlMapper::Mode() const noexcept { return state_->base_mode; }

std::uint64_t ControlMapper::RejectedContacts() const noexcept { return state_->rejected; }

}  // namespace ogplay::input
