// SPDX-License-Identifier: GPL-3.0-or-later
// ZooT-PC: loading screen shown while the shaders are extracted and precached before the game
// starts (eden-cli has no Qt loading screen).

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>

struct SDL_Renderer;
struct SDL_Window;

namespace ZooT {

/// The player's own window icon, <user dir>/zoot/icon.png, when it exists (nothing is shipped:
/// the build keeps its own icon). Applied to `window`, or to every window when null.
void ApplyPlayerIcon(SDL_Window* window = nullptr);

class LoadingScreen {
public:
    LoadingScreen();
    ~LoadingScreen();

    LoadingScreen(const LoadingScreen&) = delete;
    LoadingScreen& operator=(const LoadingScreen&) = delete;

    /// Any thread may report; only the thread that made the screen draws (at most every 50 ms)
    /// and keeps the windows responsive. Esc asks the precache to skip.
    void Update(const std::string& stage, std::size_t done, std::size_t total);

private:
    void Draw();

    SDL_Window* window{};
    SDL_Renderer* renderer{};
    const std::thread::id owner;
    const std::chrono::steady_clock::time_point begin;
    std::chrono::steady_clock::time_point last_draw{};

    std::mutex mutex;
    std::string stage;
    std::size_t done{};
    std::size_t total{};
    std::chrono::steady_clock::time_point stage_begin;
    std::size_t stage_first_done{};
    bool skip_sent{};
};

} // namespace ZooT
