// SPDX-License-Identifier: GPL-3.0-or-later
// ZooT-PC loading screen: a full-window screen in the style of current consoles. The player's own
// cover (user/zoot/capa.png or .jpg, never shipped) soft and dark behind, the ZooT shield and
// title at the top, and at the bottom the stage, a large percentage, a thin gold bar with a moving
// sheen, the count, the time left and "Esc to skip", in the launcher's language (ZOOT_LANG).
// Text is drawn with the Windows GDI from the Inter and Cinzel fonts embedded in the executable
// (SIL Open Font License); the window is owned by the game window, so it never covers other
// programs.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <filesystem>
#include <numbers>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <SDL3/SDL.h>
#include <fmt/format.h>
#include <stb_image.h>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "video_core/shader_warm/shader_warm_builder.h"
#include "yuzu_cmd/emu_window/emu_window_sdl3.h"
#include "yuzu_cmd/yuzu_icon.h"
#include "yuzu_cmd/zoot_font_cinzel.h"
#include "yuzu_cmd/zoot_font_inter.h"
#include "yuzu_cmd/zoot_font_inter_semi.h"
#include "yuzu_cmd/zoot_loading_screen.h"

namespace ZooT {
namespace {

constexpr unsigned White = 0xF4F6FB;
constexpr unsigned Muted = 0x9AA6BD;
constexpr unsigned Gold = 0xE7C164;

// ---- the screen's texts, in the launcher's language -------------------------------------------
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
    {"prepare", "Preparando...", "Getting ready...", "Preparando...", "Préparation...",
     "準備中...", "준비 중...", "Подготовка..."},
    {"read", "Lendo os arquivos do jogo", "Reading the game files", "Leyendo los archivos del juego",
     "Lecture des fichiers du jeu", "ゲームファイルを読み込み中", "게임 파일 읽는 중",
     "Чтение файлов игры"},
    {"extract", "Extraindo os shaders do jogo", "Extracting the game shaders",
     "Extrayendo los shaders del juego", "Extraction des shaders du jeu",
     "ゲームのシェーダーを抽出中", "게임 셰이더 추출 중", "Извлечение шейдеров игры"},
    {"convert", "Convertendo os shaders", "Converting the shaders", "Convirtiendo los shaders",
     "Conversion des shaders", "シェーダーを変換中", "셰이더 변환 중", "Преобразование шейдеров"},
    {"write", "Gravando o pré-cache", "Writing the precache", "Guardando la precaché",
     "Écriture du précache", "プリキャッシュを保存中", "프리캐시 저장 중",
     "Запись предкэша"},
    {"cache", "Lendo o cache de shaders", "Reading the shader cache",
     "Leyendo la caché de shaders", "Lecture du cache de shaders", "シェーダーキャッシュを読み込み中",
     "셰이더 캐시 읽는 중", "Чтение кэша шейдеров"},
    {"build", "Carregando os shaders salvos", "Loading the saved shaders",
     "Cargando los shaders guardados", "Chargement des shaders enregistrés",
     "保存済みシェーダーを読み込み中", "저장된 셰이더 불러오는 중",
     "Загрузка сохранённых шейдеров"},
    {"warm", "Pré-compilando os shaders do jogo", "Precompiling the game shaders",
     "Precompilando los shaders del juego", "Précompilation des shaders du jeu",
     "ゲームのシェーダーをプリコンパイル中", "게임 셰이더 미리 컴파일 중",
     "Предкомпиляция шейдеров игры"},
    {"start", "Iniciando o jogo", "Starting the game", "Iniciando el juego", "Lancement du jeu",
     "ゲームを起動中", "게임 시작 중", "Запуск игры"},
    {"left", "faltam ~{}", "~{} left", "faltan ~{}", "encore ~{}", "残り約 {}", "약 {} 남음",
     "осталось ~{}"},
    {"time", "Tempo {}", "Time {}", "Tiempo {}", "Temps {}", "経過 {}", "경과 {}", "Время {}"},
    {"skip", "Esc para pular", "Esc to skip", "Esc para saltar", "Échap pour passer",
     "Esc でスキップ", "Esc로 건너뛰기", "Esc — пропустить"},
};

std::string Language() {
    const char* lang = std::getenv("ZOOT_LANG");
    return lang && *lang ? lang : "pt";
}

std::string Say(std::string_view key) {
    static const std::string lang = Language();
    for (const Phrase& p : Phrases) {
        if (key == p.key) {
            const char* text = lang == "en"   ? p.en
                               : lang == "es" ? p.es
                               : lang == "fr" ? p.fr
                               : lang == "ja" ? p.ja
                               : lang == "ko" ? p.ko
                               : lang == "ru" ? p.ru
                                              : p.pt;
            return text;
        }
    }
    return std::string{key};
}

std::string FormatCount(std::size_t n) {
    static const std::string lang = Language();
    const char* sep = (lang == "en" || lang == "ja" || lang == "ko") ? ","
                      : (lang == "fr" || lang == "ru")               ? " "
                                                                     : ".";
    std::string digits = std::to_string(n);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i != 0 && (digits.size() - i) % 3 == 0) {
            out += sep;
        }
        out += digits[i];
    }
    return out;
}

std::string FormatSeconds(double seconds) {
    const int total = static_cast<int>(seconds + 0.5);
    return total >= 60 ? fmt::format("{}m{:02}s", total / 60, total % 60)
                       : fmt::format("{}s", total);
}

// ---- drawing helpers ---------------------------------------------------------------------------
SDL_FColor Color(unsigned rgb, float a = 1.0f) {
    return {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a};
}

/// A rectangle with a colour per corner (top left, top right, bottom right, bottom left).
void Quad(SDL_Renderer* r, float x, float y, float w, float h, SDL_FColor tl, SDL_FColor tr,
          SDL_FColor br, SDL_FColor bl) {
    const SDL_Vertex v[4] = {{{x, y}, tl, {0, 0}},
                             {{x + w, y}, tr, {0, 0}},
                             {{x + w, y + h}, br, {0, 0}},
                             {{x, y + h}, bl, {0, 0}}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}

void Fill(SDL_Renderer* r, float x, float y, float w, float h, SDL_FColor c) {
    Quad(r, x, y, w, h, c, c, c, c);
}

/// A pill (rounded ends) from rectangles and two half discs.
void Pill(SDL_Renderer* r, float x, float y, float w, float h, SDL_FColor left, SDL_FColor right) {
    if (w <= 0) {
        return;
    }
    const float rad = h / 2;
    if (w < h) {
        Fill(r, x, y, w, h, left);
        return;
    }
    Quad(r, x + rad, y, w - h, h, left, right, right, left);
    for (int side = 0; side < 2; ++side) {
        const float cx = side ? x + w - rad : x + rad;
        const SDL_FColor c = side ? right : left;
        std::vector<SDL_Vertex> v;
        std::vector<int> idx;
        v.push_back({{cx, y + rad}, c, {0, 0}});
        constexpr int Steps = 12;
        for (int i = 0; i <= Steps; ++i) {
            const float a = std::numbers::pi_v<float> * (side ? -0.5f + i / float(Steps)
                                                             : 0.5f + i / float(Steps));
            v.push_back({{cx + rad * std::cos(a), y + rad - rad * std::sin(a)}, c, {0, 0}});
            if (i > 0) {
                idx.insert(idx.end(), {0, i, i + 1});
            }
        }
        SDL_RenderGeometry(r, nullptr, v.data(), static_cast<int>(v.size()), idx.data(),
                           static_cast<int>(idx.size()));
    }
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

SDL_Texture* LoadTexture(SDL_Renderer* r, SDL_Surface* surface) {
    if (!surface) {
        return nullptr;
    }
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, surface);
    SDL_DestroySurface(surface);
    if (t) {
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }
    return t;
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
    RegisterFonts();
    // The game window, made before this screen.
    int count = 0;
    if (SDL_Window** const windows = SDL_GetWindows(&count)) {
        if (count > 0) {
            game = windows[0];
        }
        SDL_free(windows);
    }
    int w = 1280, h = 720;
    if (game) {
        SDL_GetWindowSize(game, &w, &h);
    }
    window = SDL_CreateWindow(ZootBaseTitle().c_str(), w, h,
                              SDL_WINDOW_BORDERLESS | SDL_WINDOW_UTILITY | SDL_WINDOW_HIDDEN |
                                  SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        LOG_WARNING(Frontend, "ZooT loading screen: {}", SDL_GetError());
        return;
    }
    // Owned by the game window: above it, below every other program, out of the taskbar.
    if (game && !SDL_SetWindowParent(window, game)) {
        LOG_WARNING(Frontend, "ZooT loading screen parent: {}", SDL_GetError());
    }
    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        LOG_WARNING(Frontend, "ZooT loading screen renderer: {}", SDL_GetError());
        SDL_DestroyWindow(window);
        window = nullptr;
        return;
    }
    // Shapes are drawn with alpha (shades, the bar's track and sheen): blending on.
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    ApplyPlayerIcon(window);
    shield = LoadTexture(renderer,
                         SDL_LoadPNG_IO(SDL_IOFromConstMem((void*)yuzu_icon, yuzu_icon_size), true));
    LoadBackground();
    FollowGameWindow();
    SDL_ShowWindow(window);
    stage = "prepare";
    Draw();
}

LoadingScreen::~LoadingScreen() {
    for (auto& [key, text] : text_cache) {
        SDL_DestroyTexture(text.texture);
    }
    if (background) {
        SDL_DestroyTexture(background);
    }
    if (shield) {
        SDL_DestroyTexture(shield);
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
}

void LoadingScreen::LoadBackground() {
    const auto dir = Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir) / "zoot";
    for (const char* name : {"capa.png", "capa.jpg", "capa.jpeg"}) {
        const auto path = dir / name;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            continue;
        }
        // PNG through SDL (common/stb.h builds stb_image with JPEG only), JPEG through stb.
        int w = 0, h = 0, n = 0;
        unsigned char* pixels = nullptr;
        SDL_Surface* png = nullptr;
        const std::string utf8 = Common::FS::PathToUTF8String(path);
        if (std::string_view{name}.ends_with(".png")) {
            if (SDL_Surface* loaded = SDL_LoadPNG(utf8.c_str())) {
                png = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
                SDL_DestroySurface(loaded);
            }
            if (!png) {
                LOG_WARNING(Frontend, "ZooT cover {}: {}", utf8, SDL_GetError());
                continue;
            }
            w = png->w;
            h = png->h;
            // Tightly packed copy, as stb returns it.
            pixels = static_cast<unsigned char*>(std::malloc(std::size_t(w) * h * 4));
            for (int y = 0; pixels && y < h; ++y) {
                std::memcpy(pixels + std::size_t(y) * w * 4,
                            static_cast<unsigned char*>(png->pixels) + std::size_t(y) * png->pitch,
                            std::size_t(w) * 4);
            }
            SDL_DestroySurface(png);
        } else {
            pixels = stbi_load(utf8.c_str(), &w, &h, &n, 4);
        }
        if (!pixels || w <= 0 || h <= 0) {
            continue;
        }
        // Box-averaged to ~128 px wide: stretched with linear filtering it is a soft backdrop.
        const int tw = std::min(128, w);
        const int th = std::max(1, h * tw / w);
        SDL_Surface* small = SDL_CreateSurface(tw, th, SDL_PIXELFORMAT_RGBA32);
        if (small) {
            auto* out = static_cast<unsigned char*>(small->pixels);
            for (int y = 0; y < th; ++y) {
                for (int x = 0; x < tw; ++x) {
                    const int x0 = x * w / tw, x1 = std::max(x0 + 1, (x + 1) * w / tw);
                    const int y0 = y * h / th, y1 = std::max(y0 + 1, (y + 1) * h / th);
                    unsigned sum[4]{};
                    for (int sy = y0; sy < y1; ++sy) {
                        for (int sx = x0; sx < x1; ++sx) {
                            const unsigned char* p = pixels + (std::size_t(sy) * w + sx) * 4;
                            for (int c = 0; c < 4; ++c) {
                                sum[c] += p[c];
                            }
                        }
                    }
                    const unsigned count = unsigned((x1 - x0) * (y1 - y0));
                    unsigned char* o = out + std::size_t(y) * small->pitch + std::size_t(x) * 4;
                    for (int c = 0; c < 4; ++c) {
                        o[c] = static_cast<unsigned char>(sum[c] / count);
                    }
                }
            }
            background = LoadTexture(renderer, small);
        }
        if (std::string_view{name}.ends_with(".png")) {
            std::free(pixels);
        } else {
            stbi_image_free(pixels);
        }
        if (background) {
            return;
        }
    }
}

void LoadingScreen::FollowGameWindow() {
    if (!game || !window) {
        return;
    }
    const SDL_WindowFlags flags = SDL_GetWindowFlags(game);
    if (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) {
        SDL_HideWindow(window);
        return;
    }
    int gx = 0, gy = 0, gw = 0, gh = 0, x = 0, y = 0, w = 0, h = 0;
    SDL_GetWindowPosition(game, &gx, &gy);
    SDL_GetWindowSize(game, &gw, &gh);
    SDL_GetWindowPosition(window, &x, &y);
    SDL_GetWindowSize(window, &w, &h);
    if (gx != x || gy != y) {
        SDL_SetWindowPosition(window, gx, gy);
    }
    if (gw != w || gh != h) {
        SDL_SetWindowSize(window, gw, gh);
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN) {
        SDL_ShowWindow(window);
    }
}

LoadingScreen::Text LoadingScreen::MakeText(const std::string& utf8, int px, bool semibold,
                                            bool title) {
    Text out;
    if (utf8.empty() || px <= 0) {
        return out;
    }
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    std::wstring wide(std::size_t(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), wide.data(), wlen);

    static const std::string lang = Language();
    const wchar_t* family = title             ? L"Cinzel Bold"
                            : lang == "ja"    ? L"Yu Gothic UI"
                            : lang == "ko"    ? L"Malgun Gothic"
                            : semibold        ? L"Inter SemiBold"
                                              : L"Inter";
    // "Cinzel Bold" is the bold face's own family name; Inter SemiBold is matched by weight.
    const int weight = title ? FW_NORMAL : semibold ? FW_SEMIBOLD : FW_NORMAL;
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH, family);
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
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        HGDIOBJ old_bmp = SelectObject(dc, bmp);
        SetBkColor(dc, RGB(0, 0, 0));
        SetTextColor(dc, RGB(255, 255, 255));
        RECT rc{0, 0, w, h};
        ExtTextOutW(dc, 2, 1, ETO_OPAQUE, &rc, wide.c_str(), UINT(wlen), nullptr);
        GdiFlush();
        // White glyphs on black: the brightness is the alpha of a white text texture.
        auto* p = static_cast<std::uint32_t*>(bits);
        for (int i = 0; i < w * h; ++i) {
            const std::uint32_t v = p[i];
            const std::uint32_t a =
                std::max({v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF});
            p[i] = (a << 24) | 0x00FFFFFF;
        }
        if (SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ARGB8888, bits, w * 4)) {
            out.texture = SDL_CreateTextureFromSurface(renderer, s);
            SDL_DestroySurface(s);
            if (out.texture) {
                SDL_SetTextureBlendMode(out.texture, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(out.texture, SDL_SCALEMODE_LINEAR);
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
    return out;
}

const LoadingScreen::Text& LoadingScreen::CachedText(const std::string& utf8, int px,
                                                     bool semibold, bool title) {
    const std::string key = fmt::format("{}|{}|{}|{}", px, semibold, title, utf8);
    if (auto it = text_cache.find(key); it != text_cache.end()) {
        return it->second;
    }
    if (text_cache.size() > 160) {  // counts and times change: drop the old ones now and then
        for (auto& [k, t] : text_cache) {
            SDL_DestroyTexture(t.texture);
        }
        text_cache.clear();
    }
    return text_cache.emplace(key, MakeText(utf8, px, semibold, title)).first->second;
}

void LoadingScreen::PutText(const Text& text, float x, float y, unsigned rgb, float alpha) {
    if (!text.texture) {
        return;
    }
    SDL_SetTextureColorMod(text.texture, Uint8(rgb >> 16), Uint8(rgb >> 8), Uint8(rgb));
    SDL_SetTextureAlphaModFloat(text.texture, alpha);
    const SDL_FRect dst{std::round(x), std::round(y), text.w, text.h};
    SDL_RenderTexture(renderer, text.texture, nullptr, &dst);
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
    if (std::chrono::steady_clock::now() - last_draw < std::chrono::milliseconds{33}) {
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
    FollowGameWindow();

    std::string current;
    std::size_t current_done, current_total, first_done;
    double stage_seconds;
    {
        std::scoped_lock lock{mutex};
        current = stage;
        current_done = done;
        current_total = total;
        first_done = stage_first_done;
        stage_seconds = std::chrono::duration<double>(last_draw - stage_begin).count();
    }
    const double elapsed = std::chrono::duration<double>(last_draw - begin).count();

    int iw = 0, ih = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &iw, &ih);
    const float W = float(iw), H = float(ih);
    const float s = std::clamp(H / 720.0f, 0.6f, 2.5f);

    // Backdrop: the player's cover, soft and dark, or night blue.
    SDL_SetRenderDrawColor(renderer, 6, 10, 20, 255);
    SDL_RenderClear(renderer);
    if (background) {
        float tw = 0, th = 0;
        SDL_GetTextureSize(background, &tw, &th);
        const float k = std::max(W / tw, H / th);
        const SDL_FRect dst{(W - tw * k) / 2, (H - th * k) / 2, tw * k, th * k};
        SDL_RenderTexture(renderer, background, nullptr, &dst);
        Fill(renderer, 0, 0, W, H, Color(0x060A14, 0.55f));
    } else {
        Quad(renderer, 0, 0, W, H, Color(0x16264A), Color(0x101B36), Color(0x05080F),
             Color(0x070B16));
    }
    // Darker toward the bottom, where the text is.
    Quad(renderer, 0, H * 0.45f, W, H * 0.55f, Color(0x05080F, 0.0f), Color(0x05080F, 0.0f),
         Color(0x05080F, 0.94f), Color(0x05080F, 0.94f));
    Quad(renderer, 0, 0, W, H * 0.22f, Color(0x05080F, 0.55f), Color(0x05080F, 0.55f),
         Color(0x05080F, 0.0f), Color(0x05080F, 0.0f));

    const float x0 = 56 * s, x1 = W - 56 * s;

    // Top: shield and title.
    const float icon = 52 * s;
    if (shield) {
        const SDL_FRect dst{x0, 38 * s, icon, icon};
        SDL_RenderTexture(renderer, shield, nullptr, &dst);
    }
    const Text& title = CachedText(ZootBaseTitle(), int(28 * s), false, true);
    PutText(title, x0 + icon + 14 * s, 38 * s + (icon - title.h) / 2, Gold);

    // Bottom: stage, percentage, bar, details.
    const float bar_y = H - 74 * s;
    const float bar_h = std::max(4.0f, 6 * s);
    const double fraction =
        current_total ? std::clamp(double(current_done) / double(current_total), 0.0, 1.0) : 0.0;

    const Text& stage_text = CachedText(Say(current), int(23 * s), true, false);
    PutText(stage_text, x0, bar_y - 22 * s - stage_text.h, White);
    if (current_total) {
        const Text& pct = CachedText(fmt::format("{:.0f}%", fraction * 100), int(34 * s), true,
                                     false);
        PutText(pct, x1 - pct.w, bar_y - 16 * s - pct.h, Gold);
    }

    const float bar_w = x1 - x0;
    Pill(renderer, x0, bar_y, bar_w, bar_h, Color(0xFFFFFF, 0.12f), Color(0xFFFFFF, 0.12f));
    const double t = elapsed;
    if (current_total) {
        const float fill = float(bar_w * fraction);
        Pill(renderer, x0, bar_y, fill, bar_h, Color(0xB8862B), Color(0xF3D27A));
        // A sheen travelling along the filled part.
        const float sheen = 140 * s;
        if (fill > sheen) {
            const float sx = x0 + float(std::fmod(t * 0.45, 1.0)) * (fill - sheen);
            Quad(renderer, sx, bar_y, sheen / 2, bar_h, Color(0xFFFFFF, 0.0f),
                 Color(0xFFFFFF, 0.45f), Color(0xFFFFFF, 0.45f), Color(0xFFFFFF, 0.0f));
            Quad(renderer, sx + sheen / 2, bar_y, sheen / 2, bar_h, Color(0xFFFFFF, 0.45f),
                 Color(0xFFFFFF, 0.0f), Color(0xFFFFFF, 0.0f), Color(0xFFFFFF, 0.45f));
        }
    } else {
        // Unknown total: a gold segment gliding back and forth.
        const float seg = bar_w / 5;
        const float phase = float(0.5 - 0.5 * std::cos(t * 1.6));
        Pill(renderer, x0 + phase * (bar_w - seg), bar_y, seg, bar_h, Color(0xB8862B),
             Color(0xF3D27A));
    }

    std::string detail;
    if (current_total) {
        detail = FormatCount(current_done) + " / " + FormatCount(current_total);
        const std::size_t stage_done = current_done - std::min(current_done, first_done);
        if (stage_seconds > 2.0 && stage_done > 0 && current_done < current_total) {
            const double rate = double(stage_done) / stage_seconds;
            detail += "   ·   " + fmt::format(fmt::runtime(Say("left")),
                                                  FormatSeconds((current_total - current_done) /
                                                                rate));
        }
        detail += "   ·   ";
    }
    detail += fmt::format(fmt::runtime(Say("time")), FormatSeconds(elapsed));
    const Text& info = CachedText(detail, int(15 * s), false, false);
    PutText(info, x0, bar_y + bar_h + 14 * s, Muted);
    const Text& skip = CachedText(Say("skip"), int(15 * s), false, false);
    PutText(skip, x1 - skip.w, bar_y + bar_h + 14 * s, Muted);

    SDL_RenderPresent(renderer);
}

} // namespace ZooT
