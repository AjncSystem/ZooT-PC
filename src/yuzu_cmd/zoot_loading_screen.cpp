// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>
#include <fmt/format.h>

#include <filesystem>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "video_core/shader_warm/shader_warm_builder.h"
#include "yuzu_cmd/emu_window/emu_window_sdl3.h"
#include "yuzu_cmd/zoot_loading_screen.h"

namespace ZooT {
namespace {

constexpr int Width = 760;
constexpr int Height = 250;
constexpr float Char = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;

// Forest green and old gold.
constexpr SDL_Color Background{12, 28, 18, 255};
constexpr SDL_Color Panel{20, 44, 28, 255};
constexpr SDL_Color Gold{214, 172, 58, 255};
constexpr SDL_Color DarkGold{120, 92, 28, 255};
constexpr SDL_Color Parchment{236, 226, 196, 255};
constexpr SDL_Color Leaf{70, 150, 80, 255};

void SetColor(SDL_Renderer* renderer, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

/// Text centred on x, in pixels of the unscaled window.
void CenteredText(SDL_Renderer* renderer, const std::string& text, float y, float scale,
                  SDL_Color color) {
    SDL_SetRenderScale(renderer, scale, scale);
    SetColor(renderer, color);
    const float width = static_cast<float>(text.size()) * Char * scale;
    SDL_RenderDebugText(renderer, (Width - width) / 2.0f / scale, y / scale, text.c_str());
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}

std::string FormatSeconds(double seconds) {
    const int total = static_cast<int>(seconds + 0.5);
    return total >= 60 ? fmt::format("{}m{:02}s", total / 60, total % 60)
                       : fmt::format("{}s", total);
}

} // Anonymous namespace

void ApplyPlayerIcon(SDL_Window* window) {
    const auto path = Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir) / "zoot" / "icon.png";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return;
    }
    SDL_Surface* const icon = SDL_LoadPNG(Common::FS::PathToUTF8String(path).c_str());
    if (!icon) {
        LOG_WARNING(Frontend, "ZooT icon {}: {}", Common::FS::PathToUTF8String(path),
                    SDL_GetError());
        return;
    }
    if (window) {
        SDL_SetWindowIcon(window, icon);
    } else {
        int count = 0;
        if (SDL_Window** const windows = SDL_GetWindows(&count)) {
            for (int i = 0; i < count; ++i) {
                SDL_SetWindowIcon(windows[i], icon);
            }
            SDL_free(windows);
        }
    }
    SDL_DestroySurface(icon);
}

LoadingScreen::LoadingScreen()
    : owner{std::this_thread::get_id()}, begin{std::chrono::steady_clock::now()},
      stage_begin{begin} {
    if (!SDL_CreateWindowAndRenderer(ZootBaseTitle().c_str(), Width, Height,
                                     SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP, &window,
                                     &renderer)) {
        LOG_WARNING(Frontend, "ZooT loading screen: {}", SDL_GetError());
        window = nullptr;
        renderer = nullptr;
        return;
    }
    ApplyPlayerIcon(window);
    stage = "Preparando";
    Draw();
}

LoadingScreen::~LoadingScreen() {
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
}

void LoadingScreen::Update(const std::string& new_stage, std::size_t new_done,
                           std::size_t new_total) {
    {
        std::scoped_lock lock{mutex};
        if (new_stage != stage) {
            stage = new_stage;
            stage_begin = std::chrono::steady_clock::now();
            stage_first_done = new_done;
        }
        done = new_done;
        total = new_total;
    }
    if (std::this_thread::get_id() != owner) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - last_draw < std::chrono::milliseconds{50}) {
        return;
    }
    Draw();
}

void LoadingScreen::Draw() {
    last_draw = std::chrono::steady_clock::now();
    // Keeps both windows responsive while the main thread loads.
    SDL_PumpEvents();
    if (const bool* keys = SDL_GetKeyboardState(nullptr);
        keys && keys[SDL_SCANCODE_ESCAPE] && !skip_sent) {
        skip_sent = true;
        VideoCommon::ShaderWarm::RequestSkipWarm();
        LOG_INFO(Frontend, "ZooT loading screen: shader precache skipped (Esc)");
    }
    if (!renderer) {
        return;
    }

    std::string current;
    std::size_t current_done;
    std::size_t current_total;
    double stage_seconds;
    std::size_t first_done;
    {
        std::scoped_lock lock{mutex};
        current = stage;
        current_done = done;
        current_total = total;
        first_done = stage_first_done;
        stage_seconds = std::chrono::duration<double>(last_draw - stage_begin).count();
    }
    const double elapsed = std::chrono::duration<double>(last_draw - begin).count();

    SetColor(renderer, Background);
    SDL_RenderClear(renderer);
    // Double gold frame.
    SetColor(renderer, Gold);
    SDL_FRect outer{3, 3, Width - 6, Height - 6};
    SDL_RenderRect(renderer, &outer);
    SetColor(renderer, DarkGold);
    SDL_FRect inner{8, 8, Width - 16, Height - 16};
    SDL_RenderRect(renderer, &inner);
    SetColor(renderer, Panel);
    SDL_FRect panel{9, 9, Width - 18, Height - 18};
    SDL_RenderFillRect(renderer, &panel);

    CenteredText(renderer, ZootBaseTitle(), 24, 4.0f, Gold);
    CenteredText(renderer, current, 82, 2.0f, Parchment);

    // Progress bar.
    const float bar_x = 50;
    const float bar_y = 120;
    const float bar_w = Width - 100;
    const float bar_h = 26;
    const double fraction =
        current_total ? std::clamp(static_cast<double>(current_done) / current_total, 0.0, 1.0)
                      : 0.0;
    SetColor(renderer, DarkGold);
    SDL_FRect frame{bar_x - 3, bar_y - 3, bar_w + 6, bar_h + 6};
    SDL_RenderRect(renderer, &frame);
    SetColor(renderer, Background);
    SDL_FRect track{bar_x, bar_y, bar_w, bar_h};
    SDL_RenderFillRect(renderer, &track);
    if (current_total) {
        SetColor(renderer, Leaf);
        SDL_FRect fill{bar_x, bar_y, static_cast<float>(bar_w * fraction), bar_h};
        SDL_RenderFillRect(renderer, &fill);
        SetColor(renderer, Gold);
        SDL_FRect edge{bar_x, bar_y, static_cast<float>(bar_w * fraction), 3};
        SDL_RenderFillRect(renderer, &edge);
    } else {
        // Unknown total: a gold block sweeping across.
        const float block = bar_w / 6;
        const float x = static_cast<float>(std::fmod(elapsed * 0.6, 1.0)) * (bar_w - block);
        SetColor(renderer, Leaf);
        SDL_FRect sweep{bar_x + x, bar_y, block, bar_h};
        SDL_RenderFillRect(renderer, &sweep);
    }

    std::string detail;
    if (current_total) {
        detail = fmt::format("{} / {}  ({:.0f}%)", current_done, current_total, fraction * 100);
        const std::size_t stage_done = current_done - std::min(current_done, first_done);
        if (stage_seconds > 2.0 && stage_done > 0 && current_done < current_total) {
            const double rate = stage_done / stage_seconds;
            detail += fmt::format("   faltam ~{}", FormatSeconds((current_total - current_done) / rate));
        }
    }
    CenteredText(renderer, detail, 166, 2.0f, Parchment);
    CenteredText(renderer,
                 fmt::format("Tempo: {}    Esc = pular a pre-compilacao", FormatSeconds(elapsed)),
                 214, 1.0f, Gold);

    SDL_RenderPresent(renderer);
}

} // namespace ZooT
