#include "game.h"

#include <android/log.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sweetshop {
namespace {

constexpr float kCell = 76.0F;
constexpr float kBoardX = 80.0F;
constexpr float kBoardY = 56.0F;
constexpr float kSwapTime = 0.16F;
constexpr float kClearTime = 0.22F;
constexpr float kHintDelay = 5.0F;
constexpr float kPi = 3.14159265F;

// Targets sit near the 10th-25th percentile of a random player (host soak test), so
// one star is easy while three stars at 1.7x need specials and cascades.
constexpr Level kLevels[12] = {
    {20, 2500, 5}, {20, 3000, 5}, {18, 3200, 5}, {18, 3800, 5}, {16, 3800, 5}, {16, 4400, 5},
    {22, 2800, 6}, {22, 3200, 6}, {20, 3200, 6}, {20, 3600, 6}, {18, 3400, 6}, {18, 3800, 6},
};
constexpr float kTwoStars = 1.35F;
constexpr float kThreeStars = 1.7F;

const char* const kCandySprites[7] = {"candy_red", "candy_orange", "candy_yellow", "candy_green",
                                      "candy_blue", "candy_purple", "candy_rainbow"};
const char* const kStripeSprites[6] = {"stripes_red", "stripes_orange", "stripes_yellow",
                                       "stripes_green", "stripes_blue", "stripes_purple"};
constexpr Color kCandyColors[7] = {Hex(0xE85D5D), Hex(0xF39A3A), Hex(0xF7CD46), Hex(0x7CCB9E),
                                   Hex(0x6EC1EA), Hex(0xB98AE0), Hex(0xFFF4E0)};
constexpr Color kTileColors[6] = {Hex(0xF7A8C0), Hex(0xFFD07A), Hex(0xA8E0C0),
                                  Hex(0xA8D4F5), Hex(0xD4B8F0), Hex(0xFFB8A0)};
constexpr Color kDimStar{0.35F, 0.28F, 0.26F, 0.35F};
constexpr Color kInk = Hex(0x7A4E3A);  // plain labels on the cream panels

// Level-start tutorial lines; levels without one start silently.
const char* LevelTip(const int level) {
    switch (level) {
    case 0: return "tip_swap";
    case 1: return "tip_line";
    case 2: return "tip_bomb";
    case 3: return "tip_rainbow";
    case 6: return "tip_grape";
    default: return nullptr;
    }
}

float LevelX(const int level) { return 335.0F + static_cast<float>(level % 4) * 203.0F; }
float LevelY(const int level) { return 232.0F + static_cast<float>(level / 4) * 172.0F; }

float Approach(const float value, const float step) {
    if (std::abs(value) <= step) return 0.0F;
    return value > 0 ? value - step : value + step;
}

float Frand(std::mt19937& random, const float low, const float high) {
    return std::uniform_real_distribution<float>(low, high)(random);
}

}  // namespace

bool Game::Button::Hit(const float px, const float py) const {
    return std::hypot(px - x, py - y) <= size * 0.5F;
}

float Game::CellX(const int x) { return kBoardX + (static_cast<float>(x) + 0.5F) * kCell; }
float Game::CellY(const int y) { return kBoardY + (static_cast<float>(y) + 0.5F) * kCell; }

Pos Game::CellAt(const float x, const float y) const {
    return {static_cast<int>(std::floor((x - kBoardX) / kCell)), static_cast<int>(std::floor((y - kBoardY) / kCell))};
}

int Game::Stars(const int score) const {
    const auto target = static_cast<float>(kLevels[level_].target);
    const auto value = static_cast<float>(score);
    if (value >= target * kThreeStars) return 3;
    if (value >= target * kTwoStars) return 2;
    return value >= target ? 1 : 0;
}

void Game::Init(Audio* audio, std::string save_path, const bool english) {
    audio_ = audio;
    save_path_ = std::move(save_path);
    english_ = english;
    Load();
}

void Game::Say(Renderer& renderer, const char* key, const float x, const float y, const float scale, const Color color,
               const Align align) const {
    renderer.Label(std::string("text_") + key + (english_ ? "_en" : "_zh"), x, y, scale, color, align);
}

void Game::Toast(Renderer& renderer, const char* key, const float alpha) const {
    const std::string name = std::string("text_") + key + (english_ ? "_en" : "_zh");
    const float width = std::min(renderer.LabelWidth(name) + 56, kCell * kBoardSize - 16);
    const float scale = std::min(1.0F, (width - 56) / renderer.LabelWidth(name));
    const float cx = kBoardX + kCell * kBoardSize / 2, cy = kBoardY + kCell * kBoardSize / 2;
    renderer.NineSlice(renderer.Get("tile"), cx - width / 2, cy - 38, width, 76, 30, Hex(0x3A2418, 0.78F * alpha));
    renderer.Label(name, cx, cy, scale, WithAlpha({}, alpha));
}

void Game::Load() {
    FILE* file = save_path_.empty() ? nullptr : std::fopen(save_path_.c_str(), "r");
    if (file == nullptr) return;
    char line[128];
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        int a = 0, b = 0, c = 0;
        if (std::sscanf(line, "unlocked %d", &a) == 1) unlocked_ = std::clamp(a, 1, 12);
        else if (std::sscanf(line, "sound %d", &a) == 1) audio_->SetEnabled(a != 0);
        else if (std::strncmp(line, "language ", 9) == 0) english_ = std::strncmp(line + 9, "en", 2) == 0;
        else if (std::sscanf(line, "level %d %d %d", &a, &b, &c) == 3 && a >= 0 && a < 12) {
            stars_[a] = std::clamp(b, 0, 3);
            best_[a] = std::max(0, c);
        }
    }
    std::fclose(file);
}

void Game::Save() const {
    if (save_path_.empty()) return;
    const std::string temporary = save_path_ + ".tmp";
    FILE* file = std::fopen(temporary.c_str(), "w");
    if (file == nullptr) {
        __android_log_print(ANDROID_LOG_WARN, "sweetshop", "cannot write %s", temporary.c_str());
        return;
    }
    std::fprintf(file, "sweetshop 1\nunlocked %d\nsound %d\nlanguage %s\n", unlocked_, audio_->Enabled() ? 1 : 0,
                 english_ ? "en" : "zh");
    for (int level = 0; level < 12; ++level) std::fprintf(file, "level %d %d %d\n", level, stars_[level], best_[level]);
    const bool written = std::fflush(file) == 0;
    std::fclose(file);
    if (!written || std::rename(temporary.c_str(), save_path_.c_str()) != 0)
        __android_log_print(ANDROID_LOG_WARN, "sweetshop", "cannot replace %s", save_path_.c_str());
}

std::vector<Game::Button> Game::Buttons() const {
    switch (screen_) {
    case Screen::splash:
        return {};
    case Screen::title:
        return {{Action::play, "icon_play", 960, 440, 170},
                {Action::language, "language", 1122, 652, 76},
                {Action::sound, audio_->Enabled() ? "icon_sound_on" : "icon_sound_off", 1212, 652, 76}};
    case Screen::levels:
        return {{Action::home, "icon_home", 80, 72, 84},
                {Action::sound, audio_->Enabled() ? "icon_sound_on" : "icon_sound_off", 1200, 72, 84}};
    case Screen::play:
        if (phase_ == Phase::result) {
            if (phase_time_ < 0.6F) return {};
            if (won_ && level_ + 1 < 12)
                return {{Action::home, "icon_home", 530, 520, 84},
                        {Action::retry, "icon_retry", 640, 520, 84},
                        {Action::next, "icon_play", 750, 520, 84}};
            return {{Action::home, "icon_home", 585, 520, 84}, {Action::retry, "icon_retry", 695, 520, 84}};
        }
        return {{Action::home, "icon_home", 846, 592, 88},
                {Action::retry, "icon_retry", 976, 592, 88},
                {Action::sound, audio_->Enabled() ? "icon_sound_on" : "icon_sound_off", 1106, 592, 88}};
    }
    return {};
}

void Game::Activate(const Action action) {
    switch (action) {
    case Action::none:
        break;
    case Action::play:
        screen_ = Screen::levels;
        break;
    case Action::sound:
        audio_->SetEnabled(!audio_->Enabled());
        Save();
        break;
    case Action::home:
        screen_ = screen_ == Screen::play ? Screen::levels : Screen::title;
        break;
    case Action::retry:
        StartLevel(level_);
        break;
    case Action::next:
        StartLevel(std::min(level_ + 1, 11));
        break;
    case Action::language:
        english_ = !english_;
        Save();
        break;
    }
}

void Game::StartLevel(const int level) {
    level_ = level;
    moves_ = kLevels[level].moves;
    score_ = 0;
    shown_score_ = 0;
    cascade_ = 0;
    board_.Fill(kLevels[level].colors, random_);
    for (int y = 0; y < kBoardSize; ++y)
        for (int x = 0; x < kBoardSize; ++x) cells_[y][x] = {0, -static_cast<float>(kBoardSize + 1 - y) * 0.6F - 2.0F, 0, 1};
    phase_ = Phase::falling;
    phase_time_ = 0;
    idle_time_ = 0;
    selected_ = dragging_ = hint_ = false;
    vanishing_.clear();
    particles_.clear();
    popups_.clear();
    blasts_.clear();
    tip_ = LevelTip(level);
    tip_time_ = 0;
    shuffle_time_ = 0;
    screen_ = Screen::play;
}

bool Game::Back() {
    pressed_ = Action::none;
    switch (screen_) {
    case Screen::splash:
        screen_ = Screen::title;
        return true;
    case Screen::title:
        return false;
    case Screen::levels:
        screen_ = Screen::title;
        return true;
    case Screen::play:
        screen_ = Screen::levels;
        return true;
    }
    return true;
}

void Game::PointerDown(const float x, const float y) {
    pressed_ = Action::none;
    if (screen_ == Screen::splash) {
        screen_ = Screen::title;
        return;
    }
    for (const Button& button : Buttons())
        if (button.Hit(x, y)) {
            // Sound on touch-down so the tap feels immediate; the action still waits for release.
            pressed_ = button.action;
            audio_->Play(Sfx::click);
            return;
        }
    if (screen_ == Screen::levels) {
        for (int level = 0; level < 12; ++level) {
            if (std::abs(x - LevelX(level)) < 75 && std::abs(y - LevelY(level)) < 75) {
                if (level < unlocked_) {
                    audio_->Play(Sfx::click);
                    StartLevel(level);
                } else {
                    audio_->Play(Sfx::invalid);
                }
                return;
            }
        }
        return;
    }
    if (screen_ != Screen::play || phase_ != Phase::idle) return;
    const Pos cell = CellAt(x, y);
    if (!Board::Inside(cell)) {
        selected_ = false;
        return;
    }
    if (selected_ && Board::Adjacent(selection_, cell)) {
        TrySwap(selection_, cell);
        return;
    }
    if (selected_ && selection_ == cell) {
        selected_ = false;
        return;
    }
    selected_ = true;
    selection_ = cell;
    dragging_ = true;
    drag_x_ = x;
    drag_y_ = y;
    audio_->Play(Sfx::select);
}

void Game::PointerMove(const float x, const float y) {
    if (!dragging_ || !selected_ || phase_ != Phase::idle) return;
    const float dx = x - drag_x_, dy = y - drag_y_;
    if (std::max(std::abs(dx), std::abs(dy)) < kCell * 0.45F) return;
    const Pos target = std::abs(dx) > std::abs(dy) ? Pos{selection_.x + (dx > 0 ? 1 : -1), selection_.y}
                                                   : Pos{selection_.x, selection_.y + (dy > 0 ? 1 : -1)};
    dragging_ = false;
    if (Board::Inside(target)) TrySwap(selection_, target);
}

void Game::PointerUp(const float x, const float y) {
    dragging_ = false;
    const Action pressed = pressed_;
    pressed_ = Action::none;
    if (pressed == Action::none) return;
    for (const Button& button : Buttons())
        if (button.action == pressed && button.Hit(x, y)) {
            Activate(pressed);
            return;
        }
}

void Game::TrySwap(const Pos a, const Pos b) {
    if (tip_ != nullptr) tip_time_ = std::max(tip_time_, 4.0F);  // start fading once the player acts
    selected_ = false;
    dragging_ = false;
    hint_ = false;
    idle_time_ = 0;
    board_.Swap(a, b);
    cells_[a.y][a.x].dx = static_cast<float>(b.x - a.x);
    cells_[a.y][a.x].dy = static_cast<float>(b.y - a.y);
    cells_[b.y][b.x].dx = static_cast<float>(a.x - b.x);
    cells_[b.y][b.x].dy = static_cast<float>(a.y - b.y);
    swap_a_ = a;
    swap_b_ = b;
    phase_ = Phase::swapping;
    phase_time_ = 0;
    audio_->Play(Sfx::swap);
}

void Game::Resolve(Resolution resolution) {
    score_ += resolution.score;
    float sum_x = 0, sum_y = 0;
    for (std::size_t index = 0; index < resolution.cleared.size(); ++index) {
        const Pos pos = resolution.cleared[index];
        const Piece piece = resolution.pieces[index];
        vanishing_.push_back({pos, piece, 0});
        Burst(CellX(pos.x), CellY(pos.y), kCandyColors[std::clamp(piece.color, 0, 6)], 5, 260);
        sum_x += CellX(pos.x);
        sum_y += CellY(pos.y);
    }
    if (!resolution.cleared.empty()) {
        const auto count = static_cast<float>(resolution.cleared.size());
        popups_.push_back({sum_x / count, sum_y / count, resolution.score, 0});
    }
    for (const Creation& creation : resolution.created) cells_[creation.at.y][creation.at.x].pop = 0;
    audio_->Play(Sfx::match, cascade_ - 1);
    if (!resolution.created.empty()) audio_->Play(Sfx::create);
    for (const Effect& effect : resolution.effects) {
        blasts_.push_back({effect, 0});
        audio_->Play(effect.kind == Special::bomb ? Sfx::bomb : effect.kind == Special::rainbow ? Sfx::rainbow : Sfx::line);
    }
    board_.Apply(resolution);
    phase_ = Phase::clearing;
    phase_time_ = 0;
}

void Game::Finish() {
    won_ = score_ >= kLevels[level_].target;
    result_stars_ = Stars(score_);
    stars_[level_] = std::max(stars_[level_], result_stars_);
    best_[level_] = std::max(best_[level_], score_);
    if (won_) unlocked_ = std::max(unlocked_, std::min(12, level_ + 2));
    Save();
    audio_->Play(won_ ? Sfx::win : Sfx::lose);
    phase_ = Phase::result;
    phase_time_ = 0;
    stars_played_ = 0;
}

void Game::Burst(const float x, const float y, const Color color, const int count, const float speed) {
    for (int index = 0; index < count; ++index) {
        const float angle = Frand(random_, 0, 2 * kPi);
        const float velocity = speed * Frand(random_, 0.4F, 1.0F);
        const float life = Frand(random_, 0.45F, 0.8F);
        particles_.push_back({x, y, std::cos(angle) * velocity, std::sin(angle) * velocity - 120, life, life,
                              Frand(random_, 14, 30), Frand(random_, 0, kPi), Frand(random_, -6, 6), color,
                              index % 2 == 0});
    }
}

void Game::Update(const float dt) {
    time_ += dt;
    for (auto& particle : particles_) {
        particle.life -= dt;
        particle.vy += 700 * dt;
        particle.x += particle.vx * dt;
        particle.y += particle.vy * dt;
        particle.rotation += particle.spin * dt;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.life <= 0; }),
                     particles_.end());
    for (auto& popup : popups_) popup.time += dt;
    popups_.erase(std::remove_if(popups_.begin(), popups_.end(), [](const Popup& p) { return p.time > 1.0F; }),
                  popups_.end());
    for (auto& blast : blasts_) blast.time += dt;
    blasts_.erase(std::remove_if(blasts_.begin(), blasts_.end(), [](const Blast& b) { return b.time > 0.45F; }),
                  blasts_.end());
    for (auto& vanish : vanishing_) vanish.time += dt;
    vanishing_.erase(std::remove_if(vanishing_.begin(), vanishing_.end(), [](const Vanish& v) { return v.time > kClearTime; }),
                     vanishing_.end());
    if (screen_ == Screen::splash && time_ > 2.2F) screen_ = Screen::title;
    if (screen_ == Screen::play) UpdatePlay(dt);
}

void Game::UpdatePlay(const float dt) {
    phase_time_ += dt;
    tip_time_ += dt;
    shuffle_time_ = std::max(0.0F, shuffle_time_ - dt);
    shown_score_ = std::min(static_cast<float>(score_), shown_score_ + std::max(40.0F, (score_ - shown_score_) * 6) * dt);
    for (auto& row : cells_)
        for (Cell& cell : row) cell.pop = std::min(1.0F, cell.pop + dt * 5);

    switch (phase_) {
    case Phase::idle:
        idle_time_ += dt;
        if (!hint_ && idle_time_ > kHintDelay) hint_ = board_.FindMove(&hint_a_, &hint_b_);
        break;
    case Phase::swapping:
    case Phase::swap_back: {
        bool moving = false;
        for (const Pos pos : {swap_a_, swap_b_}) {
            Cell& cell = cells_[pos.y][pos.x];
            cell.dx = Approach(cell.dx, dt / kSwapTime);
            cell.dy = Approach(cell.dy, dt / kSwapTime);
            moving = moving || cell.dx != 0 || cell.dy != 0;
        }
        if (moving) break;
        if (phase_ == Phase::swap_back) {
            phase_ = Phase::idle;
            break;
        }
        cascade_ = 1;
        Resolution combo = board_.SwapCombo(swap_a_, swap_b_, random_);
        Resolution matches = combo.Empty() ? board_.FindMatches({swap_b_, swap_a_}, cascade_, random_) : Resolution{};
        if (combo.Empty() && matches.Empty()) {
            board_.Swap(swap_a_, swap_b_);
            cells_[swap_a_.y][swap_a_.x].dx = static_cast<float>(swap_b_.x - swap_a_.x);
            cells_[swap_a_.y][swap_a_.x].dy = static_cast<float>(swap_b_.y - swap_a_.y);
            cells_[swap_b_.y][swap_b_.x].dx = static_cast<float>(swap_a_.x - swap_b_.x);
            cells_[swap_b_.y][swap_b_.x].dy = static_cast<float>(swap_a_.y - swap_b_.y);
            phase_ = Phase::swap_back;
            audio_->Play(Sfx::invalid);
            break;
        }
        moves_ = std::max(0, moves_ - 1);
        Resolve(combo.Empty() ? std::move(matches) : std::move(combo));
        break;
    }
    case Phase::clearing:
        if (phase_time_ < kClearTime) break;
        {
            const auto falls = board_.Collapse(kLevels[level_].colors, random_);
            for (int y = 0; y < kBoardSize; ++y)
                for (int x = 0; x < kBoardSize; ++x)
                    if (falls[y][x] > 0) {
                        cells_[y][x].dy = -static_cast<float>(falls[y][x]);
                        cells_[y][x].velocity = 0;
                    }
        }
        phase_ = Phase::falling;
        phase_time_ = 0;
        break;
    case Phase::falling: {
        bool moving = false;
        for (auto& row : cells_)
            for (Cell& cell : row) {
                if (cell.dy >= 0) continue;
                cell.velocity += 38.0F * dt;
                cell.dy = std::min(0.0F, cell.dy + cell.velocity * dt);
                if (cell.dy == 0) cell.velocity = 0;
                moving = true;
            }
        if (moving) break;
        ++cascade_;
        Resolution matches = board_.FindMatches({}, cascade_, random_);
        if (!matches.Empty()) {
            Resolve(std::move(matches));
            break;
        }
        if (cascade_ > 1 && moves_ == 0) {
            Finish();
            break;
        }
        Pos a, b;
        if (!board_.FindMove(&a, &b)) {
            board_.Shuffle(kLevels[level_].colors, random_);
            for (auto& row : cells_)
                for (Cell& cell : row) cell.pop = 0;
            shuffle_time_ = 1.8F;
        }
        phase_ = Phase::idle;
        idle_time_ = 0;
        break;
    }
    case Phase::result:
        // Each earned star chimes as it pops in on the result panel.
        if (stars_played_ < result_stars_ && phase_time_ >= 0.35F + static_cast<float>(stars_played_) * 0.3F) {
            audio_->Play(Sfx::star, stars_played_);
            ++stars_played_;
        }
        break;
    }
}

void Game::Render(Renderer& renderer) {
    switch (screen_) {
    case Screen::splash:
        RenderSplash(renderer);
        break;
    case Screen::title:
        RenderTitle(renderer);
        break;
    case Screen::levels:
        RenderLevels(renderer);
        break;
    case Screen::play:
        RenderPlay(renderer);
        break;
    }
    for (const Particle& particle : particles_) {
        const float alpha = std::clamp(particle.life / particle.max_life, 0.0F, 1.0F);
        renderer.Draw(renderer.Get(particle.sparkle ? "sparkle" : "circle"), particle.x, particle.y, particle.size,
                      particle.size, WithAlpha(particle.color, alpha), particle.rotation, particle.sparkle);
    }
    for (const Button& button : Buttons()) {
        const float scale = button.action == pressed_ ? 0.9F : 1.0F;
        const float bob = button.action == Action::play && screen_ == Screen::title ? 1.0F + 0.04F * std::sin(time_ * 3) : 1.0F;
        const float size = button.size * scale * bob;
        if (button.action == Action::language) {
            renderer.Draw(renderer.Get("circle"), button.x, button.y, size, size, Hex(0xF48FB1));
            renderer.Draw(renderer.Get("ring"), button.x, button.y, size * 1.04F, size * 1.04F, Hex(0x3A2418));
            Say(renderer, "language", button.x, button.y, scale);
            continue;
        }
        renderer.Draw(renderer.Get(button.icon), button.x, button.y, size, size);
    }
}

void Game::RenderSplash(Renderer& renderer) {
    // OGPlay-branded boot card: the desktop app's navy with its blue and yellow glows.
    const float fade = std::min({1.0F, time_ * 3, std::max(0.0F, (2.2F - time_) * 3)});
    const Sprite& glow = renderer.Get("glow");
    renderer.Rect(0, 0, kWidth, kHeight, Hex(0x121826));
    renderer.Draw(glow, 900, 250, 1100, 900, Hex(0x2F86EB, 0.55F * fade), 0, true);
    renderer.Draw(glow, 380, 500, 1000, 800, Hex(0xFFC21A, 0.45F * fade), 0, true);
    const float pop = 0.9F + 0.1F * std::min(1.0F, time_ * 2.5F);
    renderer.Draw(renderer.Get("og_logo"), 640, 270, 180 * pop, 180 * pop, WithAlpha({}, fade));
    renderer.Label("og_wordmark", 640, 420, 1.15F, WithAlpha({}, fade));
    Say(renderer, "splash_line", 640, 500, 1.0F, Hex(0xC9D3E6, fade));
}

void Game::RenderTitle(Renderer& renderer) {
    renderer.DrawFull(Texture::background, 0, 0, kWidth, kHeight);
    const Sprite logo = renderer.TextureSprite(Texture::logo);
    const float scale = 560.0F / std::max(logo.width, logo.height);
    const float bob = std::sin(time_ * 1.6F) * 8;
    renderer.DrawFull(Texture::logo, 410 - logo.width * scale / 2, 400 - logo.height * scale / 2 + bob,
                      logo.width * scale, logo.height * scale);
    for (int index = 0; index < 5; ++index) {
        const float phase = time_ * 0.7F + static_cast<float>(index) * 1.3F;
        const float pulse = std::max(0.0F, std::sin(phase * 2.1F));
        renderer.Draw(renderer.Get("sparkle"), 410 + std::cos(phase) * 290, 400 + std::sin(phase * 1.3F) * 220,
                      40 * pulse, 40 * pulse, Hex(0xFFF6C8, pulse), phase, true);
    }

    // Game name and tagline above the play button.
    const Texture title = english_ ? Texture::title_en : Texture::title_zh;
    const Sprite title_size = renderer.TextureSprite(title);
    const float title_scale = std::min(560.0F / title_size.width, 190.0F / title_size.height);
    const float title_width = title_size.width * title_scale, title_height = title_size.height * title_scale;
    renderer.DrawFull(title, 960 - title_width / 2, 172 - title_height / 2 + std::sin(time_ * 2) * 4, title_width,
                      title_height);
    Say(renderer, "subtitle", 960, 300);
    Say(renderer, "start", 960, 545);

    Brand(renderer, 22, 670);

    int total = 0;
    for (const int stars : stars_) total += stars;
    renderer.Draw(renderer.Get("icon_star"), 860, 652, 56, 56);
    renderer.Text(std::to_string(total) + "/36", 894, 652, 44, {}, Align::left);
}

void Game::RenderLevels(Renderer& renderer) {
    renderer.DrawFull(Texture::background, 0, 0, kWidth, kHeight);
    renderer.Rect(0, 0, kWidth, kHeight, Hex(0x3A2418, 0.18F));
    renderer.Label("ribbon_pink", 640, 82, 0.9F);
    Say(renderer, "select_level", 640, 70);
    for (int level = 0; level < 12; ++level) {
        const float cx = LevelX(level), cy = LevelY(level);
        const bool open = level < unlocked_;
        renderer.NineSlice(renderer.Get("panel"), cx - 72, cy - 72, 144, 144, 32,
                           open ? kTileColors[level % 6] : Color{0.78F, 0.74F, 0.74F, 0.95F});
        if (!open) {
            renderer.Draw(renderer.Get("icon_lock"), cx, cy, 72, 72);
            continue;
        }
        renderer.Number(level + 1, cx, cy - 14, 62);
        for (int star = 0; star < 3; ++star)
            renderer.Draw(renderer.Get("icon_star"), cx - 34 + static_cast<float>(star) * 34, cy + 42, 32, 32,
                          star < stars_[level] ? Color{} : kDimStar);
    }
    Brand(renderer, 22, 670);
}

void Game::Brand(Renderer& renderer, const float x, const float y) const {
    // Warm translucent tag in the art's own brown so the mark sits in the scene
    // instead of on top of it; the wordmark keeps the desktop app's colors.
    // Every part is centered on `y`; the wordmark image is padded so that means its letters.
    constexpr float kHeight = 56, kLogo = 42, kPad = 14, kGap = 10, kDot = 5;
    constexpr float kMarkScale = 0.52F, kLabelScale = 1.1F;
    const std::string demo = english_ ? "text_demo_en" : "text_demo_zh";
    const float mark = renderer.LabelWidth("og_wordmark", kMarkScale);
    const float label = renderer.LabelWidth(demo, kLabelScale);
    const float width = kPad + kLogo + kGap + mark + kGap * 2 + kDot + label + kPad + 4;
    const Sprite& tile = renderer.Get("tile");
    renderer.NineSlice(tile, x - 2, y - kHeight / 2 - 2, width + 4, kHeight + 4, 30, Hex(0xFFF2E2, 0.45F));
    renderer.NineSlice(tile, x, y - kHeight / 2, width, kHeight, 28, Hex(0x3A2418, 0.62F));
    float cursor = x + kPad;
    renderer.Draw(renderer.Get("og_logo"), cursor + kLogo / 2, y, kLogo, kLogo);
    cursor += kLogo + kGap;
    renderer.Label("og_wordmark", cursor, y, kMarkScale, {}, Align::left);
    cursor += mark + kGap;
    renderer.Draw(renderer.Get("circle"), cursor + kDot / 2, y, kDot, kDot, Hex(0xF6E9D2, 0.7F));
    cursor += kDot + kGap;
    renderer.Label(demo, cursor, y, kLabelScale, Hex(0xF6E9D2), Align::left);
}

void Game::RenderPiece(Renderer& renderer, const Piece& piece, const float x, const float y, const float size,
                       const float alpha) const {
    if (piece.Empty()) return;
    const Color tint = WithAlpha({}, alpha);
    if (piece.special == Special::bomb) {
        const float pulse = 1.0F + 0.08F * std::sin(time_ * 6);
        renderer.Draw(renderer.Get("glow"), x, y, size * 1.5F * pulse, size * 1.5F * pulse,
                      WithAlpha(kCandyColors[piece.color], 0.9F * alpha), 0, true);
    }
    if (piece.special == Special::rainbow) {
        renderer.Draw(renderer.Get("glow"), x, y, size * 1.4F, size * 1.4F, Hex(0xFFF0C0, 0.7F * alpha), 0, true);
        renderer.Draw(renderer.Get(kCandySprites[kRainbowColor]), x, y, size, size, tint, std::sin(time_ * 2) * 0.15F);
        renderer.Draw(renderer.Get("sparkle"), x + size * 0.25F, y - size * 0.25F, size * 0.4F, size * 0.4F,
                      Hex(0xFFFFFF, alpha), time_ * 2, true);
        return;
    }
    if (piece.special == Special::line_h || piece.special == Special::line_v)
        renderer.Draw(renderer.Get("glow"), x, y, size * 1.3F, size * 1.3F, Hex(0xFFF6E0, 0.55F * alpha), 0, true);
    renderer.Draw(renderer.Get(kCandySprites[piece.color]), x, y, size, size, tint);
    if (piece.special == Special::line_h || piece.special == Special::line_v)
        renderer.Draw(renderer.Get(kStripeSprites[piece.color]), x, y, size, size, tint,
                      piece.special == Special::line_v ? kPi / 2 : 0);
    if (piece.special == Special::bomb)
        renderer.Draw(renderer.Get("ring"), x, y, size * 1.08F, size * 1.08F, Hex(0xFFFFFF, 0.85F * alpha));
}

void Game::RenderPlay(Renderer& renderer) {
    renderer.DrawFull(Texture::background, 0, 0, kWidth, kHeight);
    const Sprite& panel = renderer.Get("panel");
    renderer.NineSlice(panel, kBoardX - 16, kBoardY - 16, kCell * kBoardSize + 32, kCell * kBoardSize + 32, 32,
                       Color{1, 1, 1, 0.92F});
    const Sprite& tile = renderer.Get("tile");
    for (int y = 0; y < kBoardSize; ++y)
        for (int x = 0; x < kBoardSize; ++x)
            renderer.Draw(tile, CellX(x), CellY(y), kCell - 4, kCell - 4,
                          (x + y) % 2 == 0 ? Hex(0xFBE3D6) : Hex(0xF6D3C4));

    if (selected_ && phase_ == Phase::idle) {
        const float pulse = 1.0F + 0.06F * std::sin(time_ * 8);
        renderer.Draw(renderer.Get("glow"), CellX(selection_.x), CellY(selection_.y), kCell * 1.5F * pulse,
                      kCell * 1.5F * pulse, Hex(0xFFF6D0, 0.9F), 0, true);
    }
    for (int y = 0; y < kBoardSize; ++y)
        for (int x = 0; x < kBoardSize; ++x) {
            const Cell& cell = cells_[y][x];
            float px = CellX(x) + cell.dx * kCell, py = CellY(y) + cell.dy * kCell;
            float size = kCell * 0.92F * (0.4F + 0.6F * cell.pop);
            if (py < kBoardY - kCell * 0.4F) continue;  // still above the board while dropping in
            if (hint_ && phase_ == Phase::idle && ((hint_a_ == Pos{x, y}) || (hint_b_ == Pos{x, y}))) {
                const float wiggle = std::sin(time_ * 14) * std::max(0.0F, std::sin(time_ * 2.5F));
                px += wiggle * 4;
                size *= 1.0F + 0.05F * std::abs(wiggle);
            }
            RenderPiece(renderer, board_.At({x, y}), px, py, size, 1.0F);
        }
    for (const Vanish& vanish : vanishing_) {
        const float t = vanish.time / kClearTime;
        RenderPiece(renderer, vanish.piece, CellX(vanish.at.x), CellY(vanish.at.y), kCell * 0.92F * (1.0F + 0.3F * t),
                    1.0F - t);
    }
    const float board_width = kCell * kBoardSize;
    for (const Blast& blast : blasts_) {
        const float t = blast.time / 0.45F;
        const float fade = 1.0F - t;
        const Effect& effect = blast.effect;
        const Color color = WithAlpha(effect.color >= 0 ? kCandyColors[effect.color] : Hex(0xFFFFFF), fade);
        const float cx = CellX(effect.at.x), cy = CellY(effect.at.y);
        const Sprite& glow = renderer.Get("glow");
        switch (effect.kind) {
        case Special::line_h:
            renderer.Draw(glow, kBoardX + board_width / 2, cy, board_width * 1.3F, kCell * effect.reach * 1.2F * fade,
                          color, 0, true);
            break;
        case Special::line_v:
            renderer.Draw(glow, cx, kBoardY + board_width / 2, kCell * effect.reach * 1.2F * fade, board_width * 1.3F,
                          color, 0, true);
            break;
        case Special::bomb:
        case Special::rainbow: {
            const float radius = kCell * (1.5F + effect.reach) * (0.3F + t);
            renderer.Draw(renderer.Get("ring"), cx, cy, radius * 2, radius * 2, color);
            renderer.Draw(glow, cx, cy, radius * 2.4F, radius * 2.4F, WithAlpha(color, 0.8F), 0, true);
            break;
        }
        case Special::none:
            break;
        }
    }
    for (const Popup& popup : popups_) {
        const float alpha = std::clamp(1.4F - popup.time * 1.4F, 0.0F, 1.0F);
        renderer.Number(popup.value, popup.x, popup.y - popup.time * 60, 40, Hex(0xFFF2B0, alpha));
    }

    // Side panel: level, moves, score with star progress, target.
    renderer.NineSlice(panel, 736, 40, 480, 640, 32, Color{1, 1, 1, 0.94F});
    const Level& level = kLevels[level_];
    Say(renderer, "level", 812, 84, 1.0F, kInk);
    renderer.Draw(renderer.Get("circle"), 812, 140, 84, 84, Hex(0xE98AA6));
    renderer.Number(level_ + 1, 812, 140, 50);
    Say(renderer, "moves", 1040, 84, 1.0F, kInk);
    renderer.Draw(renderer.Get("icon_moves"), 968, 140, 70, 70);
    renderer.Number(moves_, 1012, 140, 60, moves_ <= 3 ? Hex(0xFF8A8A) : Color{}, Align::left);

    Say(renderer, "score", 976, 214, 1.0F, kInk);
    renderer.Number(static_cast<int>(shown_score_), 976, 266, 76, Hex(0xFFE59A));
    const float bar_x = 786, bar_y = 320, bar_width = 380, bar_height = 30;
    const float maximum = static_cast<float>(level.target) * kThreeStars;
    renderer.NineSlice(renderer.Get("tile"), bar_x, bar_y, bar_width, bar_height, 15, Hex(0xE6CFC4));
    const float fill = std::clamp(shown_score_ / maximum, 0.0F, 1.0F);
    if (fill > 0.02F)
        renderer.NineSlice(renderer.Get("tile"), bar_x, bar_y, std::max(bar_height, bar_width * fill), bar_height, 15,
                           Hex(0xF48FB1));
    const float thresholds[3] = {1.0F / kThreeStars, kTwoStars / kThreeStars, 1.0F};
    for (int star = 0; star < 3; ++star) {
        const bool earned = shown_score_ >= maximum * thresholds[star];
        renderer.Draw(renderer.Get("icon_star"), bar_x + bar_width * thresholds[star] - (star == 2 ? 14 : 0),
                      bar_y + bar_height / 2, 50, 50, earned ? Color{} : kDimStar);
    }
    Say(renderer, "target", 976, 398, 1.0F, kInk);
    renderer.Draw(renderer.Get("icon_target"), 880, 450, 70, 70);
    renderer.Number(level.target, 926, 450, 54, {}, Align::left);
    if (phase_ != Phase::result) {  // the result panel brings its own buttons
        Say(renderer, "home", 846, 650, 1.0F, kInk);
        Say(renderer, "retry", 976, 650, 1.0F, kInk);
        Say(renderer, "sound", 1106, 650, 1.0F, kInk);
    }

    if (tip_ != nullptr && tip_time_ < 5.0F && phase_ != Phase::result)
        Toast(renderer, tip_, std::clamp(std::min(tip_time_ * 3, (5.0F - tip_time_) * 2), 0.0F, 1.0F));
    if (shuffle_time_ > 0) Toast(renderer, "shuffle", std::min(1.0F, shuffle_time_ * 2));
    if (phase_ == Phase::result) RenderResult(renderer);
}

void Game::RenderResult(Renderer& renderer) {
    const float fade = std::min(1.0F, phase_time_ * 3);
    renderer.Rect(0, 0, kWidth, kHeight, Hex(0x1E120C, 0.5F * fade));
    const float rise = (1.0F - fade) * 40;
    renderer.NineSlice(renderer.Get("panel"), 390, 150 + rise, 500, 460, 36, Color{1, 1, 1, fade});
    renderer.Label(won_ ? "ribbon_gold" : "ribbon_lavender", 640, 160 + rise, 0.92F, WithAlpha({}, fade));
    Say(renderer, won_ ? "win" : "lose", 640, 148 + rise, 1.0F, WithAlpha({}, fade));
    const float positions[3][3] = {{540, 272, 92}, {640, 254, 116}, {740, 272, 92}};
    for (int star = 0; star < 3; ++star) {
        const float delay = 0.35F + static_cast<float>(star) * 0.3F;
        const bool earned = star < result_stars_;
        float scale = 1.0F;
        if (earned) {
            const float t = std::clamp((phase_time_ - delay) / 0.25F, 0.0F, 1.0F);
            scale = t <= 0 ? 0.0F : 1.0F + 0.35F * std::sin(t * kPi);
        }
        const float size = positions[star][2] * (earned ? scale : 1.0F);
        renderer.Draw(renderer.Get("icon_star"), positions[star][0], positions[star][1] + rise, size, size,
                      earned ? Color{} : WithAlpha(kDimStar, fade));
    }
    Say(renderer, "score", 640, 336 + rise, 1.0F, WithAlpha(kInk, fade));
    renderer.Number(score_, 640, 380 + rise, 60, Hex(0xFFE59A, fade));
    const std::string best = english_ ? "text_best_en" : "text_best_zh";
    const std::string value = std::to_string(best_[level_]);
    const float total = renderer.LabelWidth(best) + 10 + renderer.TextWidth(value.size(), 32);
    renderer.Label(best, 640 - total / 2, 432 + rise, 1.0F, WithAlpha(kInk, fade), Align::left);
    renderer.Text(value, 640 - total / 2 + renderer.LabelWidth(best) + 10, 432 + rise, 32, WithAlpha({}, fade),
                  Align::left);
    if (phase_time_ < 0.6F) return;
    for (const Button& button : Buttons()) {
        const char* key = button.action == Action::home ? "home" : button.action == Action::retry ? "retry" : "next";
        Say(renderer, key, button.x, button.y + 62, 1.0F, kInk);
    }
}

}  // namespace sweetshop
