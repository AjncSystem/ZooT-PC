// SPDX-License-Identifier: GPL-3.0-or-later
// ZooT-PC: loading screen shown while the shaders are extracted and precached before the game
// starts (eden-cli has no Qt loading screen). It covers the game window's client area and is
// owned by it: it follows the game window and never sits above other programs.

#pragma once

#include <chrono>
#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <thread>

struct SDL_Renderer;
struct SDL_Texture;
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

    /// Any thread may report; only the thread that made the screen draws (at most every 33 ms)
    /// and keeps the windows responsive. Esc asks the precache to skip. `stage` is a key of the
    /// screen's own texts (see the .cpp), shown in the launcher's language.
    void Update(const std::string& stage, std::size_t done, std::size_t total);

private:
    struct Text {
        SDL_Texture* texture{};
        float w{}, h{};
    };

    void Draw();
    void FollowGameWindow();
    void LoadBackground();
    Text MakeText(const std::string& utf8, int px, bool semibold, bool title);
    const Text& CachedText(const std::string& utf8, int px, bool semibold, bool title);
    void PutText(const Text& text, float x, float y, unsigned rgb, float alpha = 1.0f);

    SDL_Window* window{};
    SDL_Window* game{};
    SDL_Renderer* renderer{};
    SDL_Texture* background{};  // the player's cover, small: drawn stretched it is soft
    SDL_Texture* shield{};
    float scale{1.0f};
    const std::thread::id owner;
    const std::chrono::steady_clock::time_point begin;
    std::chrono::steady_clock::time_point last_draw{};
    std::map<std::string, Text> text_cache;

    std::mutex mutex;
    std::string stage;
    std::size_t done{};
    std::size_t total{};
    std::chrono::steady_clock::time_point stage_begin;
    std::size_t stage_first_done{};
    bool skip_sent{};
};

} // namespace ZooT
