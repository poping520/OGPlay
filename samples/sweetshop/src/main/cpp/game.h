#pragma once

#include <array>
#include <random>
#include <string>
#include <vector>

#include "audio.h"
#include "board.h"
#include "gfx.h"

namespace sweetshop {

struct Level {
    int moves;
    int target;  // one star; two and three stars at kTwoStars / kThreeStars times this
    int colors;
};

class Game {
public:
    // `english` is the device default; a choice made in game is saved and wins.
    void Init(Audio* audio, std::string save_path, bool english);
    void Update(float dt);
    void Render(Renderer& renderer);
    void PointerDown(float x, float y);
    void PointerMove(float x, float y);
    void PointerUp(float x, float y);
    // Returns false when the title screen should let the activity close.
    bool Back();
    void Save() const;

private:
    enum class Screen { splash, title, levels, play };
    enum class Phase { idle, swapping, swap_back, clearing, falling, result };
    enum class Action { none, play, sound, home, retry, next, language };

    struct Cell {
        float dx = 0, dy = 0;  // visual offset in cells, eased back to zero
        float velocity = 0;
        float pop = 1;  // scale for newly created specials
    };
    struct Vanish {
        Pos at;
        Piece piece;
        float time = 0;
    };
    struct Particle {
        float x, y, vx, vy, life, max_life, size, rotation, spin;
        Color color;
        bool sparkle;
    };
    struct Popup {
        float x, y;
        int value;
        float time;
    };
    struct Blast {
        Effect effect;
        float time;
    };
    struct Button {
        Action action;
        const char* icon;
        float x, y, size;
        [[nodiscard]] bool Hit(float px, float py) const;
    };

    void Load();
    void StartLevel(int level);
    void RenderSplash(Renderer& renderer);
    void RenderTitle(Renderer& renderer);
    void RenderLevels(Renderer& renderer);
    void RenderPlay(Renderer& renderer);
    void RenderResult(Renderer& renderer);
    void RenderPiece(Renderer& renderer, const Piece& piece, float x, float y, float size, float alpha) const;
    void TrySwap(Pos a, Pos b);
    void Resolve(Resolution resolution);
    void UpdatePlay(float dt);
    void Finish();
    void Burst(float x, float y, Color color, int count, float speed);
    [[nodiscard]] Pos CellAt(float x, float y) const;
    [[nodiscard]] static float CellX(int x);
    [[nodiscard]] static float CellY(int y);
    [[nodiscard]] int Stars(int score) const;
    [[nodiscard]] std::vector<Button> Buttons() const;
    void Activate(Action action);
    // Draws the label `key` in the current language.
    void Say(Renderer& renderer, const char* key, float x, float y, float scale = 1.0F, Color color = {},
             Align align = Align::center) const;
    void Toast(Renderer& renderer, const char* key, float alpha) const;
    // Small "OGPlay · demo" tag whose left edge sits at `x`, vertically centered on `y`.
    void Brand(Renderer& renderer, float x, float y) const;

    Audio* audio_ = nullptr;
    std::string save_path_;
    std::mt19937 random_{std::random_device{}()};
    Screen screen_ = Screen::splash;
    float time_ = 0;
    bool english_ = false;

    // Progress
    std::array<int, 12> stars_{};
    std::array<int, 12> best_{};
    int unlocked_ = 1;

    // Current level
    int level_ = 0;
    int moves_ = 0;
    int score_ = 0;
    float shown_score_ = 0;
    int cascade_ = 0;
    Phase phase_ = Phase::idle;
    float phase_time_ = 0;
    float idle_time_ = 0;
    Board board_;
    std::array<std::array<Cell, kBoardSize>, kBoardSize> cells_{};
    Pos swap_a_, swap_b_;
    bool selected_ = false;
    Pos selection_;
    bool dragging_ = false;
    float drag_x_ = 0, drag_y_ = 0;
    bool hint_ = false;
    Pos hint_a_, hint_b_;
    int result_stars_ = 0;
    int stars_played_ = 0;
    const char* tip_ = nullptr;  // level-start tutorial line, hidden after the first swap
    float tip_time_ = 0;
    float shuffle_time_ = 0;
    bool won_ = false;
    std::vector<Vanish> vanishing_;
    std::vector<Particle> particles_;
    std::vector<Popup> popups_;
    std::vector<Blast> blasts_;

    Action pressed_ = Action::none;
};

}  // namespace sweetshop
