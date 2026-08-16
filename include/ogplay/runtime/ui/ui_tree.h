#pragma once

#include <cstddef>
#include <cstdint>
#include <compare>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ogplay::runtime::ui {

class UiNodeId final {
public:
    constexpr UiNodeId() = default;
    explicit constexpr UiNodeId(const std::uint64_t value) : value_(value) {}

    [[nodiscard]] constexpr std::uint64_t Value() const { return value_; }
    [[nodiscard]] explicit constexpr operator bool() const { return value_ != 0; }
    constexpr auto operator<=>(const UiNodeId&) const = default;

private:
    std::uint64_t value_{};
};

struct UiNodeIdHash final {
    [[nodiscard]] std::size_t operator()(UiNodeId id) const noexcept;
};

enum class UiClass : std::uint8_t {
    ContentRoot,
    View,
    FrameLayout,
    LinearLayout,
    RelativeLayout,
    TextView,
    Button,
    ImageView,
    ImageButton,
    VideoView,
};

enum class Visibility : std::uint8_t {
    Visible,
    Invisible,
    Gone,
};

enum class SizeMode : std::uint8_t {
    Fixed,
    MatchParent,
    WrapContent,
};

enum class Orientation : std::uint8_t {
    Horizontal,
    Vertical,
};

enum class ImageScaleType : std::uint8_t {
    Center,
    CenterInside,
    FitCenter,
    FitXy,
    CenterCrop,
};

struct DimensionSpec final {
    SizeMode mode{SizeMode::WrapContent};
    std::int32_t px{};
};

struct Insets final {
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};
    constexpr auto operator<=>(const Insets&) const = default;
};

struct Size final {
    std::int32_t width{};
    std::int32_t height{};
    constexpr auto operator<=>(const Size&) const = default;
};

struct UiMetrics final {
    std::int32_t width{};
    std::int32_t height{};
};

enum class MeasureMode : std::uint8_t {
    Exactly,
    AtMost,
    Unspecified,
};

struct MeasureSpec final {
    MeasureMode mode{MeasureMode::Unspecified};
    std::int32_t size{};
};

struct Rect final {
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};
    constexpr auto operator<=>(const Rect&) const = default;
};

struct RelativeRules final {
    std::optional<std::int32_t> left_of;
    std::optional<std::int32_t> right_of;
    std::optional<std::int32_t> above;
    std::optional<std::int32_t> below;
    std::optional<std::int32_t> align_left;
    std::optional<std::int32_t> align_right;
    std::optional<std::int32_t> align_top;
    std::optional<std::int32_t> align_bottom;
    bool align_parent_left{};
    bool align_parent_right{};
    bool align_parent_top{};
    bool align_parent_bottom{};
    bool center_in_parent{};
    bool center_horizontal{};
    bool center_vertical{};
};

struct LayoutParams final {
    DimensionSpec width;
    DimensionSpec height;
    Insets margin;
    std::uint32_t layout_gravity{};
    float weight{};
    RelativeRules relative;
};

struct UiNode final {
    UiNodeId id;
    std::optional<UiNodeId> parent;
    std::vector<UiNodeId> children;
    UiClass kind{UiClass::View};
    std::int32_t android_id{-1};
    Visibility visibility{Visibility::Visible};
    bool enabled{true};
    bool clickable{};
    Orientation orientation{Orientation::Horizontal};
    std::uint32_t gravity{};
    std::uint32_t image_resource_id{};
    ImageScaleType image_scale_type{ImageScaleType::FitCenter};
    std::optional<std::uint32_t> background_color;
    std::u16string text;
    std::uint32_t text_color{0xffffffffU};
    float text_size_px{8.0F};
    std::int32_t max_lines{1};
    LayoutParams layout;
    Insets padding;
    Size measured;
    Size intrinsic;
    Rect frame;
    Rect screen_frame;
    float alpha{1.0F};
    bool layout_dirty{true};
    bool draw_dirty{true};
};

class UiTree final {
public:
    static constexpr std::size_t kMaxNodes = 4096;
    static constexpr std::size_t kMaxChildren = 1024;
    static constexpr std::size_t kMaxDepth = 128;

    UiTree();

    [[nodiscard]] UiNodeId Root() const { return root_; }
    [[nodiscard]] std::uint32_t Generation() const { return generation_; }
    [[nodiscard]] std::size_t Size() const { return nodes_.size(); }

    UiNodeId CreateNode(UiClass kind);
    void Attach(UiNodeId parent, UiNodeId child,
                std::optional<std::size_t> index = std::nullopt);
    void Detach(UiNodeId child);
    void DestroySubtree(UiNodeId node);
    void Reset();

    [[nodiscard]] UiNode* Get(UiNodeId id);
    [[nodiscard]] const UiNode* Get(UiNodeId id) const;
    [[nodiscard]] std::optional<UiNodeId> FindByAndroidId(
        std::int32_t android_id) const;

    void SetAndroidId(UiNodeId node, std::int32_t android_id);
    void SetVisibility(UiNodeId node, Visibility visibility);
    void SetEnabled(UiNodeId node, bool enabled);
    void SetClickable(UiNodeId node, bool clickable);
    void MarkLayoutDirty(UiNodeId node);
    void MarkDrawDirty(UiNodeId node);
    void ClearDirty();

private:
    using NodeMap =
        std::unordered_map<UiNodeId, UiNode, UiNodeIdHash>;

    [[nodiscard]] UiNode& Require(UiNodeId id);
    [[nodiscard]] const UiNode& Require(UiNodeId id) const;
    [[nodiscard]] bool IsAttached(UiNodeId id) const;
    void RebuildIndex();
    void IndexSubtree(UiNodeId node);
    void MarkAncestors(UiNodeId node, bool layout, bool draw);

    std::uint32_t generation_{};
    std::uint32_t next_node_{};
    UiNodeId root_;
    NodeMap nodes_;
    std::unordered_map<std::int32_t, std::vector<UiNodeId>> id_index_;
};

void LayoutUiTree(UiTree& tree, UiMetrics metrics);

}  // namespace ogplay::runtime::ui
