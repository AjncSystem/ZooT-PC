// SPDX-License-Identifier: GPL-3.0-or-later
// ZooT-PC in-game hotkeys and menu:
//   F1  the ZooT menu, laid out like Hookshot's in gold: over the whole game window, tabs
//       (Display, Performance, Audio, Controls; Q/E or Tab switch), rows with their value
//       between arrows (the chosen one in a gold box), and a help panel for the chosen row.
//       Arrows choose and change, F1 or Esc closes.
//   F7  FPS mode: 60 -> 30 -> unlimited -> 60.
//   F8  frame generation (Lichii's built-in optical flow, 2x) on and off.
//   F10 performance panel at the top (left, center or right): FPS (game frames plus generated
//       ones), frame time, the game's CPU and GPU use, video memory and RAM, the PC's RAM in
//       use, and a frame time graph of the last 30 s.
//   F11 fullscreen.
// The menu and the short notices are small windows owned by the game window (opaque panels: a
// see-through window over the game's Vulkan output is not reliable on Windows). Every change is
// also written to the launcher's settings (user/zoot/launcher.json), so the launcher and the next
// start keep it.

#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>

union SDL_Event;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;

namespace Core {
class System;
}

namespace ZooT {

class Overlay {
public:
    /// `stats` gives the FPS and frame time in ms of the last half second.
    Overlay(Core::System& system, SDL_Window* game, std::function<void()> relayout,
            std::function<std::pair<double, double>()> stats);
    ~Overlay();

    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    /// A key from the game window. True when the overlay used it (the game must not see it).
    bool HandleKey(int scancode, bool down);

    /// Called by the main loop: redraws, follows the game window, hides an old notice.
    void Tick();

    /// A mouse event; true when it was over the open menu (clicks, wheel, pointing), which
    /// the game must not see.
    bool HandleMouse(const SDL_Event& event);

    bool MenuOpen() const {
        return menu_open;
    }

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
        int tab{};         ///< MenuTab
        std::string help;  ///< shown beside the rows while this one is chosen
    };

    void BuildItems();
    void OpenMenu(bool open);
    void Notice(const std::string& text);
    void CycleFps();
    void ToggleFrameGen();
    void TogglePerf();
    void DrawPerf();
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
    void PlaceCorner(SDL_Window* window);

    Core::System& system;
    SDL_Window* game{};
    std::function<void()> relayout;
    std::function<std::pair<double, double>()> stats;
    Panel perf;
    bool perf_open{};
    std::array<float, 60> perf_ms{};  // frame times, one per half second
    std::size_t perf_count{};
    std::chrono::steady_clock::time_point perf_next{};
    std::chrono::steady_clock::time_point perf_last{};
    unsigned long long perf_generated{};  // frame generation's count at the last sample
    double perf_generated_fps{};
    bool perf_fg_running{};

    int perf_position{};  // 0 top left, 1 top center, 2 top right
    std::vector<Item> items;
    int selected{};  ///< row within the current tab
    int tab{};       ///< current tab
    std::array<int, 4> tab_selected{};
    std::vector<int> TabRows() const;
    /// Where the menu's tabs, rows and values were drawn (render pixels), for the mouse.
    enum class HitKind { Tab, Row, Less, More, Choice };
    struct Hit {
        float x, y, w, h;
        HitKind kind;
        int a;  ///< tab, or row within the tab
        int b;  ///< choice index (Choice)
    };
    std::vector<Hit> hits;
    void SetChoice(int item, int choice);
    bool menu_open{};
    Panel menu;
    Panel notice;
    std::string notice_text;
    std::chrono::steady_clock::time_point notice_until{};
    SDL_Texture* shield{};
    bool dirty{true};
};

} // namespace ZooT
