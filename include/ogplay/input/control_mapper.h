#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/hal/window_input.h"
#include "ogplay/input/touch_mapper.h"

namespace ogplay::input {

// Invalid control configuration: unknown names, conflicting sources, zones outside the surface.
class ControlConfigError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class Anchor : std::uint8_t {
    top_left, top, top_right, left, center, right, bottom_left, bottom, bottom_right,
};

// A zone box is placed so that its own anchor point sits at the surface anchor point plus
// (x, y) guest pixels; +x points right and +y points down. radius > 0 makes a circle whose
// bounding box is 2 * radius wide; otherwise width and height describe a rectangle.
struct ZoneSpec final {
    std::string name;
    Anchor anchor{Anchor::top_left};
    float x{}, y{};
    float width{}, height{};
    float radius{};
};

enum class SourceKind : std::uint8_t { key, mouse_button };

// key: USB HID / SDL physical scancode; mouse_button: hal::PointerButton value.
struct InputSource final {
    SourceKind kind{SourceKind::key};
    std::int32_t code{};
    bool operator==(const InputSource&) const = default;
};

enum class ActionKind : std::uint8_t { button, stick, key, mode };
enum class PressMode : std::uint8_t { tap, hold, repeat };
enum class ModeSwitch : std::uint8_t { toggle, hold };

inline constexpr std::string_view kDefaultMode = "default";
inline constexpr std::string_view kEveryMode = "*";

struct Binding final {
    std::string mode{kDefaultMode};
    ActionKind action{ActionKind::button};
    // stick: up, left, down, right; every other action: exactly one source.
    std::vector<InputSource> sources;
    std::string zone;                  // button, stick
    PressMode press{PressMode::hold};  // button
    std::uint32_t repeat_frames{};     // button repeat interval in frames, at least 2
    float stick_radius{0.7F};          // stick travel as a fraction of the zone half extent
    // Like a finger: rest at the origin for settle frames after pressing, then glide to each
    // new direction over ramp frames instead of jumping in a single MOVE.
    std::uint32_t stick_settle_frames{2};
    std::uint32_t stick_ramp_frames{4};
    std::int32_t android_key_code{};   // key
    std::string target_mode;           // mode
    ModeSwitch mode_switch{ModeSwitch::toggle};
};

struct ControlConfig final {
    std::vector<ZoneSpec> zones;
    std::vector<Binding> bindings;
};

// Built-in tier: mouse and host touch stay direct, Escape sends KEYCODE_BACK, other keys pass through.
[[nodiscard]] ControlConfig DefaultControlConfig();
// "key:<name>" with physical key names such as w, space, escape, f1, left_shift, up;
// "mouse:left|middle|right|x1|x2".
[[nodiscard]] InputSource ParseInputSource(std::string_view text);
// Inverse of ParseInputSource: "key:w", "mouse:right".
[[nodiscard]] std::string FormatInputSource(const InputSource& source);
[[nodiscard]] Anchor ParseAnchor(std::string_view text);
[[nodiscard]] std::string_view AnchorName(Anchor anchor);

// A zone resolved against the current guest surface.
struct ResolvedZoneView final {
    std::string name;
    float center_x{}, center_y{}, half_width{}, half_height{};
    bool circle{};
};

// Key hint for a zone under the active modes (ADR-0134 F1 overlay). A stick lists its four
// sources as up, left, down, right; a zone may carry several button sources.
struct ControlHint final {
    ResolvedZoneView zone;
    bool stick{};
    std::vector<InputSource> sources;
};

struct AndroidKeyOutput final {
    std::int32_t key_code{};
    bool pressed{};
    std::uint32_t modifiers{};
    std::uint32_t device_id{};
};

struct ControlOutput final {
    enum class Kind : std::uint8_t { touch, passthrough_key, android_key };
    Kind kind{Kind::touch};
    TouchSnapshot touch;
    hal::InputEvent passthrough;
    AndroidKeyOutput android_key;
};

// One logical touchscreen shared by host touch, mouse, bound keys and external (MCP) contacts.
// Accept() records host input; Flush() runs once per guest frame and returns the frame's
// ordered outputs. A contact pressed and released inside one frame is still held for that
// frame, so DOWN and UP are always at least one frame apart.
class ControlMapper final {
public:
    static constexpr std::size_t kMaxContacts = 16;

    explicit ControlMapper(ControlConfig config = DefaultControlConfig());
    ~ControlMapper();
    ControlMapper(const ControlMapper&) = delete;
    ControlMapper& operator=(const ControlMapper&) = delete;

    // Resolves zones against the guest surface. A size change cancels every active contact.
    void SetSurface(std::uint32_t width, std::uint32_t height);
    void Accept(const hal::InputEvent& event, const hal::WindowState& window);
    // External contacts use guest coordinates; ids belong to the caller.
    void SetExternalContact(std::uint64_t id, float x, float y);
    void ClearExternalContact(std::uint64_t id);
    [[nodiscard]] std::vector<ControlOutput> Flush();
    // Cancels contacts and bindings; the next Flush() reports CANCEL for any active contact.
    void Cancel();
    // Drops all state silently when the host already cancelled the guest input stream.
    void Reset() noexcept;
    // Validates the new configuration against the current surface before replacing the old
    // one; returns the old configuration's CANCEL and key releases to deliver.
    [[nodiscard]] std::vector<ControlOutput> Reconfigure(ControlConfig config);

    [[nodiscard]] const ControlConfig& Config() const noexcept;
    // Empty until SetSurface; zone order follows the configuration.
    [[nodiscard]] std::vector<ResolvedZoneView> Layout() const;
    // Zones reachable by a binding active in the current base mode, "*" or a held overlay.
    [[nodiscard]] std::vector<ControlHint> Hints() const;

    [[nodiscard]] bool Active() const noexcept;
    [[nodiscard]] std::string_view Mode() const noexcept;
    [[nodiscard]] std::uint64_t RejectedContacts() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace ogplay::input
