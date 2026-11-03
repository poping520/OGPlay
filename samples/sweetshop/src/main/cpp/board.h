#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <vector>

namespace sweetshop {

constexpr int kBoardSize = 8;
constexpr int kRainbowColor = 6;

enum class Special : std::uint8_t { none, line_h, line_v, bomb, rainbow };

struct Piece {
    int color = -1;  // -1 empty, 0..5 candy, kRainbowColor for the rainbow candy
    Special special = Special::none;
    [[nodiscard]] bool Empty() const { return color < 0; }
    [[nodiscard]] bool Matchable() const { return color >= 0 && color < kRainbowColor; }
};

struct Pos {
    int x = 0, y = 0;
    bool operator==(const Pos& other) const { return x == other.x && y == other.y; }
};

struct Creation {
    Pos at;
    Piece piece;
};

// A triggered special, kept for the explosion visuals.
struct Effect {
    Special kind = Special::none;
    Pos at;
    int color = -1;
    int reach = 1;  // 1 normal; 2 or 3 for combined specials
};

struct Resolution {
    std::vector<Pos> cleared;  // unique cells removed this step
    std::vector<Piece> pieces;  // what each cleared cell held, same order
    std::vector<Creation> created;
    std::vector<Effect> effects;
    int score = 0;
    [[nodiscard]] bool Empty() const { return cleared.empty() && created.empty(); }
};

class Board {
public:
    void Fill(int colors, std::mt19937& random);
    [[nodiscard]] const Piece& At(Pos pos) const { return cells_[pos.y][pos.x]; }
    [[nodiscard]] static bool Inside(Pos pos) {
        return pos.x >= 0 && pos.y >= 0 && pos.x < kBoardSize && pos.y < kBoardSize;
    }
    [[nodiscard]] static bool Adjacent(Pos a, Pos b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y) == 1; }

    // A swap is legal when it makes a run, involves a rainbow, or pairs two specials.
    [[nodiscard]] bool IsLegalSwap(Pos a, Pos b) const;
    [[nodiscard]] bool FindMove(Pos* a, Pos* b) const;
    void Swap(Pos a, Pos b);

    // Specials that fire from the swap itself (rainbow or two specials); empty otherwise.
    [[nodiscard]] Resolution SwapCombo(Pos a, Pos b, std::mt19937& random) const;
    // Runs of three or more; `hints` choose where a created special lands.
    [[nodiscard]] Resolution FindMatches(const std::vector<Pos>& hints, int cascade, std::mt19937& random) const;
    void Apply(const Resolution& resolution);
    // Drops pieces and refills; returns the distance in cells each landed piece fell.
    std::array<std::array<int, kBoardSize>, kBoardSize> Collapse(int colors, std::mt19937& random);
    void Shuffle(int colors, std::mt19937& random);

private:
    using Grid = std::array<std::array<Piece, kBoardSize>, kBoardSize>;
    static bool RunAt(const Grid& grid, Pos pos);
    void Expand(Resolution& resolution, std::vector<Pos> queue, const std::vector<Pos>& keep,
                std::mt19937& random) const;

    Grid cells_{};
};

}  // namespace sweetshop
