#include "board.h"

#include <algorithm>
#include <cstdlib>
#include <numeric>

namespace sweetshop {
namespace {

struct Run {
    std::vector<Pos> cells;
    bool horizontal = true;
};

int RandomColor(const int colors, std::mt19937& random) {
    return std::uniform_int_distribution<int>(0, colors - 1)(random);
}

bool Contains(const std::vector<Pos>& list, const Pos pos) {
    return std::find(list.begin(), list.end(), pos) != list.end();
}

int Find(std::vector<int>& parent, int index) {
    while (parent[index] != index) index = parent[index] = parent[parent[index]];
    return index;
}

}  // namespace

void Board::Fill(const int colors, std::mt19937& random) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        for (int y = 0; y < kBoardSize; ++y)
            for (int x = 0; x < kBoardSize; ++x) {
                int color = 0;
                do {
                    color = RandomColor(colors, random);
                } while ((x >= 2 && cells_[y][x - 1].color == color && cells_[y][x - 2].color == color) ||
                         (y >= 2 && cells_[y - 1][x].color == color && cells_[y - 2][x].color == color));
                cells_[y][x] = {color, Special::none};
            }
        Pos a, b;
        if (FindMove(&a, &b)) return;
    }
}

bool Board::RunAt(const Grid& grid, const Pos pos) {
    const Piece& piece = grid[pos.y][pos.x];
    if (!piece.Matchable()) return false;
    const auto same = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < kBoardSize && y < kBoardSize && grid[y][x].Matchable() &&
               grid[y][x].color == piece.color;
    };
    int horizontal = 1, vertical = 1;
    for (int x = pos.x - 1; same(x, pos.y); --x) ++horizontal;
    for (int x = pos.x + 1; same(x, pos.y); ++x) ++horizontal;
    for (int y = pos.y - 1; same(pos.x, y); --y) ++vertical;
    for (int y = pos.y + 1; same(pos.x, y); ++y) ++vertical;
    return horizontal >= 3 || vertical >= 3;
}

bool Board::IsLegalSwap(const Pos a, const Pos b) const {
    if (!Inside(a) || !Inside(b) || !Adjacent(a, b)) return false;
    const Piece& first = At(a);
    const Piece& second = At(b);
    if (first.Empty() || second.Empty()) return false;
    if (first.color == kRainbowColor || second.color == kRainbowColor) return true;
    if (first.special != Special::none && second.special != Special::none) return true;
    Grid grid = cells_;
    std::swap(grid[a.y][a.x], grid[b.y][b.x]);
    return RunAt(grid, a) || RunAt(grid, b);
}

bool Board::FindMove(Pos* a, Pos* b) const {
    for (int y = 0; y < kBoardSize; ++y)
        for (int x = 0; x < kBoardSize; ++x)
            for (const Pos next : {Pos{x + 1, y}, Pos{x, y + 1}})
                if (IsLegalSwap({x, y}, next)) {
                    *a = {x, y};
                    *b = next;
                    return true;
                }
    return false;
}

void Board::Swap(const Pos a, const Pos b) { std::swap(cells_[a.y][a.x], cells_[b.y][b.x]); }

void Board::Expand(Resolution& resolution, std::vector<Pos> queue, const std::vector<Pos>& keep,
                   std::mt19937& random) const {
    std::array<std::array<bool, kBoardSize>, kBoardSize> marked{};
    for (const Pos pos : resolution.cleared) marked[pos.y][pos.x] = true;
    for (std::size_t next = 0; next < queue.size(); ++next) {
        const Pos pos = queue[next];
        if (!Inside(pos) || marked[pos.y][pos.x] || Contains(keep, pos) || At(pos).Empty()) continue;
        marked[pos.y][pos.x] = true;
        const Piece piece = At(pos);
        resolution.cleared.push_back(pos);
        resolution.pieces.push_back(piece);
        switch (piece.special) {
        case Special::none:
            break;
        case Special::line_h:
            for (int x = 0; x < kBoardSize; ++x) queue.push_back({x, pos.y});
            resolution.effects.push_back({Special::line_h, pos, piece.color, 1});
            break;
        case Special::line_v:
            for (int y = 0; y < kBoardSize; ++y) queue.push_back({pos.x, y});
            resolution.effects.push_back({Special::line_v, pos, piece.color, 1});
            break;
        case Special::bomb:
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) queue.push_back({pos.x + dx, pos.y + dy});
            resolution.effects.push_back({Special::bomb, pos, piece.color, 1});
            break;
        case Special::rainbow: {
            // Set off by another blast: take a random color still on the board.
            std::vector<int> colors;
            for (const auto& row : cells_)
                for (const Piece& other : row)
                    if (other.Matchable()) colors.push_back(other.color);
            if (colors.empty()) break;
            const int color = colors[std::uniform_int_distribution<std::size_t>(0, colors.size() - 1)(random)];
            for (int y = 0; y < kBoardSize; ++y)
                for (int x = 0; x < kBoardSize; ++x)
                    if (cells_[y][x].color == color) queue.push_back({x, y});
            resolution.effects.push_back({Special::rainbow, pos, color, 1});
            break;
        }
        }
    }
}

Resolution Board::SwapCombo(const Pos a, const Pos b, std::mt19937& random) const {
    Resolution resolution;
    const Piece& first = At(a);
    const Piece& second = At(b);
    std::vector<Pos> queue;
    const auto consume = [&](Pos pos) {
        resolution.cleared.push_back(pos);
        resolution.pieces.push_back(At(pos));
    };
    if (first.color == kRainbowColor || second.color == kRainbowColor) {
        const Pos rainbow = first.color == kRainbowColor ? a : b;
        const Pos other = rainbow == a ? b : a;
        const int color = At(other).color;
        consume(rainbow);
        if (color == kRainbowColor) {
            consume(other);
            for (int y = 0; y < kBoardSize; ++y)
                for (int x = 0; x < kBoardSize; ++x) queue.push_back({x, y});
            resolution.effects.push_back({Special::rainbow, rainbow, -1, 3});
        } else {
            for (int y = 0; y < kBoardSize; ++y)
                for (int x = 0; x < kBoardSize; ++x)
                    if (cells_[y][x].color == color) queue.push_back({x, y});
            resolution.effects.push_back({Special::rainbow, rainbow, color, 1});
        }
    } else if (first.special != Special::none && second.special != Special::none) {
        consume(a);
        consume(b);
        const bool first_bomb = first.special == Special::bomb, second_bomb = second.special == Special::bomb;
        if (first_bomb && second_bomb) {
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx) queue.push_back({b.x + dx, b.y + dy});
            resolution.effects.push_back({Special::bomb, b, second.color, 2});
        } else {
            const int reach = first_bomb || second_bomb ? 1 : 0;
            for (int offset = -reach; offset <= reach; ++offset)
                for (int index = 0; index < kBoardSize; ++index) {
                    queue.push_back({index, b.y + offset});
                    queue.push_back({b.x + offset, index});
                }
            resolution.effects.push_back({Special::line_h, b, second.color, reach * 2 + 1});
            resolution.effects.push_back({Special::line_v, b, second.color, reach * 2 + 1});
        }
    } else {
        return resolution;
    }
    Expand(resolution, std::move(queue), {}, random);
    resolution.score = static_cast<int>(resolution.cleared.size()) * 30 + 200;
    return resolution;
}

Resolution Board::FindMatches(const std::vector<Pos>& hints, const int cascade, std::mt19937& random) const {
    std::vector<Run> runs;
    for (int horizontal = 1; horizontal >= 0; --horizontal)
        for (int line = 0; line < kBoardSize; ++line) {
            const auto cell = [&](int index) { return horizontal ? Pos{index, line} : Pos{line, index}; };
            int start = 0;
            while (start < kBoardSize) {
                const Piece& piece = At(cell(start));
                int end = start;
                while (piece.Matchable() && end + 1 < kBoardSize && At(cell(end + 1)).Matchable() &&
                       At(cell(end + 1)).color == piece.color)
                    ++end;
                if (piece.Matchable() && end - start + 1 >= 3) {
                    Run run;
                    run.horizontal = horizontal != 0;
                    for (int index = start; index <= end; ++index) run.cells.push_back(cell(index));
                    runs.push_back(std::move(run));
                }
                start = end + 1;
            }
        }
    Resolution resolution;
    if (runs.empty()) return resolution;

    std::vector<int> parent(runs.size());
    std::iota(parent.begin(), parent.end(), 0);
    for (std::size_t i = 0; i < runs.size(); ++i)
        for (std::size_t j = i + 1; j < runs.size(); ++j)
            for (const Pos pos : runs[i].cells)
                if (Contains(runs[j].cells, pos)) {
                    parent[Find(parent, static_cast<int>(i))] = Find(parent, static_cast<int>(j));
                    break;
                }

    std::vector<Pos> queue, keep;
    for (std::size_t root = 0; root < runs.size(); ++root) {
        if (Find(parent, static_cast<int>(root)) != static_cast<int>(root)) continue;
        std::vector<Pos> cells;
        const Run* longest = nullptr;
        bool has_horizontal = false, has_vertical = false;
        for (std::size_t index = 0; index < runs.size(); ++index) {
            if (Find(parent, static_cast<int>(index)) != static_cast<int>(root)) continue;
            const Run& run = runs[index];
            (run.horizontal ? has_horizontal : has_vertical) = true;
            if (longest == nullptr || run.cells.size() > longest->cells.size()) longest = &run;
            for (const Pos pos : run.cells)
                if (!Contains(cells, pos)) cells.push_back(pos);
        }
        const int color = At(cells.front()).color;
        Special create = Special::none;
        if (longest->cells.size() >= 5) create = Special::rainbow;
        else if (has_horizontal && has_vertical) create = Special::bomb;
        else if (longest->cells.size() == 4) create = longest->horizontal ? Special::line_h : Special::line_v;
        if (create != Special::none) {
            Pos spawn = longest->cells[longest->cells.size() / 2];
            bool chosen = false;
            for (const Pos hint : hints)
                if (Contains(cells, hint) && At(hint).special == Special::none) {
                    spawn = hint;
                    chosen = true;
                    break;
                }
            if (!chosen)
                for (const Pos pos : longest->cells)
                    if (At(pos).special == Special::none) {
                        spawn = pos;
                        break;
                    }
            keep.push_back(spawn);
            resolution.created.push_back(
                {spawn, {create == Special::rainbow ? kRainbowColor : color, create}});
        }
        for (const Pos pos : cells) queue.push_back(pos);
    }
    Expand(resolution, std::move(queue), keep, random);
    resolution.score = static_cast<int>(resolution.cleared.size()) * 20 * cascade +
                       static_cast<int>(resolution.created.size()) * 60 +
                       static_cast<int>(resolution.effects.size()) * 100;
    return resolution;
}

void Board::Apply(const Resolution& resolution) {
    for (const Pos pos : resolution.cleared) cells_[pos.y][pos.x] = {};
    for (const Creation& creation : resolution.created) cells_[creation.at.y][creation.at.x] = creation.piece;
}

std::array<std::array<int, kBoardSize>, kBoardSize> Board::Collapse(const int colors, std::mt19937& random) {
    std::array<std::array<int, kBoardSize>, kBoardSize> falls{};
    for (int x = 0; x < kBoardSize; ++x) {
        int write = kBoardSize - 1;
        for (int y = kBoardSize - 1; y >= 0; --y) {
            if (cells_[y][x].Empty()) continue;
            if (y != write) {
                cells_[write][x] = cells_[y][x];
                cells_[y][x] = {};
            }
            falls[write][x] = write - y;
            --write;
        }
        const int missing = write + 1;
        for (int y = write; y >= 0; --y) {
            cells_[y][x] = {RandomColor(colors, random), Special::none};
            falls[y][x] = missing;
        }
    }
    return falls;
}

void Board::Shuffle(const int colors, std::mt19937& random) {
    std::vector<Piece> pieces;
    for (const auto& row : cells_)
        for (const Piece& piece : row) pieces.push_back(piece);
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::shuffle(pieces.begin(), pieces.end(), random);
        for (int index = 0; index < kBoardSize * kBoardSize; ++index)
            cells_[index / kBoardSize][index % kBoardSize] = pieces[index];
        bool runs = false;
        for (int y = 0; y < kBoardSize && !runs; ++y)
            for (int x = 0; x < kBoardSize && !runs; ++x) runs = RunAt(cells_, {x, y});
        Pos a, b;
        if (!runs && FindMove(&a, &b)) return;
    }
    Fill(colors, random);
}

}  // namespace sweetshop
