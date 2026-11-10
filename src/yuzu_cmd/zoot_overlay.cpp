// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <SDL3/SDL.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/hle/service/am/applet_manager.h"
#include "yuzu_cmd/emu_window/emu_window_sdl3.h"
#include "yuzu_cmd/yuzu_icon.h"
#include "yuzu_cmd/zoot_font_cinzel.h"
#include "yuzu_cmd/zoot_font_inter.h"
#include "yuzu_cmd/zoot_font_inter_semi.h"
#include "yuzu_cmd/zoot_overlay.h"

namespace ZooT {
namespace {

constexpr int KeyF1 = SDL_SCANCODE_F1;
constexpr int KeyF7 = SDL_SCANCODE_F7;
constexpr int KeyF11 = SDL_SCANCODE_F11;

constexpr unsigned White = 0xF4F6FB;
constexpr unsigned Muted = 0x9AA6BD;
constexpr unsigned Gold = 0xE7C164;

// ---- texts, in the launcher's language (ZOOT_LANG) ---------------------------------------------
struct Phrase {
    const char* key;
    const char* pt;
    const char* en;
    const char* es;
    const char* fr;
    const char* ja;
    const char* ko;
    const char* ru;
};

constexpr Phrase Phrases[] = {
    {"menu", "Menu ZooT", "ZooT menu", "Menú ZooT", "Menu ZooT", "ZooT メニュー", "ZooT 메뉴",
     "Меню ZooT"},
    {"live", "As mudanças valem na hora", "Changes apply at once", "Los cambios se aplican al instante",
     "Les changements s’appliquent aussitôt", "変更はすぐに反映されます", "변경 사항이 바로 적용됩니다",
     "Изменения применяются сразу"},
    {"fps", "Modo de FPS", "FPS mode", "Modo de FPS", "Mode FPS", "FPS モード", "FPS 모드",
     "Режим FPS"},
    {"unlimited", "Ilimitado", "Unlimited", "Ilimitado", "Illimité", "無制限", "무제한",
     "Без ограничения"},
    {"fullscreen", "Tela cheia", "Fullscreen", "Pantalla completa", "Plein écran",
     "フルスクリーン", "전체 화면", "Полный экран"},
    {"window", "Janela", "Window", "Ventana", "Fenêtre", "ウィンドウ", "창 모드", "Окно"},
    {"on", "Ligado", "On", "Activado", "Activé", "オン", "켬", "Вкл."},
    {"off", "Desligado", "Off", "Apagado", "Désactivé", "オフ", "끔", "Выкл."},
    {"filter", "Filtro de escala", "Scaling filter", "Filtro de escalado",
     "Filtre de mise à l’échelle", "スケーリングフィルター", "스케일링 필터",
     "Фильтр масштабирования"},
    {"sharp", "Nitidez (FSR)", "Sharpening (FSR)", "Nitidez (FSR)", "Netteté (FSR)",
     "シャープネス (FSR)", "선명도 (FSR)", "Резкость (FSR)"},
    {"aa", "Anti-serrilhado", "Anti-aliasing", "Antialiasing", "Anticrénelage",
     "アンチエイリアス", "안티앨리어싱", "Сглаживание"},
    {"none", "Nenhum", "None", "Ninguno", "Aucun", "なし", "없음", "Нет"},
    {"aspect", "Proporção", "Aspect ratio", "Relación de aspecto", "Format d’image",
     "アスペクト比", "화면 비율", "Соотношение сторон"},
    {"stretch", "Esticar", "Stretch", "Estirar", "Étirer", "引き伸ばし", "늘리기", "Растянуть"},
    {"vsync", "VSync", "VSync", "VSync", "VSync", "VSync", "VSync", "VSync"},
    {"relaxed", "FIFO relaxado", "Relaxed FIFO", "FIFO relajado", "FIFO relâché", "FIFO (緩和)",
     "FIFO (완화)", "Ослабленный FIFO"},
    {"mode", "Modo do console", "Console mode", "Modo de la consola", "Mode de la console",
     "本体モード", "본체 모드", "Режим консоли"},
    {"tv", "TV", "TV", "TV", "TV", "TV", "TV", "ТВ"},
    {"handheld", "Portátil", "Handheld", "Portátil", "Portable", "携帯", "휴대", "Портативный"},
    {"volume", "Volume", "Volume", "Volumen", "Volume", "音量", "음량", "Громкость"},
    {"resolution", "Resolução", "Resolution", "Resolución", "Résolution", "解像度", "해상도",
     "Разрешение"},
    {"next", "ao reabrir o jogo", "on the next start", "al reabrir el juego",
     "au prochain lancement", "次回起動時", "다음 실행 시", "при следующем запуске"},
    {"keys", "↑↓ escolher   ·   ←→ mudar   ·   F1 / Esc fechar",
     "↑↓ choose   ·   ←→ change   ·   F1 / Esc close",
     "↑↓ elegir   ·   ←→ cambiar   ·   F1 / Esc cerrar",
     "↑↓ choisir   ·   ←→ modifier   ·   F1 / Échap fermer",
     "↑↓ 選択   ·   ←→ 変更   ·   F1 / Esc 閉じる", "↑↓ 선택   ·   ←→ 변경   ·   F1 / Esc 닫기",
     "↑↓ выбор   ·   ←→ изменить   ·   F1 / Esc закрыть"},
    {"fps_notice", "FPS: {}", "FPS: {}", "FPS: {}", "FPS : {}", "FPS: {}", "FPS: {}", "FPS: {}"},
};

std::string Language() {
    const char* lang = std::getenv("ZOOT_LANG");
    return lang && *lang ? lang : "pt";
}

std::string Say(std::string_view key) {
    static const std::string lang = Language();
    for (const Phrase& p : Phrases) {
        if (key == p.key) {
            return lang == "en"   ? p.en
                   : lang == "es" ? p.es
                   : lang == "fr" ? p.fr
                   : lang == "ja" ? p.ja
                   : lang == "ko" ? p.ko
                   : lang == "ru" ? p.ru
                                  : p.pt;
        }
    }
    return std::string{key};
}

void Fill(SDL_Renderer* r, float x, float y, float w, float h, unsigned rgb, float a = 1.0f) {
    SDL_SetRenderDrawColorFloat(r, ((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f,
                                (rgb & 0xFF) / 255.0f, a);
    const SDL_FRect rect{x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

void RegisterFonts() {
    static const bool done = [] {
        DWORD n = 0;
        AddFontMemResourceEx((void*)zoot_font_inter, zoot_font_inter_size, nullptr, &n);
        AddFontMemResourceEx((void*)zoot_font_inter_semi, zoot_font_inter_semi_size, nullptr, &n);
        AddFontMemResourceEx((void*)zoot_font_cinzel, zoot_font_cinzel_size, nullptr, &n);
        return true;
    }();
    (void)done;
}

float DisplayScale(SDL_Window* window) {
    const float s = window ? SDL_GetWindowDisplayScale(window) : 1.0f;
    return s > 0.5f ? s : 1.0f;
}

// The FPS modes F7 cycles: 60, 30, unlimited (the console's vsync rate raised to 240; Ocarina's
// built-in Dynamic FPS keeps the game at its speed at any rate).
enum FpsMode { Fps60 = 0, Fps30 = 1, FpsUnlimited = 2 };

FpsMode CurrentFps() {
    if (Settings::values.fps_limit.GetValue() == 30) {
        return Fps30;
    }
    return Settings::values.frame_rate_limit.GetValue() > 60 ? FpsUnlimited : Fps60;
}

void ApplyFps(int mode) {
    Settings::values.fps_limit.SetValue(mode == Fps30 ? 30 : 0);
    Settings::values.frame_rate_limit.SetValue(static_cast<u16>(mode == FpsUnlimited ? 240 : 60));
}

std::string FpsName(int mode) {
    return mode == Fps30 ? "30" : mode == FpsUnlimited ? Say("unlimited") : "60";
}

} // Anonymous namespace

Overlay::Overlay(Core::System& system_, SDL_Window* game_, std::function<void()> relayout_)
    : system{system_}, game{game_}, relayout{std::move(relayout_)} {
    RegisterFonts();
    BuildItems();
}

Overlay::~Overlay() {
    FreePanel(menu);
    FreePanel(notice);
}

void Overlay::BuildItems() {
    using namespace Settings;
    items.clear();
    items.push_back({Say("fps"),
                     {{"60", Fps60}, {"30", Fps30}, {Say("unlimited"), FpsUnlimited}},
                     [] { return int(CurrentFps()); },
                     [](int v) { ApplyFps(v); }});
    items.push_back({Say("fullscreen"),
                     {{Say("window"), 0}, {Say("fullscreen"), 1}},
                     [this] {
                         return (SDL_GetWindowFlags(game) & SDL_WINDOW_FULLSCREEN) ? 1 : 0;
                     },
                     [this](int v) { SetFullscreen(v != 0); }});
    items.push_back({Say("filter"),
                     {{"Bilinear", int(ScalingFilter::Bilinear)},
                      {"Bicubic", int(ScalingFilter::Bicubic)},
                      {"ScaleForce", int(ScalingFilter::ScaleForce)},
                      {"AMD FSR 1", int(ScalingFilter::Fsr)},
                      {"AMD FSR 3", int(ScalingFilter::Fsr3)},
                      {"Lanczos", int(ScalingFilter::Lanczos)},
                      {"Area", int(ScalingFilter::Area)}},
                     [] { return int(values.scaling_filter.GetValue()); },
                     [](int v) { values.scaling_filter.SetValue(ScalingFilter(v)); }});
    std::vector<std::pair<std::string, int>> sharp;
    for (int v = 0; v <= 200; v += 25) {
        sharp.emplace_back(fmt::format("{}%", v), v);
    }
    items.push_back({Say("sharp"), sharp,
                     [] { return values.fsr_sharpening_slider.GetValue(); },
                     [](int v) { values.fsr_sharpening_slider.SetValue(v); }});
    items.push_back({Say("aa"),
                     {{Say("none"), int(AntiAliasing::None)},
                      {"FXAA", int(AntiAliasing::Fxaa)},
                      {"SMAA", int(AntiAliasing::Smaa)}},
                     [] { return int(values.anti_aliasing.GetValue()); },
                     [](int v) { values.anti_aliasing.SetValue(AntiAliasing(v)); }});
    items.push_back({Say("aspect"),
                     {{"16:9", int(AspectRatio::R16_9)},
                      {"21:9", int(AspectRatio::R21_9)},
                      {"16:10", int(AspectRatio::R16_10)},
                      {"4:3", int(AspectRatio::R4_3)},
                      {Say("stretch"), int(AspectRatio::Stretch)}},
                     [] { return int(values.aspect_ratio.GetValue()); },
                     [this](int v) {
                         values.aspect_ratio.SetValue(AspectRatio(v));
                         if (relayout) {
                             relayout();
                         }
                     }});
    items.push_back({Say("vsync"),
                     {{Say("off"), int(VSyncMode::Immediate)},
                      {"Mailbox", int(VSyncMode::Mailbox)},
                      {Say("on") + " (FIFO)", int(VSyncMode::Fifo)},
                      {Say("relaxed"), int(VSyncMode::FifoRelaxed)}},
                     [] { return int(values.vsync_mode.GetValue()); },
                     [](int v) { values.vsync_mode.SetValue(VSyncMode(v)); }});
    items.push_back({Say("mode"),
                     {{Say("tv"), int(ConsoleMode::Docked)},
                      {Say("handheld"), int(ConsoleMode::Handheld)}},
                     [] { return int(values.use_docked_mode.GetValue()); },
                     [this](int v) {
                         values.use_docked_mode.SetValue(ConsoleMode(v));
                         if (system.IsPoweredOn()) {
                             system.GetAppletManager().OperationModeChanged();
                         }
                     }});
    std::vector<std::pair<std::string, int>> volume;
    for (int v = 0; v <= 100; v += 10) {
        volume.emplace_back(fmt::format("{}%", v), v);
    }
    items.push_back({Say("volume"), volume, [] { return int(values.volume.GetValue()); },
                     [](int v) { values.volume.SetValue(static_cast<u8>(v)); }});
    items.push_back({Say("resolution"),
                     {{"0.5x (540p)", int(ResolutionSetup::Res1_2X)},
                      {"0.75x", int(ResolutionSetup::Res3_4X)},
                      {"1x (1080p)", int(ResolutionSetup::Res1X)},
                      {"1.25x", int(ResolutionSetup::Res5_4X)},
                      {"1.5x", int(ResolutionSetup::Res3_2X)},
                      {"2x (4K)", int(ResolutionSetup::Res2X)},
                      {"3x", int(ResolutionSetup::Res3X)},
                      {"4x", int(ResolutionSetup::Res4X)}},
                     [] { return int(values.resolution_setup.GetValue()); },
                     [](int v) { values.resolution_setup.SetValue(ResolutionSetup(v)); },
                     true});
}

bool Overlay::HandleKey(int scancode, bool down) {
    if (scancode == KeyF1) {
        if (down) {
            OpenMenu(!menu_open);
        }
        return true;
    }
    if (scancode == KeyF7) {
        if (down) {
            CycleFps();
        }
        return true;
    }
    if (scancode == KeyF11) {
        if (down) {
            SetFullscreen(!(SDL_GetWindowFlags(game) & SDL_WINDOW_FULLSCREEN));
        }
        return true;
    }
    if (!menu_open) {
        return false;
    }
    // While the menu is open it takes the keyboard.
    if (down) {
        switch (scancode) {
        case SDL_SCANCODE_ESCAPE:
            OpenMenu(false);
            break;
        case SDL_SCANCODE_UP:
            selected = (selected + int(items.size()) - 1) % int(items.size());
            break;
        case SDL_SCANCODE_DOWN:
            selected = (selected + 1) % int(items.size());
            break;
        case SDL_SCANCODE_LEFT:
            Step(selected, -1);
            break;
        case SDL_SCANCODE_RIGHT:
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_SPACE:
            Step(selected, +1);
            break;
        default:
            break;
        }
        dirty = true;
    }
    return true;
}

void Overlay::Step(int index, int direction) {
    Item& item = items[std::size_t(index)];
    const int now = item.get();
    auto it = std::find_if(item.choices.begin(), item.choices.end(),
                           [now](const auto& c) { return c.second == now; });
    int pos = it == item.choices.end() ? 0 : int(it - item.choices.begin());
    pos = (pos + direction + int(item.choices.size())) % int(item.choices.size());
    item.set(item.choices[std::size_t(pos)].second);
    LOG_INFO(Frontend, "ZooT menu: {} -> {}", item.label, item.choices[std::size_t(pos)].first);
    SaveToLauncher();
    dirty = true;
}

void Overlay::CycleFps() {
    const int next = CurrentFps() == Fps60 ? Fps30 : CurrentFps() == Fps30 ? FpsUnlimited : Fps60;
    ApplyFps(next);
    LOG_INFO(Frontend, "ZooT: FPS mode {}", FpsName(next));
    SaveToLauncher();
    Notice(fmt::format(fmt::runtime(Say("fps_notice")), FpsName(next)));
    dirty = true;
}

void Overlay::SetFullscreen(bool on) {
    SDL_SetWindowFullscreen(game, on);
    SaveToLauncher();
    Notice(on ? Say("fullscreen") : Say("window"));
    dirty = true;
}

void Overlay::SaveToLauncher() {
    using namespace Settings;
    const auto path = Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir) / "zoot" /
                      "launcher.json";
    nlohmann::json data = nlohmann::json::object();
    {
        std::ifstream in{path};
        if (in) {
            data = nlohmann::json::parse(in, nullptr, false);
            if (data.is_discarded() || !data.is_object()) {
                data = nlohmann::json::object();
            }
        }
    }
    data["resolution"] = int(values.resolution_setup.GetValue());
    data["filter"] = int(values.scaling_filter.GetValue());
    data["sharpness"] = values.fsr_sharpening_slider.GetValue();
    data["aa"] = int(values.anti_aliasing.GetValue());
    data["aspect"] = int(values.aspect_ratio.GetValue());
    data["vsync"] = int(values.vsync_mode.GetValue());
    data["mode"] = int(values.use_docked_mode.GetValue());
    data["volume"] = int(values.volume.GetValue());
    data["fullscreen"] = (SDL_GetWindowFlags(game) & SDL_WINDOW_FULLSCREEN) != 0;
    const int fps = CurrentFps();
    data["fps_mode"] = fps == Fps30 ? "30" : fps == FpsUnlimited ? "unlimited" : "60";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto tmp = path.parent_path() / "launcher.json.tmp";
    {
        std::ofstream out{tmp, std::ios::binary | std::ios::trunc};
        out << data.dump(2);
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        LOG_WARNING(Frontend, "ZooT: could not save {}: {}", Common::FS::PathToUTF8String(path),
                    ec.message());
    }
}

void Overlay::OpenMenu(bool open) {
    menu_open = open;
    if (open) {
        BuildItems();  // fresh values
        if (!menu.window && !MakePanel(menu, 620, 640)) {
            menu_open = false;
            return;
        }
        // The keyboard stays with the game window: its key events reach HandleKey.
        SDL_ShowWindow(menu.window);
        SDL_RaiseWindow(game);
        dirty = true;
    } else if (menu.window) {
        SDL_HideWindow(menu.window);
    }
}

void Overlay::Notice(const std::string& text) {
    notice_text = text;
    notice_until = std::chrono::steady_clock::now() + std::chrono::milliseconds{1600};
    if (!notice.window && !MakePanel(notice, 300, 64)) {
        return;
    }
    SDL_ShowWindow(notice.window);
    SDL_RaiseWindow(game);
    dirty = true;
}

bool Overlay::MakePanel(Panel& panel, int w, int h) {
    const float s = DisplayScale(game);
    panel.window = SDL_CreateWindow(ZootBaseTitle().c_str(), int(w * s), int(h * s),
                                    SDL_WINDOW_BORDERLESS | SDL_WINDOW_UTILITY | SDL_WINDOW_HIDDEN);
    if (!panel.window) {
        LOG_WARNING(Frontend, "ZooT overlay: {}", SDL_GetError());
        return false;
    }
    SDL_SetWindowParent(panel.window, game);
    panel.renderer = SDL_CreateRenderer(panel.window, nullptr);
    if (!panel.renderer) {
        SDL_DestroyWindow(panel.window);
        panel.window = nullptr;
        return false;
    }
    SDL_SetRenderDrawBlendMode(panel.renderer, SDL_BLENDMODE_BLEND);
    if (&panel == &menu && !shield) {
        if (SDL_Surface* s2 =
                SDL_LoadPNG_IO(SDL_IOFromConstMem((void*)yuzu_icon, yuzu_icon_size), true)) {
            shield = SDL_CreateTextureFromSurface(panel.renderer, s2);
            SDL_DestroySurface(s2);
            if (shield) {
                SDL_SetTextureScaleMode(shield, SDL_SCALEMODE_LINEAR);
            }
        }
    }
    return true;
}

void Overlay::FreePanel(Panel& panel) {
    for (auto& [key, text] : panel.texts) {
        SDL_DestroyTexture(text.texture);
    }
    panel.texts.clear();
    if (&panel == &menu && shield) {
        SDL_DestroyTexture(shield);
        shield = nullptr;
    }
    if (panel.renderer) {
        SDL_DestroyRenderer(panel.renderer);
    }
    if (panel.window) {
        SDL_DestroyWindow(panel.window);
    }
    panel = {};
}

const Overlay::Text& Overlay::GetText(Panel& panel, const std::string& utf8, int px,
                                      bool semibold, bool title) {
    const std::string key = fmt::format("{}|{}|{}|{}", px, semibold, title, utf8);
    if (auto it = panel.texts.find(key); it != panel.texts.end()) {
        return it->second;
    }
    if (panel.texts.size() > 200) {
        for (auto& [k, t] : panel.texts) {
            SDL_DestroyTexture(t.texture);
        }
        panel.texts.clear();
    }
    Text out;
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    if (wlen > 0 && px > 0) {
        std::wstring wide(std::size_t(wlen), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), wide.data(), wlen);
        static const std::string lang = Language();
        const wchar_t* family = title          ? L"Cinzel Bold"
                                : lang == "ja" ? L"Yu Gothic UI"
                                : lang == "ko" ? L"Malgun Gothic"
                                : semibold     ? L"Inter SemiBold"
                                               : L"Inter";
        HDC dc = CreateCompatibleDC(nullptr);
        HFONT font = CreateFontW(-px, 0, 0, 0, semibold && !title ? FW_SEMIBOLD : FW_NORMAL,
                                 FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, family);
        HGDIOBJ old_font = SelectObject(dc, font);
        SIZE size{};
        GetTextExtentPoint32W(dc, wide.c_str(), wlen, &size);
        const int w = size.cx + 4, h = size.cy + 2;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        if (HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0)) {
            HGDIOBJ old_bmp = SelectObject(dc, bmp);
            SetBkColor(dc, RGB(0, 0, 0));
            SetTextColor(dc, RGB(255, 255, 255));
            RECT rc{0, 0, w, h};
            ExtTextOutW(dc, 2, 1, ETO_OPAQUE, &rc, wide.c_str(), UINT(wlen), nullptr);
            GdiFlush();
            auto* p = static_cast<std::uint32_t*>(bits);
            for (int i = 0; i < w * h; ++i) {
                const std::uint32_t v = p[i];
                const std::uint32_t a = std::max({v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF});
                p[i] = (a << 24) | 0x00FFFFFF;
            }
            if (SDL_Surface* s =
                    SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ARGB8888, bits, w * 4)) {
                out.texture = SDL_CreateTextureFromSurface(panel.renderer, s);
                SDL_DestroySurface(s);
                if (out.texture) {
                    SDL_SetTextureBlendMode(out.texture, SDL_BLENDMODE_BLEND);
                    out.w = float(w);
                    out.h = float(h);
                }
            }
            SelectObject(dc, old_bmp);
            DeleteObject(bmp);
        }
        SelectObject(dc, old_font);
        DeleteObject(font);
        DeleteDC(dc);
    }
    return panel.texts.emplace(key, out).first->second;
}

void Overlay::Put(Panel& panel, const Text& text, float x, float y, unsigned rgb) {
    if (!text.texture) {
        return;
    }
    SDL_SetTextureColorMod(text.texture, Uint8(rgb >> 16), Uint8(rgb >> 8), Uint8(rgb));
    const SDL_FRect dst{std::round(x), std::round(y), text.w, text.h};
    SDL_RenderTexture(panel.renderer, text.texture, nullptr, &dst);
}

void Overlay::PlaceOver(SDL_Window* window, int w, int h, bool top) {
    int gx = 0, gy = 0, gw = 0, gh = 0;
    SDL_GetWindowPosition(game, &gx, &gy);
    SDL_GetWindowSize(game, &gw, &gh);
    const int x = gx + (gw - w) / 2;
    const int y = top ? gy + std::max(16, gh / 14) : gy + std::max(0, (gh - h) / 2);
    int cx = 0, cy = 0;
    SDL_GetWindowPosition(window, &cx, &cy);
    if (cx != x || cy != y) {
        SDL_SetWindowPosition(window, x, y);
    }
}

void Overlay::Tick() {
    const auto now = std::chrono::steady_clock::now();
    const bool minimized = (SDL_GetWindowFlags(game) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN));
    if (notice.window) {
        const bool show = !notice_text.empty() && now < notice_until && !minimized;
        const bool shown = !(SDL_GetWindowFlags(notice.window) & SDL_WINDOW_HIDDEN);
        if (show != shown) {
            show ? SDL_ShowWindow(notice.window) : SDL_HideWindow(notice.window);
        }
        if (show) {
            DrawNotice();
        }
    }
    if (menu_open && menu.window) {
        if (minimized) {
            SDL_HideWindow(menu.window);
        } else {
            if (SDL_GetWindowFlags(menu.window) & SDL_WINDOW_HIDDEN) {
                SDL_ShowWindow(menu.window);
            }
            DrawMenu();
        }
    }
    dirty = false;
}

void Overlay::DrawNotice() {
    int w = 0, h = 0;
    SDL_GetWindowSize(notice.window, &w, &h);
    PlaceOver(notice.window, w, h, true);
    int pw = 0, ph = 0;
    SDL_GetCurrentRenderOutputSize(notice.renderer, &pw, &ph);
    const float s = float(ph) / 64.0f;
    SDL_Renderer* r = notice.renderer;
    Fill(r, 0, 0, float(pw), float(ph), 0x0B1424);
    Fill(r, 0, 0, 4 * s, float(ph), Gold);
    Fill(r, 0, float(ph) - 1, float(pw), 1, 0x2A3A5C);
    const Text& t = GetText(notice, notice_text, int(22 * s), true, false);
    Put(notice, t, (float(pw) - t.w) / 2, (float(ph) - t.h) / 2, Gold);
    SDL_RenderPresent(r);
}

void Overlay::DrawMenu() {
    int w = 0, h = 0;
    SDL_GetWindowSize(menu.window, &w, &h);
    PlaceOver(menu.window, w, h, false);
    int pw = 0, ph = 0;
    SDL_GetCurrentRenderOutputSize(menu.renderer, &pw, &ph);
    const float s = float(ph) / 640.0f;
    SDL_Renderer* r = menu.renderer;
    const float W = float(pw), H = float(ph);

    Fill(r, 0, 0, W, H, 0x0B1222);
    Fill(r, 0, 0, W, 1 * s, 0x3A4E78);
    Fill(r, 0, H - 1 * s, W, 1 * s, 0x3A4E78);
    Fill(r, 0, 0, 1 * s, H, 0x3A4E78);
    Fill(r, W - 1 * s, 0, 1 * s, H, 0x3A4E78);

    // Header.
    const float pad = 26 * s;
    if (shield) {
        const SDL_FRect dst{pad, 22 * s, 40 * s, 40 * s};
        SDL_RenderTexture(r, shield, nullptr, &dst);
    }
    const Text& title = GetText(menu, Say("menu"), int(24 * s), false, true);
    Put(menu, title, pad + 52 * s, 22 * s + (40 * s - title.h) / 2, Gold);
    const Text& live = GetText(menu, Say("live"), int(13 * s), false, false);
    Put(menu, live, W - pad - live.w, 22 * s + (40 * s - live.h) / 2, Muted);
    Fill(r, pad, 76 * s, W - 2 * pad, 1 * s, 0x2A3A5C);

    // Rows.
    const float row_h = 46 * s;
    float y = 88 * s;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const Item& item = items[i];
        const bool on = int(i) == selected;
        if (on) {
            Fill(r, pad - 10 * s, y, W - 2 * pad + 20 * s, row_h, 0x17284A);
            Fill(r, pad - 10 * s, y + 10 * s, 3 * s, row_h - 20 * s, Gold);
        }
        const Text& label = GetText(menu, item.label, int(16 * s), on, false);
        Put(menu, label, pad + 4 * s, y + (row_h - label.h) / 2, on ? White : 0xC3CCDA);
        const int now = item.get();
        std::string value = "?";
        for (const auto& c : item.choices) {
            if (c.second == now) {
                value = c.first;
            }
        }
        const Text& val = GetText(menu, value, int(16 * s), true, false);
        const Text& left = GetText(menu, "◀", int(12 * s), false, false);
        const Text& right = GetText(menu, "▶", int(12 * s), false, false);
        float vx = W - pad - right.w;
        Put(menu, right, vx, y + (row_h - right.h) / 2, on ? Gold : 0x5A6884);
        vx -= 14 * s + val.w;
        Put(menu, val, vx, y + (row_h - val.h) / 2, on ? Gold : White);
        vx -= 14 * s + left.w;
        Put(menu, left, vx, y + (row_h - left.h) / 2, on ? Gold : 0x5A6884);
        if (item.next_start) {
            const Text& note = GetText(menu, "(" + Say("next") + ")", int(12 * s), false, false);
            Put(menu, note, vx - 12 * s - note.w, y + (row_h - note.h) / 2, Muted);
        }
        y += row_h;
    }

    // Footer.
    Fill(r, pad, H - 58 * s, W - 2 * pad, 1 * s, 0x2A3A5C);
    const Text& keys = GetText(menu, Say("keys"), int(13 * s), false, false);
    Put(menu, keys, (W - keys.w) / 2, H - 42 * s, Muted);
    SDL_RenderPresent(r);
}

} // namespace ZooT
