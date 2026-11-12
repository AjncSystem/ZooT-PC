// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <atomic>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#pragma comment(lib, "pdh.lib")

#include <SDL3/SDL.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/hle/service/am/applet_manager.h"
#include "video_core/renderer_base.h"
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
constexpr int KeyF8 = SDL_SCANCODE_F8;
constexpr int KeyF10 = SDL_SCANCODE_F10;
constexpr int KeyF11 = SDL_SCANCODE_F11;

// Gold on dark blue, the launcher's palette (zoot_theme.py): the text is gold everywhere.
constexpr unsigned White = 0xFBE6A6;      // strong gold: values, the chosen row
constexpr unsigned Muted = 0xA8935C;      // dim gold: hints, notes, the key help
constexpr unsigned Gold = 0xE7C164;       // accent gold: the title, the chosen value
constexpr unsigned GoldLabel = 0xD9BD6A;  // row names
constexpr unsigned GoldDark = 0x6E5C33;   // arrows of the other rows
constexpr unsigned GoldLine = 0x4A3F24;   // borders and separators

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
    {"upscaler", "Upscaler", "Upscaler", "Escalador", "Upscaler", "アップスケーラー", "업스케일러",
     "Апскейлер"},
    {"game_own", "Do jogo", "Game's own", "El del juego", "Celui du jeu", "ゲーム標準", "게임 기본",
     "Игровой"},
    {"framegen", "Geração de quadros", "Frame generation", "Generación de fotogramas",
     "Génération d’images", "フレーム生成", "프레임 생성", "Генерация кадров"},
    {"perf_pos", "Posição do F10", "F10 panel position", "Posición del F10",
     "Position du F10", "F10 パネルの位置", "F10 패널 위치", "Положение панели F10"},
    {"left", "Esquerda", "Left", "Izquierda", "Gauche", "左", "왼쪽", "Слева"},
    {"center", "Meio", "Center", "Centro", "Centre", "中央", "가운데", "По центру"},
    {"right", "Direita", "Right", "Derecha", "Droite", "右", "오른쪽", "Справа"},
    {"tab_display", "Imagem", "Display", "Imagen", "Image", "画面", "화면", "Изображение"},
    {"tab_perf", "Desempenho", "Performance", "Rendimiento", "Performances", "パフォーマンス",
     "성능", "Производительность"},
    {"tab_audio", "Áudio", "Audio", "Audio", "Audio", "オーディオ", "오디오", "Звук"},
    {"tab_controls", "Controles", "Controls", "Controles", "Commandes", "操作", "조작",
     "Управление"},
    {"mouse_sens", "Sensibilidade do mouse", "Mouse sensitivity", "Sensibilidad del ratón",
     "Sensibilité de la souris", "マウス感度", "마우스 감도", "Чувствительность мыши"},
    {"choose", "Escolher", "Choose", "Elegir", "Choisir", "選択", "선택", "Выбор"},
    {"change", "Mudar", "Change", "Cambiar", "Modifier", "変更", "변경", "Изменить"},
    {"tabs", "Abas", "Tabs", "Pestañas", "Onglets", "タブ", "탭", "Вкладки"},
    {"close", "Fechar", "Close", "Cerrar", "Fermer", "閉じる", "닫기", "Закрыть"},
    {"restart_note", "Vale ao reabrir o jogo", "Applies on the next start",
     "Se aplica al reabrir el juego", "S’applique au prochain lancement", "次回起動時に反映",
     "다음 실행 시 적용", "Применится при следующем запуске"},
    {"h_fps", "Limite de quadros: 60, 30 ou ilimitado. A velocidade do jogo não muda. F7 alterna.",
     "Frame rate cap: 60, 30 or unlimited. The game's speed stays the same. F7 cycles it.",
     "Límite de fotogramas: 60, 30 o ilimitado. La velocidad del juego no cambia. F7 lo alterna.",
     "Limite d’images : 60, 30 ou illimité. La vitesse du jeu ne change pas. F7 l’alterne.",
     "フレームレート上限: 60・30・無制限。ゲーム速度は変わりません。F7 で切替。",
     "프레임 제한: 60, 30, 무제한. 게임 속도는 그대로입니다. F7로 전환.",
     "Ограничение кадров: 60, 30 или без ограничения. Скорость игры не меняется. F7 — переключение."},
    {"h_fullscreen", "Janela ou tela cheia. F11 alterna.", "Window or fullscreen. F11 switches it.",
     "Ventana o pantalla completa. F11 lo alterna.", "Fenêtre ou plein écran. F11 l’alterne.",
     "ウィンドウかフルスクリーン。F11 で切替。", "창 모드 또는 전체 화면. F11로 전환.",
     "Окно или полный экран. F11 — переключение."},
    {"h_filter", "Como a imagem é esticada até a janela. FSR 1 deixa mais nítido.",
     "How the picture is scaled to the window. FSR 1 is the sharpest.",
     "Cómo se escala la imagen a la ventana. FSR 1 es el más nítido.",
     "Comment l’image est mise à l’échelle de la fenêtre. FSR 1 est le plus net.",
     "画面をウィンドウに合わせる方法。FSR 1 が最も鮮明です。",
     "화면을 창 크기에 맞추는 방식. FSR 1이 가장 선명합니다.",
     "Как картинка растягивается до окна. FSR 1 — самый чёткий."},
    {"h_sharp", "Nitidez do FSR (filtro e upscaler). 0% desliga.",
     "FSR sharpening (filter and upscaler). 0% turns it off.",
     "Nitidez de FSR (filtro y escalador). 0% la desactiva.",
     "Netteté FSR (filtre et upscaler). 0 % la désactive.",
     "FSR のシャープネス (フィルターとアップスケーラー)。0% でオフ。",
     "FSR 선명도 (필터와 업스케일러). 0%는 끔.",
     "Резкость FSR (фильтр и апскейлер). 0% — выключено."},
    {"h_aa", "Suaviza o serrilhado das bordas. FXAA é leve; SMAA, mais caprichado.",
     "Smooths jagged edges. FXAA is light; SMAA is finer.",
     "Suaviza los bordes dentados. FXAA es ligero; SMAA, más fino.",
     "Adoucit les bords crénelés. FXAA est léger ; SMAA, plus fin.",
     "ギザギザを滑らかにします。FXAA は軽く、SMAA はより丁寧。",
     "계단 현상을 부드럽게 합니다. FXAA는 가볍고 SMAA는 더 정교합니다.",
     "Сглаживает зубчатые края. FXAA — лёгкое, SMAA — аккуратнее."},
    {"h_aspect", "Formato da imagem. Formatos mais largos esticam o HUD.",
     "Picture shape. Wider shapes stretch the HUD.",
     "Forma de la imagen. Las más anchas estiran el HUD.",
     "Format de l’image. Les formats plus larges étirent le HUD.",
     "画面の形。横長にすると HUD が引き伸ばされます。",
     "화면 비율. 더 넓은 비율은 HUD를 늘립니다.",
     "Форма картинки. Более широкие растягивают интерфейс."},
    {"h_vsync", "Sincroniza com o monitor e evita cortes na imagem. Desligado tem menos atraso.",
     "Syncs with the monitor and avoids tearing. Off has less latency.",
     "Sincroniza con el monitor y evita cortes. Apagado tiene menos retraso.",
     "Synchronise avec l’écran et évite les déchirures. Désactivé a moins de latence.",
     "モニターと同期してティアリングを防ぎます。オフは遅延が少なめ。",
     "모니터와 동기화해 화면 찢김을 막습니다. 끄면 지연이 줄어듭니다.",
     "Синхронизация с монитором без разрывов. Выключено — меньше задержка."},
    {"h_mode", "TV (dock) desenha em resolução maior; Portátil é mais leve.",
     "TV (docked) renders at a higher resolution; Handheld is lighter.",
     "TV (dock) dibuja a mayor resolución; Portátil es más ligero.",
     "TV (station) rend en plus haute résolution ; Portable est plus léger.",
     "TV モードは高解像度、携帯モードは軽め。",
     "TV 모드는 높은 해상도, 휴대 모드는 더 가볍습니다.",
     "ТВ рисует в большем разрешении; портативный — легче."},
    {"h_resolution", "Escala da resolução interna. Mais alta: mais nítida e mais pesada.",
     "Internal resolution scale. Higher: sharper and heavier.",
     "Escala de resolución interna. Más alta: más nítida y más pesada.",
     "Échelle de résolution interne. Plus haute : plus nette et plus lourde.",
     "内部解像度の倍率。高いほど鮮明で重くなります。",
     "내부 해상도 배율. 높을수록 선명하고 무거워집니다.",
     "Масштаб внутреннего разрешения. Выше — чётче и тяжелее."},
    {"h_upscaler", "Quem reconstrói a imagem do jogo (1440p → 4K na TV). FSR 3.1 e DLSS deixam mais nítido.",
     "What rebuilds the game's picture (1440p → 4K docked). FSR 3.1 and DLSS are sharper.",
     "Lo que reconstruye la imagen del juego (1440p → 4K en TV). FSR 3.1 y DLSS son más nítidos.",
     "Ce qui reconstruit l’image du jeu (1440p → 4K en TV). FSR 3.1 et DLSS sont plus nets.",
     "ゲーム画面を再構成する方式 (TV で 1440p → 4K)。FSR 3.1 と DLSS はより鮮明。",
     "게임 화면을 재구성하는 방식 (TV에서 1440p → 4K). FSR 3.1과 DLSS가 더 선명합니다.",
     "Чем восстанавливается картинка (1440p → 4K на ТВ). FSR 3.1 и DLSS — чётче."},
    {"h_framegen", "Um quadro gerado entre dois do jogo: mais fluidez, um pouco mais de atraso. F8 alterna.",
     "One generated frame between two of the game's: smoother, a little more latency. F8 toggles it.",
     "Un fotograma generado entre dos del juego: más fluidez, algo más de retraso. F8 lo alterna.",
     "Une image générée entre deux du jeu : plus fluide, un peu plus de latence. F8 l’alterne.",
     "ゲームの 2 フレームの間に 1 枚生成: より滑らか、遅延は少し増加。F8 で切替。",
     "게임 프레임 사이에 1장 생성: 더 부드럽고 지연이 약간 늘어납니다. F8로 전환.",
     "Один созданный кадр между двумя игровыми: плавнее, чуть больше задержка. F8 — переключение."},
    {"h_perf_pos", "Onde o painel de desempenho (F10) fica no topo da tela.",
     "Where the performance panel (F10) sits at the top of the screen.",
     "Dónde va el panel de rendimiento (F10) en la parte superior.",
     "Où se place le panneau de performances (F10) en haut de l’écran.",
     "パフォーマンス表示 (F10) の画面上部での位置。",
     "성능 패널(F10)이 화면 위쪽 어디에 놓일지.",
     "Где панель производительности (F10) стоит вверху экрана."},
    {"h_volume", "Volume do jogo.", "Game volume.", "Volumen del juego.", "Volume du jeu.",
     "ゲームの音量。", "게임 음량.", "Громкость игры."},
    {"h_mouse_sens", "Velocidade da câmera quando o mouse move a câmera (Controles do lançador).",
     "Camera speed when the mouse turns the camera (launcher Controls).",
     "Velocidad de la cámara cuando el ratón la mueve (Controles del lanzador).",
     "Vitesse de la caméra quand la souris la tourne (Commandes du lanceur).",
     "マウスでカメラを動かすときの速さ (ランチャーの操作設定)。",
     "마우스로 카메라를 움직일 때의 속도 (런처의 조작 설정).",
     "Скорость камеры, когда ею управляет мышь (Управление в лаунчере)."},
    {"soft", "suave", "soft", "suave", "doux", "ソフト", "부드럽게", "мягкий"},
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

// The upscaler in place of Ocarina's own, as the launcher stores it ("upscaler" in launcher.json).
enum Upscaler { UpscalerGame = 0, UpscalerFsr31 = 1, UpscalerFsr4 = 2, UpscalerDlss = 3 };

Upscaler CurrentUpscaler() {
    using Settings::values;
    if (!values.ocarina_fsr.GetValue()) {
        return UpscalerGame;
    }
    return values.nvidia_dlss.GetValue() ? UpscalerDlss
           : values.amd_fsr4.GetValue()  ? UpscalerFsr4
                                         : UpscalerFsr31;
}

void ApplyUpscaler(int v) {
    using Settings::values;
    values.ocarina_fsr.SetValue(v != UpscalerGame);
    values.amd_fsr4.SetValue(v == UpscalerFsr4);
    values.nvidia_dlss.SetValue(v == UpscalerDlss);
}

const char* UpscalerKey(int v) {
    return v == UpscalerFsr31 ? "fsr31" : v == UpscalerFsr4 ? "fsr4" : v == UpscalerDlss ? "dlss" : "game";
}

/// The F10 panel's numbers, the same counters Task Manager shows (PDH, by English counter name
/// so they work on any Windows language), sampled once a second on a thread of its own: CPU and
/// GPU (3D engine) use of this process, its video memory, its RAM, the PC's RAM in use. Nothing
/// of DXGI: a DXGI query beside Streamline's DXGI hooks crashed the game (0.13).
class PerfSampler {
public:
    std::atomic<double> cpu{-1}, gpu{-1}, vram{-1}, vram_total{0}, ram{0}, ram_used{0},
        ram_total{0};

    PerfSampler() {
        vram_total.store(DedicatedVideoMemory());
        thread = std::jthread([this](std::stop_token stop) { Run(stop); });
    }

private:
    static double DedicatedVideoMemory() {
        // The largest card's memory, as the driver writes it in the registry (like the launcher).
        double best = 0;
        HKEY cls{};
        const wchar_t* base =
            L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, base, 0, KEY_READ, &cls) != ERROR_SUCCESS) {
            return 0;
        }
        for (DWORD i = 0; i < 64; ++i) {
            wchar_t name[256];
            DWORD len = 256;
            if (RegEnumKeyExW(cls, i, name, &len, nullptr, nullptr, nullptr, nullptr) !=
                ERROR_SUCCESS) {
                break;
            }
            unsigned long long size = 0;
            DWORD bytes = sizeof(size);
            if (RegGetValueW(cls, name, L"HardwareInformation.qwMemorySize", RRF_RT_QWORD,
                             nullptr, &size, &bytes) == ERROR_SUCCESS) {
                best = std::max(best, double(size));
            }
        }
        RegCloseKey(cls);
        return best;
    }

    static double SumCounter(PDH_HCOUNTER counter, bool largest = false) {
        DWORD size = 0, count = 0;
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, nullptr) !=
                static_cast<PDH_STATUS>(PDH_MORE_DATA) ||
            size == 0) {
            return -1;
        }
        std::vector<unsigned char> buffer(size);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, items) !=
            ERROR_SUCCESS) {
            return -1;
        }
        double sum = 0;
        for (DWORD i = 0; i < count; ++i) {
            if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA ||
                items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA) {
                sum = largest ? std::max(sum, items[i].FmtValue.doubleValue)
                              : sum + items[i].FmtValue.doubleValue;
            }
        }
        return sum;
    }

    void Run(std::stop_token stop) {
        Common::SetCurrentThreadName("ZootPerfSampler");
        const DWORD pid = GetCurrentProcessId();
        PDH_HQUERY query{};
        PDH_HCOUNTER gpu_counter{}, vram_counter{};
        if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS) {
            const std::wstring gpu_path =
                L"\\GPU Engine(pid_" + std::to_wstring(pid) + L"_*engtype_3D)\\Utilization Percentage";
            // The card's whole video memory in use (Task Manager's Performance tab): NVIDIA's
            // Vulkan allocations mostly fall outside the per-process count.
            const std::wstring vram_path = L"\\GPU Adapter Memory(*)\\Dedicated Usage";
            if (PdhAddEnglishCounterW(query, gpu_path.c_str(), 0, &gpu_counter) != ERROR_SUCCESS) {
                gpu_counter = nullptr;
            }
            if (PdhAddEnglishCounterW(query, vram_path.c_str(), 0, &vram_counter) !=
                ERROR_SUCCESS) {
                vram_counter = nullptr;
            }
            PdhCollectQueryData(query);
        }
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        const double cores = std::max<DWORD>(1, info.dwNumberOfProcessors);
        ULARGE_INTEGER last_cpu{}, last_wall{};
        const auto cpu_time = [] {
            FILETIME create, exit, kernel, user;
            ULARGE_INTEGER total{};
            if (GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user)) {
                ULARGE_INTEGER k{{kernel.dwLowDateTime, kernel.dwHighDateTime}};
                ULARGE_INTEGER u{{user.dwLowDateTime, user.dwHighDateTime}};
                total.QuadPart = k.QuadPart + u.QuadPart;
            }
            return total;
        };
        const auto wall_time = [] {
            FILETIME now;
            GetSystemTimeAsFileTime(&now);
            return ULARGE_INTEGER{{now.dwLowDateTime, now.dwHighDateTime}};
        };
        last_cpu = cpu_time();
        last_wall = wall_time();
        while (!stop.stop_requested()) {
            for (int i = 0; i < 10 && !stop.stop_requested(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds{100});
            }
            const auto c = cpu_time();
            const auto w = wall_time();
            if (w.QuadPart > last_wall.QuadPart) {
                cpu.store(100.0 * double(c.QuadPart - last_cpu.QuadPart) /
                          (double(w.QuadPart - last_wall.QuadPart) * cores));
            }
            last_cpu = c;
            last_wall = w;
            if (query && PdhCollectQueryData(query) == ERROR_SUCCESS) {
                if (gpu_counter) {
                    const double g = SumCounter(gpu_counter);
                    gpu.store(g < 0 ? -1 : std::min(100.0, g));
                }
                if (vram_counter) {
                    vram.store(SumCounter(vram_counter, true));
                }
            }
            PROCESS_MEMORY_COUNTERS pmc{};
            pmc.cb = sizeof(pmc);
            if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
                ram.store(double(pmc.WorkingSetSize));
            }
            MEMORYSTATUSEX ms{};
            ms.dwLength = sizeof(ms);
            if (GlobalMemoryStatusEx(&ms)) {
                ram_total.store(double(ms.ullTotalPhys));
                ram_used.store(double(ms.ullTotalPhys - ms.ullAvailPhys));
            }
        }
        if (query) {
            PdhCloseQuery(query);
        }
    }

    std::jthread thread;
};

PerfSampler& Sampler() {
    static PerfSampler sampler;
    return sampler;
}

} // Anonymous namespace

Overlay::Overlay(Core::System& system_, SDL_Window* game_, std::function<void()> relayout_,
                 std::function<std::pair<double, double>()> stats_)
    : system{system_}, game{game_}, relayout{std::move(relayout_)}, stats{std::move(stats_)} {
    RegisterFonts();
    // Where the F10 panel goes (top left, top center or top right), as last chosen in F1.
    const auto path = Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir) / "zoot" /
                      "launcher.json";
    if (std::ifstream in{path}) {
        const auto data = nlohmann::json::parse(in, nullptr, false);
        if (data.is_object() && data.contains("perf_position") &&
            data["perf_position"].is_number_integer()) {
            perf_position = std::clamp(data["perf_position"].get<int>(), 0, 2);
        }
    }
    BuildItems();
}

Overlay::~Overlay() {
    FreePanel(menu);
    FreePanel(notice);
    FreePanel(perf);
}

std::vector<int> Overlay::TabRows() const {
    std::vector<int> rows;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].tab == tab) {
            rows.push_back(int(i));
        }
    }
    return rows;
}

void Overlay::BuildItems() {
    using namespace Settings;
    items.clear();
    enum { TabDisplay, TabPerf, TabAudio, TabControls };
    const auto tag = [this](int tab_index, const char* help) {
        items.back().tab = tab_index;
        items.back().help = Say(help);
    };
    items.push_back({Say("fps"),
                     {{"60", Fps60}, {"30", Fps30}, {Say("unlimited"), FpsUnlimited}},
                     [] { return int(CurrentFps()); },
                     [](int v) { ApplyFps(v); }});
    tag(TabPerf, "h_fps");
    items.push_back({Say("fullscreen"),
                     {{Say("window"), 0}, {Say("fullscreen"), 1}},
                     [this] {
                         return (SDL_GetWindowFlags(game) & SDL_WINDOW_FULLSCREEN) ? 1 : 0;
                     },
                     [this](int v) { SetFullscreen(v != 0); }});
    tag(TabDisplay, "h_fullscreen");
    items.push_back({Say("filter"),
                     {{"Bilinear", int(ScalingFilter::Bilinear)},
                      {"Bicubic", int(ScalingFilter::Bicubic)},
                      {"ScaleForce", int(ScalingFilter::ScaleForce)},
                      {"AMD FSR 1", int(ScalingFilter::Fsr)},
                      {"AMD FSR 1 (" + Say("soft") + ")", int(ScalingFilter::Fsr3)},
                      {"Lanczos", int(ScalingFilter::Lanczos)},
                      {"Area", int(ScalingFilter::Area)}},
                     [] { return int(values.scaling_filter.GetValue()); },
                     [](int v) { values.scaling_filter.SetValue(ScalingFilter(v)); }});
    tag(TabDisplay, "h_filter");
    std::vector<std::pair<std::string, int>> sharp;
    for (int v = 0; v <= 200; v += 25) {
        sharp.emplace_back(fmt::format("{}%", v), v);
    }
    items.push_back({Say("sharp"), sharp,
                     [] { return values.fsr_sharpening_slider.GetValue(); },
                     [](int v) { values.fsr_sharpening_slider.SetValue(v); }});
    tag(TabDisplay, "h_sharp");
    items.push_back({Say("aa"),
                     {{Say("none"), int(AntiAliasing::None)},
                      {"FXAA", int(AntiAliasing::Fxaa)},
                      {"SMAA", int(AntiAliasing::Smaa)}},
                     [] { return int(values.anti_aliasing.GetValue()); },
                     [](int v) { values.anti_aliasing.SetValue(AntiAliasing(v)); }});
    tag(TabDisplay, "h_aa");
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
    tag(TabDisplay, "h_aspect");
    items.push_back({Say("vsync"),
                     {{Say("off"), int(VSyncMode::Immediate)},
                      {"Mailbox", int(VSyncMode::Mailbox)},
                      {Say("on") + " (FIFO)", int(VSyncMode::Fifo)},
                      {Say("relaxed"), int(VSyncMode::FifoRelaxed)}},
                     [] { return int(values.vsync_mode.GetValue()); },
                     [](int v) { values.vsync_mode.SetValue(VSyncMode(v)); }});
    tag(TabDisplay, "h_vsync");
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
    tag(TabDisplay, "h_mode");
    std::vector<std::pair<std::string, int>> volume;
    for (int v = 0; v <= 100; v += 10) {
        volume.emplace_back(fmt::format("{}%", v), v);
    }
    items.push_back({Say("volume"), volume, [] { return int(values.volume.GetValue()); },
                     [](int v) { values.volume.SetValue(static_cast<u8>(v)); }});
    tag(TabAudio, "h_volume");
    std::vector<std::pair<std::string, int>> sens;
    for (const int v : {20, 35, 50, 65, 80, 100}) {
        sens.emplace_back(fmt::format("{}%", v), v);
    }
    items.push_back({Say("mouse_sens"), sens,
                     [] { return int(values.mouse_panning_x_sensitivity.GetValue()); },
                     [](int v) {
                         values.mouse_panning_x_sensitivity.SetValue(static_cast<u8>(v));
                         values.mouse_panning_y_sensitivity.SetValue(static_cast<u8>(v));
                     }});
    tag(TabControls, "h_mouse_sens");
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
    tag(TabDisplay, "h_resolution");
    // What replaces Ocarina's own upscaler, read when the game starts. FSR 4 and DLSS are offered
    // by the launcher where the PC has them; here they stay listed when already chosen.
    std::vector<std::pair<std::string, int>> upscalers{{Say("game_own"), UpscalerGame},
                                                       {"AMD FSR 3.1", UpscalerFsr31}};
    if (CurrentUpscaler() == UpscalerFsr4) {
        upscalers.emplace_back("AMD FSR 4", UpscalerFsr4);
    }
    if (CurrentUpscaler() == UpscalerDlss) {
        upscalers.emplace_back("NVIDIA DLSS", UpscalerDlss);
    }
    items.push_back({Say("upscaler"), upscalers, [] { return int(CurrentUpscaler()); },
                     [](int v) { ApplyUpscaler(v); }, true});
    tag(TabDisplay, "h_upscaler");
    items.push_back({Say("perf_pos"),
                     {{Say("left"), 0}, {Say("center"), 1}, {Say("right"), 2}},
                     [this] { return perf_position; },
                     [this](int v) {
                         perf_position = v;
                         if (perf_open && perf.window) {
                             PlaceCorner(perf.window);
                         }
                     }});
    tag(TabPerf, "h_perf_pos");
    items.push_back({Say("framegen"),
                     {{Say("off"), 0}, {"2x", 1}},
                     [] { return values.frame_gen.GetValue() ? 1 : 0; },
                     [](int v) {
                         values.frame_gen.SetValue(v != 0);
                         values.frame_gen_type.SetValue(FrameGenType::Fsr3);
                     }});
    tag(TabPerf, "h_framegen");
    // Display rows in the order players look for them.
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.tab < b.tab;
    });
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
    if (scancode == KeyF8) {
        if (down) {
            ToggleFrameGen();
        }
        return true;
    }
    if (scancode == KeyF10) {
        if (down) {
            TogglePerf();
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
        case SDL_SCANCODE_Q:
        case SDL_SCANCODE_E:
        case SDL_SCANCODE_TAB: {
            tab_selected[std::size_t(tab)] = selected;
            const int step = scancode == SDL_SCANCODE_Q ? 3 : 1;
            tab = (tab + step) % 4;
            selected = tab_selected[std::size_t(tab)];
            break;
        }
        case SDL_SCANCODE_UP:
        case SDL_SCANCODE_DOWN: {
            const int n = std::max<int>(1, int(TabRows().size()));
            selected = (selected + (scancode == SDL_SCANCODE_UP ? n - 1 : 1)) % n;
            break;
        }
        case SDL_SCANCODE_LEFT:
        case SDL_SCANCODE_RIGHT:
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_SPACE: {
            const auto rows = TabRows();
            if (!rows.empty()) {
                Step(rows[std::size_t(std::clamp(selected, 0, int(rows.size()) - 1))],
                     scancode == SDL_SCANCODE_LEFT ? -1 : +1);
            }
            break;
        }
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

void Overlay::SetChoice(int index, int choice) {
    Item& item = items[std::size_t(index)];
    if (choice < 0 || choice >= int(item.choices.size())) {
        return;
    }
    item.set(item.choices[std::size_t(choice)].second);
    LOG_INFO(Frontend, "ZooT menu: {} -> {}", item.label, item.choices[std::size_t(choice)].first);
    SaveToLauncher();
    dirty = true;
}

bool Overlay::HandleMouse(const SDL_Event& event) {
    if (!menu_open || !menu.window) {
        return false;
    }
    const SDL_WindowID id = SDL_GetWindowID(menu.window);
    SDL_WindowID event_window = 0;
    float mx = 0, my = 0;
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        event_window = event.motion.windowID;
        mx = event.motion.x;
        my = event.motion.y;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        event_window = event.button.windowID;
        mx = event.button.x;
        my = event.button.y;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        event_window = event.wheel.windowID;
        break;
    default:
        return false;
    }
    if (event_window != id) {
        // While the menu is open the game gets no mouse input either.
        return true;
    }
    // Window coordinates -> the render pixels the hits were recorded in.
    int ww = 0, wh = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(menu.window, &ww, &wh);
    SDL_GetCurrentRenderOutputSize(menu.renderer, &pw, &ph);
    if (ww > 0 && wh > 0) {
        mx *= float(pw) / float(ww);
        my *= float(ph) / float(wh);
    }
    const auto rows = TabRows();
    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        // The wheel moves the choice, never a value.
        const int n = std::max<int>(1, int(rows.size()));
        selected = (selected + (event.wheel.y > 0 ? n - 1 : 1)) % n;
        dirty = true;
        return true;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT) {
        OpenMenu(false);
        return true;
    }
    const Hit* hit = nullptr;
    for (const Hit& h : hits) {
        if (mx >= h.x && mx < h.x + h.w && my >= h.y && my < h.y + h.h) {
            hit = &h;
            break;
        }
    }
    if (hit == nullptr) {
        return true;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        // Pointing at a row chooses it (its help shows), as the arrows do.
        if (hit->kind != HitKind::Tab && hit->a != selected) {
            selected = hit->a;
            dirty = true;
        }
        return true;
    }
    if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || event.button.button != SDL_BUTTON_LEFT) {
        return true;
    }
    switch (hit->kind) {
    case HitKind::Tab:
        tab_selected[std::size_t(tab)] = selected;
        tab = hit->a;
        selected = tab_selected[std::size_t(tab)];
        break;
    case HitKind::Row:
        selected = hit->a;
        break;
    case HitKind::Less:
    case HitKind::More:
        selected = hit->a;
        if (hit->a < int(rows.size())) {
            Step(rows[std::size_t(hit->a)], hit->kind == HitKind::Less ? -1 : +1);
        }
        break;
    case HitKind::Choice:
        selected = hit->a;
        if (hit->a < int(rows.size())) {
            SetChoice(rows[std::size_t(hit->a)], hit->b);
        }
        break;
    }
    dirty = true;
    return true;
}

void Overlay::CycleFps() {
    const int next = CurrentFps() == Fps60 ? Fps30 : CurrentFps() == Fps30 ? FpsUnlimited : Fps60;
    ApplyFps(next);
    LOG_INFO(Frontend, "ZooT: FPS mode {}", FpsName(next));
    SaveToLauncher();
    Notice(fmt::format(fmt::runtime(Say("fps_notice")), FpsName(next)));
    dirty = true;
}

void Overlay::ToggleFrameGen() {
    using Settings::values;
    const bool on = !values.frame_gen.GetValue();
    values.frame_gen_type.SetValue(Settings::FrameGenType::Fsr3);
    values.frame_gen.SetValue(on);
    LOG_INFO(Frontend, "ZooT: frame generation {}", on ? "2x" : "off");
    SaveToLauncher();
    Notice(Say("framegen") + ": " + (on ? std::string("2x") : Say("off")));
    dirty = true;
}

void Overlay::TogglePerf() {
    perf_open = !perf_open;
    LOG_INFO(Frontend, "ZooT: performance panel {}", perf_open ? "on" : "off");
    if (perf_open) {
        if (!perf.window) {
            if (!MakePanel(perf, 270, 146)) {
                perf_open = false;
                return;
            }
            // See-through: the whole panel at 55% (a layered window, which Windows composites
            // reliably over the game's Vulkan output, unlike per-pixel transparency).
            SDL_SetWindowOpacity(perf.window, 0.55f);
        }
        perf_count = 0;
        perf_next = {};
        SDL_ShowWindow(perf.window);
        SDL_RaiseWindow(game);
    } else if (perf.window) {
        SDL_HideWindow(perf.window);
    }
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
    data["perf_position"] = perf_position;
    data["mouse_sens"] = int(values.mouse_panning_x_sensitivity.GetValue());
    data["resolution"] = int(values.resolution_setup.GetValue());
    data["filter"] = int(values.scaling_filter.GetValue());
    data["sharpness"] = values.fsr_sharpening_slider.GetValue();
    data["aa"] = int(values.anti_aliasing.GetValue());
    data["aspect"] = int(values.aspect_ratio.GetValue());
    data["vsync"] = int(values.vsync_mode.GetValue());
    data["mode"] = int(values.use_docked_mode.GetValue());
    data["volume"] = int(values.volume.GetValue());
    data["fullscreen"] = (SDL_GetWindowFlags(game) & SDL_WINDOW_FULLSCREEN) != 0;
    data["upscaler"] = UpscalerKey(CurrentUpscaler());
    data["framegen"] = values.frame_gen.GetValue() ? 2 : 0;
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
        // The menu covers the game window (followed in DrawMenu), the game faintly behind it.
        if (!menu.window) {
            if (!MakePanel(menu, 1280, 720)) {
                menu_open = false;
                return;
            }
            SDL_SetWindowOpacity(menu.window, 0.93f);
        }
        // The keyboard stays with the game window: its key events reach HandleKey.
        SDL_ShowWindow(menu.window);
        SDL_RaiseWindow(game);
        dirty = true;
    } else if (menu.window) {
        SDL_HideWindow(menu.window);
        SDL_RaiseWindow(game);
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

void Overlay::PlaceCorner(SDL_Window* window) {
    int gx = 0, gy = 0, gw = 0, gh = 0, w = 0, h = 0, cx = 0, cy = 0;
    SDL_GetWindowPosition(game, &gx, &gy);
    SDL_GetWindowSize(game, &gw, &gh);
    SDL_GetWindowSize(window, &w, &h);
    const int margin = int(12 * DisplayScale(game));
    // Top left, top center or top right of the game window (F1 → F10 panel position).
    const int x = perf_position == 1   ? gx + (gw - w) / 2
                  : perf_position == 2 ? gx + gw - w - margin
                                       : gx + margin;
    const int y = gy + margin;
    SDL_GetWindowPosition(window, &cx, &cy);
    if (cx != x || cy != y) {
        SDL_SetWindowPosition(window, x, y);
    }
}

void Overlay::Tick() {
    const auto now = std::chrono::steady_clock::now();
    const bool minimized = (SDL_GetWindowFlags(game) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN));
    if (perf_open && perf.window) {
        const bool shown = !(SDL_GetWindowFlags(perf.window) & SDL_WINDOW_HIDDEN);
        if (minimized == shown) {
            minimized ? SDL_HideWindow(perf.window) : SDL_ShowWindow(perf.window);
        }
        if (!minimized && now >= perf_next) {
            perf_next = now + std::chrono::milliseconds{500};
            perf_ms[perf_count % perf_ms.size()] = float(stats ? stats().second : 0.0);
            ++perf_count;
            // Frames made by frame generation are not game frames: counted from the renderer.
            perf_generated_fps = 0.0;
            perf_fg_running = false;
            if (system.IsPoweredOn()) {
                const auto fg = system.Renderer().GetFrameGenStatus();
                perf_fg_running = fg.state == VideoCore::FrameGenState::Running;
                const double dt = std::chrono::duration<double>(now - perf_last).count();
                if (perf_count > 1 && dt > 0.1 && fg.generated_frames >= perf_generated) {
                    perf_generated_fps = double(fg.generated_frames - perf_generated) / dt;
                }
                perf_generated = fg.generated_frames;
            }
            perf_last = now;
            DrawPerf();
        } else if (!minimized) {
            PlaceCorner(perf.window);
        }
    }
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

void Overlay::DrawPerf() {
    PlaceCorner(perf.window);
    int pw = 0, ph = 0;
    SDL_GetCurrentRenderOutputSize(perf.renderer, &pw, &ph);
    const float s = float(pw) / 270.0f;
    SDL_Renderer* r = perf.renderer;
    const float W = float(pw), H = float(ph);
    Fill(r, 0, 0, W, H, 0x0B1222);
    Fill(r, 0, 0, 3 * s, H, Gold);
    const auto [game_fps, ms] = stats ? stats() : std::pair{0.0, 0.0};
    // With frame generation the screen shows the game's frames plus the generated ones.
    const double fps = game_fps + perf_generated_fps;
    const Text& big = GetText(perf, fmt::format("{:.0f} FPS", fps), int(24 * s), true, false);
    Put(perf, big, 14 * s, 8 * s, fps >= 55.0 ? Gold : fps >= 30.0 ? 0xF0C060u : 0xEF5350u);
    std::string info = fmt::format("{:.1f} ms", ms);
    if (perf_fg_running) {
        info = fmt::format("{:.0f} + FG {:.0f}", game_fps, perf_generated_fps);
    } else if (Settings::values.frame_gen.GetValue()) {
        info += "  \u00b7  FG\u2026";
    }
    const Text& detail = GetText(perf, info, int(13 * s), false, false);
    Put(perf, detail, 16 * s + big.w, 8 * s + big.h - detail.h - 3 * s, Muted);  // "small" is a macro in rpcndr.h
    // CPU and GPU use of the game, its video memory and RAM, and the PC's RAM in use; red near
    // the limit.
    constexpr double GB = 1024.0 * 1024.0 * 1024.0;
    PerfSampler& m = Sampler();
    const auto pct = [](double v) {
        return v < 0 ? std::string("--") : fmt::format("{:.0f}%", v);
    };
    const double cpu = m.cpu.load(), gpu = m.gpu.load();
    const Text& cg = GetText(perf, fmt::format("CPU {}    GPU {}", pct(cpu), pct(gpu)),
                             int(12 * s), false, false);
    Put(perf, cg, 16 * s, 42 * s, gpu > 97.0 ? 0xF0C060u : GoldLabel);
    const double vram = m.vram.load(), vram_total = m.vram_total.load();
    std::string mem;
    if (vram >= 0) {
        mem = vram_total > 0 ? fmt::format("VRAM {:.1f}/{:.0f} GB", vram / GB, vram_total / GB)
                             : fmt::format("VRAM {:.1f} GB", vram / GB);
    }
    const double ram_total = m.ram_total.load(), ram_used = m.ram_used.load();
    if (ram_total > 0) {
        mem += fmt::format("{}RAM {:.1f} GB  (PC {:.0f}/{:.0f})", mem.empty() ? "" : "   ",
                           m.ram.load() / GB, ram_used / GB, ram_total / GB);
    }
    const bool tight = (vram_total > 0 && vram > 0.9 * vram_total) ||
                       (ram_total > 0 && ram_used > 0.9 * ram_total);
    const Text& mt = GetText(perf, mem, int(12 * s), false, false);
    Put(perf, mt, 16 * s, 62 * s, tight ? 0xEF5350u : GoldLabel);
    // Frame time graph: the last 60 half-second samples, 16.7 ms (60 FPS) as the guide line.
    const float gx = 14 * s, gy = 88 * s, gw = W - 28 * s, gh = H - 98 * s;
    Fill(r, gx, gy, gw, gh, 0x111C33);
    const float scale_ms = 50.0f;
    const float guide = gy + gh - gh * (16.7f / scale_ms);
    Fill(r, gx, guide, gw, 1 * s, GoldLine);
    const std::size_t n = std::min(perf_count, perf_ms.size());
    const float bar = gw / float(perf_ms.size());
    for (std::size_t i = 0; i < n; ++i) {
        const float v = perf_ms[(perf_count - n + i) % perf_ms.size()];
        const float bh = std::clamp(v / scale_ms, 0.02f, 1.0f) * gh;
        const float x = gx + gw - float(n - i) * bar;
        Fill(r, x, gy + gh - bh, std::max(1.0f, bar - 1 * s), bh,
             v <= 17.5f ? Gold : v <= 34.0f ? 0xF0C060u : 0xEF5350u, 0.9f);
    }
    SDL_RenderPresent(r);
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
    Fill(r, 0, float(ph) - 1, float(pw), 1, GoldLine);
    const Text& t = GetText(notice, notice_text, int(22 * s), true, false);
    Put(notice, t, (float(pw) - t.w) / 2, (float(ph) - t.h) / 2, Gold);
    SDL_RenderPresent(r);
}

void Overlay::DrawMenu() {
    // Over the whole game window, like Hookshot's menu: a parchment tab bar, rows in dark boxes
    // with their value between arrows, the chosen value in a gold box, a help panel on the right
    // and the keys at the bottom. All text is gold.
    int gx = 0, gy = 0, gw = 0, gh = 0, mw = 0, mh = 0;
    SDL_GetWindowPosition(game, &gx, &gy);
    SDL_GetWindowSize(game, &gw, &gh);
    SDL_GetWindowSize(menu.window, &mw, &mh);
    if (gw > 0 && gh > 0 && (mw != gw || mh != gh)) {
        SDL_SetWindowSize(menu.window, gw, gh);
    }
    int cx = 0, cy = 0;
    SDL_GetWindowPosition(menu.window, &cx, &cy);
    if (cx != gx || cy != gy) {
        SDL_SetWindowPosition(menu.window, gx, gy);
    }
    int pw = 0, ph = 0;
    SDL_GetCurrentRenderOutputSize(menu.renderer, &pw, &ph);
    const float W = float(pw), H = float(ph);
    // Laid out for 1280x720 and scaled to the window.
    const float s = std::max(0.5f, std::min(W / 1280.0f, H / 720.0f));
    SDL_Renderer* r = menu.renderer;

    constexpr unsigned Night = 0x120C08;      // behind everything
    constexpr unsigned Parchment = 0xE8DCC0;  // tab bar
    constexpr unsigned ParchLine = 0xB9A57E;
    constexpr unsigned Ink = 0x3B2A1A;        // text on parchment
    constexpr unsigned Brown = 0x4A2F18;      // active tab
    constexpr unsigned RowBox = 0x2A1C10;
    constexpr unsigned ValueBox = 0x1C130B;
    constexpr unsigned GoldText = 0xE9C878;   // labels and values
    constexpr unsigned GoldBright = 0xFBE3A0;
    constexpr unsigned GoldBox = 0xE3A13A;    // the chosen value
    constexpr unsigned GoldEdge = 0xFFE09A;
    constexpr unsigned Dim = 0x9C8558;

    Fill(r, 0, 0, W, H, Night);

    hits.clear();
    // Tab bar.
    const float bar_h = 50 * s;
    Fill(r, 0, 0, W, bar_h, Parchment);
    Fill(r, 0, bar_h - 2 * s, W, 2 * s, ParchLine);
    const Text& version = GetText(menu, ZootBaseTitle(), int(13 * s), false, false);
    Put(menu, version, 18 * s, (bar_h - version.h) / 2, 0x6B5638);
    const std::array<std::string, 4> tab_names{Say("tab_display"), Say("tab_perf"),
                                                Say("tab_audio"), Say("tab_controls")};
    const auto badge = [&](const std::string& key, float x, float y, float size, unsigned bg,
                           unsigned fg) {
        Fill(r, x, y, size, size, bg);
        const Text& t = GetText(menu, key, int(size * 0.62f), true, false);
        Put(menu, t, x + (size - t.w) / 2, y + (size - t.h) / 2, fg);
        return size;
    };
    float tx = std::max(18 * s + version.w + 40 * s, W * 0.24f);
    const float bsize = 22 * s;
    tx += badge("Q", tx, (bar_h - bsize) / 2, bsize, RowBox, GoldBright) + 18 * s;
    for (int i = 0; i < 4; ++i) {
        const bool on = i == tab;
        const Text& t = GetText(menu, tab_names[std::size_t(i)], int(18 * s), true, false);
        const float tw = t.w + 44 * s;
        if (on) {
            Fill(r, tx, 0, tw, bar_h - 2 * s, Brown);
        }
        hits.push_back({tx, 0, tw, bar_h, HitKind::Tab, i, 0});
        Put(menu, t, tx + 22 * s, (bar_h - t.h) / 2, on ? GoldBright : Ink);
        tx += tw;
    }
    badge("E", tx + 18 * s, (bar_h - bsize) / 2, bsize, RowBox, GoldBright);

    // Rows of the current tab.
    const auto rows = TabRows();
    selected = rows.empty() ? 0 : std::clamp(selected, 0, int(rows.size()) - 1);
    const float left = 60 * s, label_w = 330 * s, value_w = 280 * s, gap = 8 * s;
    const float row_h = 40 * s, row_gap = 7 * s;
    float y = bar_h + 26 * s;
    for (std::size_t n = 0; n < rows.size(); ++n) {
        const Item& item = items[std::size_t(rows[n])];
        const bool on = int(n) == selected;
        Fill(r, left, y, label_w, row_h, RowBox, 0.92f);
        hits.push_back({left, y, label_w, row_h, HitKind::Row, int(n), 0});
        if (on) {
            Fill(r, left, y, 4 * s, row_h, GoldBox);
        }
        const Text& label = GetText(menu, item.label, int(17 * s), on, false);
        Put(menu, label, left + 16 * s, y + (row_h - label.h) / 2, on ? GoldBright : GoldText);

        const float vx = left + label_w + gap;
        const int now = item.get();
        if (item.choices.size() == 2) {
            // Two choices: both shown, the current one lit.
            const float half = value_w / 2;
            for (std::size_t c = 0; c < 2; ++c) {
                const bool lit = item.choices[c].second == now;
                const float x = vx + half * float(c);
                hits.push_back({x, y, half, row_h, HitKind::Choice, int(n), int(c)});
                Fill(r, x, y, half - (c == 0 ? 2 * s : 0), row_h,
                     lit ? (on ? GoldBox : 0x5A4228) : ValueBox, 0.95f);
                const Text& t = GetText(menu, item.choices[c].first, int(16 * s), lit, false);
                Put(menu, t, x + (half - t.w) / 2, y + (row_h - t.h) / 2,
                    lit ? (on ? RowBox : GoldBright) : Dim);
            }
            if (on) {
                Fill(r, vx - 2 * s, y - 2 * s, value_w + 4 * s, 2 * s, GoldEdge);
                Fill(r, vx - 2 * s, y + row_h, value_w + 4 * s, 2 * s, GoldEdge);
            }
        } else {
            hits.push_back({vx, y, value_w / 2, row_h, HitKind::Less, int(n), 0});
            hits.push_back({vx + value_w / 2, y, value_w / 2, row_h, HitKind::More, int(n), 0});
            std::string value = "?";
            for (const auto& c : item.choices) {
                if (c.second == now) {
                    value = c.first;
                }
            }
            if (on) {
                Fill(r, vx - 2 * s, y - 2 * s, value_w + 4 * s, row_h + 4 * s, GoldEdge);
            }
            Fill(r, vx, y, value_w, row_h, on ? GoldBox : ValueBox, 0.95f);
            const unsigned ink = on ? RowBox : GoldText;
            const Text& val = GetText(menu, value, int(16 * s), true, false);
            Put(menu, val, vx + (value_w - val.w) / 2, y + (row_h - val.h) / 2, ink);
            const Text& la = GetText(menu, "◀", int(11 * s), false, false);
            const Text& ra = GetText(menu, "▶", int(11 * s), false, false);
            Put(menu, la, vx + 12 * s, y + (row_h - la.h) / 2, on ? RowBox : Dim);
            Put(menu, ra, vx + value_w - 12 * s - ra.w, y + (row_h - ra.h) / 2, on ? RowBox : Dim);
        }
        y += row_h + row_gap;
    }

    // Help for the chosen row.
    if (!rows.empty()) {
        const Item& item = items[std::size_t(rows[std::size_t(selected)])];
        const float hx = left + label_w + gap + value_w + 50 * s;
        const float hw = std::max(200 * s, W - hx - 50 * s);
        const float hy = bar_h + 26 * s;
        const float pad = 18 * s;
        // Word wrap the help to the panel's width.
        std::vector<std::string> lines;
        {
            std::string line, word;
            std::istringstream words{item.help};
            const float max_w = hw - 2 * pad;
            while (words >> word) {
                const std::string candidate = line.empty() ? word : line + " " + word;
                if (!line.empty() &&
                    GetText(menu, candidate, int(15 * s), false, false).w > max_w) {
                    lines.push_back(line);
                    line = word;
                } else {
                    line = candidate;
                }
            }
            if (!line.empty()) {
                lines.push_back(line);
            }
        }
        const Text& title = GetText(menu, item.label, int(19 * s), true, false);
        const float line_h = 22 * s;
        const float note_h = item.next_start ? 44 * s : 0;
        const float hh = pad + title.h + 10 * s + float(lines.size()) * line_h + note_h + pad;
        Fill(r, hx, hy, hw, hh, RowBox, 0.94f);
        Put(menu, title, hx + pad, hy + pad, GoldBright);
        float ly = hy + pad + title.h + 10 * s;
        for (const auto& l : lines) {
            Put(menu, GetText(menu, l, int(15 * s), false, false), hx + pad, ly, GoldText);
            ly += line_h;
        }
        if (item.next_start) {
            Fill(r, hx + pad, ly + 10 * s, hw - 2 * pad, 1 * s, 0x5A4630);
            const Text& note =
                GetText(menu, "◆  " + Say("restart_note"), int(14 * s), true, false);
            Put(menu, note, hx + pad, ly + 20 * s, GoldBox);
        }
    }

    // Keys, bottom right.
    {
        const float ky = H - 44 * s;
        const float ksize = 24 * s;
        float kx = W - 40 * s;
        const auto group = [&](const std::string& label, std::initializer_list<const char*> keys) {
            for (auto it = std::rbegin(keys); it != std::rend(keys); ++it) {
                const Text& t = GetText(menu, *it, int(13 * s), true, false);
                const float bw = std::max(ksize, t.w + 10 * s);
                kx -= bw;
                Fill(r, kx, ky, bw, ksize, Parchment);
                Put(menu, t, kx + (bw - t.w) / 2, ky + (ksize - t.h) / 2, Ink);
                kx -= 5 * s;
            }
            const Text& l = GetText(menu, label, int(15 * s), false, false);
            kx -= l.w + 4 * s;
            Put(menu, l, kx, ky + (ksize - l.h) / 2, GoldText);
            kx -= 26 * s;
        };
        group(Say("close"), {"Esc"});
        group(Say("tabs"), {"Q", "E"});
        group(Say("change"), {"◀", "▶"});
        group(Say("choose"), {"▲", "▼"});
    }
    SDL_RenderPresent(r);
}

} // namespace ZooT
