// SPDX-License-Identifier: GPL-3.0-or-later
// ZooT-PC in-game hotkeys and menu:
//   F1  the ZooT menu: video options applied at once (FPS mode, fullscreen, scaling filter, FSR
//       sharpening, anti-aliasing, aspect ratio, VSync, console mode, volume) and the resolution
//       for the next start. Arrows choose and change, F1 or Esc closes.
//   F7  FPS mode: 60 -> 30 -> unlimited -> 60.
//   F11 fullscreen.
// The menu and the short notices are small windows owned by the game window (opaque panels: a
// see-through window over the game's Vulkan output is not reliable on Windows). Every change is
// also written to the launcher's settings (user/zoot/launcher.json), so the launcher and the next
// start keep it.

#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;

namespace Core {
class System;
}

namespace ZooT {

class Overlay {
public:
    Overlay(Core::System& system, SDL_Window* game, std::function<void()> relayout);
    ~Overlay();

    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    /// A key from the game window. True when the overlay used it (the game must not see it).
    bool HandleKey(int scancode, bool down);

    /// Called by the main loop: redraws, follows the game window, hides an old notice.
    void Tick();

private:
    struct Text {
        SDL_Texture* texture{};
        float w{}, h{};
    };
    struct Panel {
        SDL_Window* window{};
        SDL_Renderer* renderer{};
        std::map<std::string, Text> texts;
    };
    struct Item {
        std::string label;
        std::vector<std::pair<std::string, int>> choices;
        std::function<int()> get;
        std::function<void(int)> set;
        bool next_start{};
    };

    void BuildItems();
    void OpenMenu(bool open);
    void Notice(const std::string& text);
    void CycleFps();
    void SetFullscreen(bool on);
    void Step(int item, int direction);
    void SaveToLauncher();
    bool MakePanel(Panel& panel, int w, int h);
    void FreePanel(Panel& panel);
    const Text& GetText(Panel& panel, const std::string& utf8, int px, bool semibold,
                        bool title);
    void Put(Panel& panel, const Text& text, float x, float y, unsigned rgb);
    void DrawMenu();
    void DrawNotice();
    void PlaceOver(SDL_Window* window, int w, int h, bool top);

    Core::System& system;
    SDL_Window* game{};
    std::function<void()> relayout;
    std::vector<Item> items;
    int selected{};
    bool menu_open{};
    Panel menu;
    Panel notice;
    std::string notice_text;
    std::chrono::steady_clock::time_point notice_until{};
    SDL_Texture* shield{};
    bool dirty{true};
};

} // namespace ZooT
