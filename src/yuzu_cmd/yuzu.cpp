// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <iterator>
#include <fstream>
#include <map>
#include <thread>

#include <nlohmann/json.hpp>
#include <vector>
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include "common/settings_enums.h"
#include "core/launch_params.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>

#include <fmt/ostream.h>

#include "common/fs/fs_util.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/cpu_manager.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/nup_writer.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"
#include "frontend_common/config.h"
#include "input_common/drivers/keyboard.h"
#include "input_common/main.h"
#include "common/param_package.h"
#include "common/settings_input.h"
#include "network/network.h"
#include "sdl_config.h"
#include "video_core/gpu.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_base.h"
#include "video_core/shader_warm/shader_warm_builder.h"
#include "video_core/shader_warm/warm_records.h"
#include "yuzu_cmd/emu_window/emu_window_sdl3.h"
#ifdef HAS_OPENGL
#include "yuzu_cmd/emu_window/emu_window_sdl3_gl.h"
#endif
#include "yuzu_cmd/emu_window/emu_window_sdl3_null.h"
#include "yuzu_cmd/emu_window/emu_window_sdl3_vk.h"
#include "yuzu_cmd/zoot_loading_screen.h"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

#ifdef _WIN32
// windows.h needs to be included before shellapi.h
#include <windows.h>
#include <shellapi.h>
#include "common/windows/timer_resolution.h"
#endif

#ifdef _WIN32
extern "C" {
// tells Nvidia and AMD drivers to use the dedicated GPU by default on laptops with switchable
// graphics
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

// EDEN_NX2_CRASH_DUMP=<dir>: on an unhandled exception (a real crash; fastmem faults are handled
// before this), write <dir>/eden-crash-<pid>.dmp. A debugger changes the timing enough to hide
// some crashes, so this catches them at full speed. dbghelp.dll is loaded only then.
// Prints a code address as module+offset (resolve with llvm-symbolizer and the module's PDB).
static void PrintCodeAddress(const char* label, const void* address) {
    HMODULE module{};
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(address), &module)) {
        GetModuleFileNameA(module, name, sizeof(name));
    }
    const char* base_name = std::strrchr(name, '\\');
    std::fprintf(stderr, "  %s %p %s+%#llx\n", label, address, base_name ? base_name + 1 : name,
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address) -
                                                 reinterpret_cast<uintptr_t>(module)));
}

static LONG WINAPI WriteCrashDump(EXCEPTION_POINTERS* exception) {
    static std::atomic<bool> written{};
    const char* directory = std::getenv("EDEN_NX2_CRASH_DUMP");
    if (directory == nullptr || written.exchange(true)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // The faulting address and stack first: they survive even if the dump fails.
    const EXCEPTION_RECORD& record = *exception->ExceptionRecord;
    std::fprintf(stderr, "Unhandled exception %#lx on thread %lu, info %#llx %#llx\n",
                 record.ExceptionCode, GetCurrentThreadId(),
                 static_cast<unsigned long long>(record.NumberParameters > 0 ? record.ExceptionInformation[0] : 0),
                 static_cast<unsigned long long>(record.NumberParameters > 1 ? record.ExceptionInformation[1] : 0));
    PrintCodeAddress("at", record.ExceptionAddress);
    void* frames[48];
    const USHORT count = RtlCaptureStackBackTrace(0, 48, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        PrintCodeAddress("frame", frames[i]);
    }
    std::fflush(stderr);
    struct ExceptionInformation {
        DWORD thread_id;
        EXCEPTION_POINTERS* exception_pointers;
        BOOL client_pointers;
    };
    using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, int, const ExceptionInformation*,
                                              const void*, const void*);
    const HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
    const auto write = dbghelp ? reinterpret_cast<MiniDumpWriteDumpFn>(
                                     GetProcAddress(dbghelp, "MiniDumpWriteDump"))
                               : nullptr;
    if (write == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    char path[MAX_PATH];
    std::snprintf(path, sizeof(path), "%s\\eden-crash-%lu.dmp", directory, GetCurrentProcessId());
    const HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        const ExceptionInformation information{GetCurrentThreadId(), exception, FALSE};
        // MiniDumpWithThreadInfo; the larger MiniDumpWithIndirectlyReferencedMemory failed on
        // Eden's address space and left an empty file.
        const BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), file, 0x1000,
                              &information, nullptr, nullptr);
        const DWORD error = ok ? 0 : GetLastError();
        CloseHandle(file);
        std::fprintf(stderr, ok ? "Crash dump written to %s\n" : "Crash dump to %s failed (%#lx)\n",
                     path, error);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static void OnStateChanged(const Network::RoomMember::State& state) {
    switch (state) {
    case Network::RoomMember::State::Idle:
        LOG_DEBUG(Network, "Network is idle");
        break;
    case Network::RoomMember::State::Joining:
        LOG_DEBUG(Network, "Connection sequence to room started");
        break;
    case Network::RoomMember::State::Joined:
        LOG_DEBUG(Network, "Successfully joined to the room");
        break;
    case Network::RoomMember::State::Moderator:
        LOG_DEBUG(Network, "Successfully joined the room as a moderator");
        break;
    default:
        break;
    }
}

static void OnNetworkError(const Network::RoomMember::Error& error) {
    switch (error) {
    case Network::RoomMember::Error::LostConnection:
        LOG_DEBUG(Network, "Lost connection to the room");
        break;
    case Network::RoomMember::Error::CouldNotConnect:
        LOG_ERROR(Network, "Error: Could not connect");
        exit(1);
        break;
    case Network::RoomMember::Error::NameCollision:
        LOG_ERROR(
            Network,
            "You tried to use the same nickname as another user that is connected to the Room");
        exit(1);
        break;
    case Network::RoomMember::Error::IpCollision:
        LOG_ERROR(Network, "You tried to use the same fake IP-Address as another user that is "
                           "connected to the Room");
        exit(1);
        break;
    case Network::RoomMember::Error::WrongPassword:
        LOG_ERROR(Network, "Room replied with: Wrong password");
        exit(1);
        break;
    case Network::RoomMember::Error::WrongVersion:
        LOG_ERROR(Network,
                  "You are using a different version than the room you are trying to connect to");
        exit(1);
        break;
    case Network::RoomMember::Error::RoomIsFull:
        LOG_ERROR(Network, "The room is full");
        exit(1);
        break;
    case Network::RoomMember::Error::HostKicked:
        LOG_ERROR(Network, "You have been kicked by the host");
        break;
    case Network::RoomMember::Error::HostBanned:
        LOG_ERROR(Network, "You have been banned by the host");
        break;
    case Network::RoomMember::Error::UnknownError:
        LOG_ERROR(Network, "UnknownError");
        break;
    case Network::RoomMember::Error::PermissionDenied:
        LOG_ERROR(Network, "PermissionDenied");
        break;
    case Network::RoomMember::Error::NoSuchUser:
        LOG_ERROR(Network, "NoSuchUser");
        break;
    }
}

static void OnMessageReceived(const Network::ChatEntry& msg) {
    std::cout << std::endl << msg.nickname << ": " << msg.message << std::endl << std::endl;
}

static void OnStatusMessageReceived(const Network::StatusMessageEntry& msg) {
    std::string message = [&]() {
        switch (msg.type) {
        case Network::IdMemberJoin:
            return fmt::format("{} has joined", msg.nickname);
        case Network::IdMemberLeave:
            return fmt::format("{} has left", msg.nickname);
        case Network::IdMemberKicked:
            return fmt::format("{} has been kicked", msg.nickname);
        case Network::IdMemberBanned:
            return fmt::format("{} has been banned", msg.nickname);
        case Network::IdAddressUnbanned:
            return fmt::format("{} has been unbanned", msg.nickname);
        default:
            return std::string{};
        }
    }();
    if (!message.empty())
        std::cout << std::endl << "* " << message << std::endl << std::endl;
}

struct SdlState {
    Core::System system{};
    // Must outlive emu_window, which keeps a pointer to it.
    InputCommon::InputSubsystem input_subsystem{};
    std::unique_ptr<EmuWindow_SDL3> emu_window;
    bool network_initialized{};
};

constexpr int kRestartExitCode = 75;
static std::atomic_bool restart_requested{false};

// ExecuteProgram state for the restarted process: what the Qt frontend keeps in-process when it
// reboots a game. Only read when the launcher sets EDEN_CLI_RESTART_HANDOFF, so a leftover file
// never changes a normal launch.
struct RestartHandoff {
    u64 program_id{};
    s32 program_index{};
    s32 previous_program_index{-1};
    std::vector<std::vector<u8>> user_channel;
};

static std::filesystem::path RestartHandoffPath() {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir) / "restart-handoff.bin";
}

template <typename T>
static void PutRaw(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

template <typename T>
static void GetRaw(std::istream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
}

// Bounds keep a damaged handoff from requesting huge allocations; the writer refuses anything the
// reader would reject.
constexpr u32 kHandoffMaxEntries = 256;
constexpr u64 kHandoffMaxTotalSize = 64u << 20;

static bool WriteRestartHandoff(const RestartHandoff& handoff) {
    u64 total = 0;
    for (const auto& entry : handoff.user_channel) {
        total += entry.size();
    }
    if (handoff.user_channel.size() > kHandoffMaxEntries || total > kHandoffMaxTotalSize) {
        return false;
    }
    std::ofstream out{RestartHandoffPath(), std::ios::binary | std::ios::trunc};
    PutRaw(out, handoff.program_id);
    PutRaw(out, handoff.program_index);
    PutRaw(out, handoff.previous_program_index);
    PutRaw(out, static_cast<u32>(handoff.user_channel.size()));
    for (const auto& entry : handoff.user_channel) {
        PutRaw(out, static_cast<u32>(entry.size()));
        out.write(reinterpret_cast<const char*>(entry.data()), std::streamsize(entry.size()));
    }
    out.close();
    return !out.fail();
}

// Leaves the file in place; the caller removes it once the game has loaded.
static std::optional<RestartHandoff> ReadRestartHandoff() {
    std::ifstream in{RestartHandoffPath(), std::ios::binary};
    RestartHandoff handoff;
    u32 count{};
    GetRaw(in, handoff.program_id);
    GetRaw(in, handoff.program_index);
    GetRaw(in, handoff.previous_program_index);
    GetRaw(in, count);
    if (in.fail() || count > kHandoffMaxEntries) {
        return std::nullopt;
    }
    u64 total = 0;
    for (u32 i = 0; i < count; ++i) {
        u32 size{};
        GetRaw(in, size);
        total += size;
        if (in.fail() || total > kHandoffMaxTotalSize) {
            return std::nullopt;
        }
        auto& entry = handoff.user_channel.emplace_back(size);
        in.read(reinterpret_cast<char*>(entry.data()), std::streamsize(size));
        if (in.fail()) {
            return std::nullopt;
        }
    }
    return handoff;
}

static std::atomic_bool pack_nup_cancel{false};
static std::atomic_bool unpack_nup_cancel{false};
static std::atomic_bool edit_nup_cancel{false};

// eden-cli --pack-nup <dump> [<out.nup>]; the output defaults to <dump>.nup next to the dump.
static bool PackNup(const std::string& source, const std::string& output) {
    const auto to_path = [](const std::string& utf8) {
        return std::filesystem::path{std::u8string{utf8.begin(), utf8.end()}};
    };
    const auto dump_root = to_path(source);
    const auto archive =
        output.empty() ? FileSys::Nup::DefaultArchivePath(dump_root) : to_path(output);
    std::signal(SIGINT, [](int) { pack_nup_cancel = true; });

    const auto start = std::chrono::steady_clock::now();
    auto last_report = start;
    std::string error;
    const auto result = FileSys::Nup::PackDump(
        dump_root, archive, {},
        [&](const FileSys::Nup::PackProgress& p) {
            const auto now = std::chrono::steady_clock::now();
            if (now - last_report < std::chrono::seconds(1) && p.files_done != p.files_total) {
                return;
            }
            last_report = now;
            fmt::print(stderr, "\r{} / {} MiB, {} / {} files", p.bytes_done >> 20,
                       p.bytes_total >> 20, p.files_done, p.files_total);
        },
        &pack_nup_cancel, &error);
    const auto seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    fmt::print(stderr, "\n");
    switch (result) {
    case FileSys::Nup::PackResult::Success:
        fmt::print(stderr, "Wrote {} in {:.1f} s\n", archive.string(), seconds);
        return true;
    case FileSys::Nup::PackResult::Cancelled:
        fmt::print(stderr, "Cancelled\n");
        return false;
    default:
        fmt::print(stderr, "Packing failed: {}\n", error);
        return false;
    }
}

// eden-cli --unpack-nup <file.nup> [<out_dir>]; the output defaults to <name>/ next to the NUP.
static bool UnpackNup(const std::string& source, const std::string& output) {
    const auto to_path = [](const std::string& utf8) {
        return std::filesystem::path{std::u8string{utf8.begin(), utf8.end()}};
    };
    const auto archive = to_path(source);
    const auto out_dir =
        output.empty() ? FileSys::Nup::DefaultExtractPath(archive) : to_path(output);
    std::signal(SIGINT, [](int) { unpack_nup_cancel = true; });

    const auto start = std::chrono::steady_clock::now();
    auto last_report = start;
    std::string error;
    const auto result = FileSys::Nup::ExtractNup(
        archive, out_dir,
        [&](const FileSys::Nup::PackProgress& p) {
            const auto now = std::chrono::steady_clock::now();
            if (now - last_report < std::chrono::seconds(1) && p.files_done != p.files_total) {
                return;
            }
            last_report = now;
            fmt::print(stderr, "\r{} / {} MiB, {} / {} files", p.bytes_done >> 20,
                       p.bytes_total >> 20, p.files_done, p.files_total);
        },
        &unpack_nup_cancel, &error);
    const auto seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    fmt::print(stderr, "\n");
    switch (result) {
    case FileSys::Nup::ExtractResult::Success:
        fmt::print(stderr, "Extracted to {} in {:.1f} s\n", out_dir.string(), seconds);
        return true;
    case FileSys::Nup::ExtractResult::Cancelled:
        fmt::print(stderr, "Cancelled\n");
        return false;
    default:
        fmt::print(stderr, "Extraction failed: {}\n", error);
        return false;
    }
}

static bool EditNup(const Core::LaunchParams& lp) {
    const auto to_path = [](const std::string& utf8) {
        return std::filesystem::path{std::u8string{utf8.begin(), utf8.end()}};
    };
    const auto archive = to_path(*lp.edit_nup_source);
    FileSys::Nup::MetadataEdit edit;
    if (lp.edit_nup_name) {
        edit.title_name = *lp.edit_nup_name;
    }
    if (lp.edit_nup_publisher) {
        edit.publisher = *lp.edit_nup_publisher;
    }
    if (lp.edit_nup_version) {
        edit.display_version = *lp.edit_nup_version;
    }
    if (lp.edit_nup_icon) {
        if (lp.edit_nup_icon->empty()) {
            edit.icon = std::vector<u8>{};
        } else {
            const auto icon_path = to_path(*lp.edit_nup_icon);
            std::ifstream in(icon_path, std::ios::binary);
            if (!in) {
                fmt::print(stderr, "Cannot read icon {}\n", icon_path.string());
                return false;
            }
            edit.icon = std::vector<u8>((std::istreambuf_iterator<char>(in)), {});
            if (edit.icon->empty()) {
                fmt::print(stderr, "Icon file is empty\n");
                return false;
            }
        }
    }
    if (!edit.title_name && !edit.icon && !edit.display_version && !edit.publisher) {
        fmt::print(stderr, "Nothing to edit: pass --nup-name, --nup-icon, --nup-version or "
                           "--nup-publisher (or a --clear-* flag)\n");
        return false;
    }
    std::signal(SIGINT, [](int) { edit_nup_cancel = true; });
    std::string error;
    switch (FileSys::Nup::EditNupMetadata(archive, edit, &error, &edit_nup_cancel)) {
    case FileSys::Nup::EditResult::Success:
        fmt::print(stderr, "Updated {}\n", archive.string());
        return true;
    case FileSys::Nup::EditResult::NoChange:
        fmt::print(stderr, "No changes to apply\n");
        return true;
    case FileSys::Nup::EditResult::Cancelled:
        fmt::print(stderr, "Cancelled\n");
        return false;
    default:
        fmt::print(stderr, "Edit failed: {}\n", error);
        return false;
    }
}

/// Hidden test hook (not in --help): builds a game's shader precache files without booting it.
///   yuzu-cmd --build-warm <game file> <title id hex> <output dir> [--defaults | cache ...]
/// With pipeline cache files (lichii-cache.v2.bin logs or older vulkan.fork-v<N>.bin caches), the
/// draw state guesses come from them; --defaults uses the built-in guesses; neither uses the
/// title's own pipeline cache when it exists.
static bool BuildWarm(Core::System& system, int argc, char** argv) {
    namespace ShaderWarm = VideoCommon::ShaderWarm;
    if (argc < 5) {
        fmt::print(stderr, "usage: --build-warm <game> <title id> <output dir> [--defaults | cache ...]\n");
        return false;
    }
    system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    system.SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
    const u64 title_id = std::strtoull(argv[3], nullptr, 16);
    ShaderWarm::BuildOptions options;
    options.output_dir = Common::FS::ToU8String(argv[4]);
    std::vector<std::filesystem::path> caches;
    bool defaults = false;
    for (int i = 5; i < argc; ++i) {
        if (std::string_view{argv[i]} == "--defaults") {
            defaults = true;
        } else {
            caches.emplace_back(Common::FS::ToU8String(argv[i]));
        }
    }
    if (defaults || !caches.empty()) {
        options.runtime_caches = caches;
    }
    std::string last_stage;
    auto last_print = std::chrono::steady_clock::now();
    const auto begin = last_print;
    const auto on_progress = [&](const ShaderWarm::Progress& progress) {
        const auto now = std::chrono::steady_clock::now();
        if (progress.stage == last_stage && now - last_print < std::chrono::seconds{5}) {
            return;
        }
        last_stage = progress.stage;
        last_print = now;
        fmt::print("{:7.1f} s  {}: {} / {}\n",
                   std::chrono::duration<double>(now - begin).count(), progress.stage,
                   progress.done, progress.total);
        std::fflush(stdout);
    };
    const auto result = ShaderWarm::BuildWarmFilesWith(
        system, Common::FS::ToU8String(argv[2]), title_id, options, on_progress, {});
    // The error can hold a Windows path: escape it so the summary stays valid JSON.
    std::string error;
    for (const char c : result.error) {
        switch (c) {
        case '"':
            error += "\\\"";
            break;
        case '\\':
            error += "\\\\";
            break;
        case '\n':
            error += "\\n";
            break;
        case '\r':
            error += "\\r";
            break;
        case '\t':
            error += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                error += fmt::format("\\u{:04x}", static_cast<unsigned>(c));
            } else {
                error += c;
            }
            break;
        }
    }
    fmt::print("{{\"ok\": {}, \"error\": \"{}\", \"archives\": {}, \"compute\": {}, "
               "\"vertex\": {}, \"fragment\": {}, \"skipped\": {}, \"seconds\": {:.1f}}}\n",
               result.ok, error, result.archives, result.compute_programs,
               result.vertex_programs, result.fragment_programs, result.skipped,
               std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count());
    std::fflush(stdout);
    return result.ok;
}

// ZooT-PC: the Qt frontend's PrepareShaderPrecache for eden-cli. With precache_shaders on (Ask
// counts as yes, there is no dialog), the game's compute programs are extracted from its files
// once, and every boot builds them with the cached graphics pipelines before the game starts,
// instead of mid-game. The renderer redoes it only when the driver or the precache file changed.
/// ZooT-PC: the NX2 switches of nx2_switches.txt, which only the Qt frontend read before
/// (yuzu/nx2_switches.h). Portable: the file next to the executable, then an optional
/// user\nx2_switches.txt for the player's own overrides; never %AppData%. A variable already in
/// the real environment (the launcher's, a script's) wins.
static void ParseZootSwitchFile(const std::filesystem::path& path,
                                std::map<std::string, std::string>& entries) {
    std::ifstream file{path};
    if (!file) {
        return;
    }
    constexpr const char* blank = " \t\r";
    std::string line;
    while (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(blank));
        line.erase(line.find_last_not_of(blank) + 1);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string name = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        name.erase(name.find_last_not_of(blank) + 1);
        value.erase(0, value.find_first_not_of(blank));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        if (name.starts_with("EDEN_NX2_") || name.starts_with("EDEN_NATIVE_") ||
            name.starts_with("EDEN_DKB_") || name == "EDEN_PUNCH_HOLE" ||
            name == "EDEN_VI2_MAX_RESOLUTION_MODE") {
            entries.insert_or_assign(std::move(name), std::move(value));
        }
    }
}

static void LoadZootSwitches() {
    std::map<std::string, std::string> entries;
    const auto exe_dir = Common::FS::GetExeDirectory();
    ParseZootSwitchFile(exe_dir / "nx2_switches.txt", entries);
    ParseZootSwitchFile(exe_dir / "user" / "nx2_switches.txt", entries);
    for (const auto& [name, value] : entries) {
        if (std::getenv(name.c_str()) == nullptr) {
            _putenv_s(name.c_str(), value.c_str());
        }
    }
}

/// ZooT-PC launcher hooks for the Controls page (not in --help), through Lichii's own input
/// subsystem, so every mapping is in the format the game reads. Each prints one JSON line:
///   eden-cli --zoot-pad-list                    controllers: [{"name", "params"}]
///   eden-cli --zoot-pad-automap "<params>"      default mapping of a controller:
///                                               {"button_a": "<params>", ..., "lstick": ...}
///   eden-cli --zoot-pad-capture button|stick <seconds>
///                                               the next controller button or stick moved:
///                                               {"params": "<params>"} (empty on time-out)
/// The keyboard is captured by the launcher itself (no game window here).
static bool ZootPadCommand(InputCommon::InputSubsystem& input, int argc, char** argv) {
    const std::string_view command{argv[1]};
    input.Initialize();
    // SDL is not set up by this process, so the SDL driver runs its own event thread and
    // enumerates the controllers; give it a moment.
    const auto settle = [&input](int ms) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds{ms};
        while (std::chrono::steady_clock::now() < until) {
            input.PumpEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
    };
    const auto is_pad = [](const Common::ParamPackage& p) {
        const std::string engine = p.Get("engine", "");
        return !engine.empty() && engine != "any" && engine != "keyboard" && engine != "mouse" &&
               engine != "touch_from_button" && engine != "analog_from_button";
    };
    nlohmann::json out;
    bool ok = true;
    if (command == "--zoot-pad-list") {
        settle(1500);
        out = nlohmann::json::array();
        for (const auto& device : input.GetInputDevices()) {
            if (is_pad(device)) {
                out.push_back({{"name", device.Get("display", "Controller")},
                               {"params", device.Serialize()}});
            }
        }
    } else if (command == "--zoot-pad-automap" && argc > 2) {
        settle(1500);
        const Common::ParamPackage device{argv[2]};
        out = nlohmann::json::object();
        for (const auto& [button, params] : input.GetButtonMappingForDevice(device)) {
            out[Settings::NativeButton::mapping[button]] = params.Serialize();
        }
        for (const auto& [analog, params] : input.GetAnalogMappingForDevice(device)) {
            out[Settings::NativeAnalog::mapping[analog]] = params.Serialize();
        }
        ok = !out.empty();
    } else if (command == "--zoot-pad-capture" && argc > 2) {
        const bool stick = std::string_view{argv[2]} == "stick";
        const int seconds = argc > 3 ? std::clamp(std::atoi(argv[3]), 1, 30) : 10;
        settle(800);
        input.BeginMapping(stick ? InputCommon::Polling::InputType::Stick
                                 : InputCommon::Polling::InputType::Button);
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{seconds};
        std::string found;
        while (found.empty() && std::chrono::steady_clock::now() < until) {
            input.PumpEvents();
            const Common::ParamPackage p = input.GetNextInput();
            if (is_pad(p)) {
                found = p.Serialize();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        input.StopMapping();
        out = {{"params", found}};
    } else {
        fmt::print(stderr, "usage: --zoot-pad-list | --zoot-pad-automap <params> | "
                           "--zoot-pad-capture button|stick [seconds]\n");
        ok = false;
    }
    fmt::print("{}\n", out.dump());
    std::fflush(stdout);
    input.Shutdown();
    return ok;
}

/// ZooT-PC tool (not in --help): the pipeline state list from pipeline caches recorded while
/// playing, merged into the list file when it exists. Each vertex + fragment pipeline goes in
/// with its draw state and the values its programs' translation read: no game code.
///   eden-cli --zoot-export-states <list file> [pipeline cache files...]
/// Without cache files: Ocarina's own (lichii-cache.v2.bin, or an older disk cache).
static bool ExportPipelineStates(int argc, char** argv) {
    namespace ShaderWarm = VideoCommon::ShaderWarm;
    if (argc < 3) {
        fmt::print(stderr, "usage: --zoot-export-states <list file> [cache files...]\n");
        return false;
    }
    const std::filesystem::path out = Common::FS::ToU8String(argv[2]);
    std::vector<std::filesystem::path> caches;
    for (int i = 3; i < argc; ++i) {
        caches.emplace_back(Common::FS::ToU8String(argv[i]));
    }
    if (caches.empty()) {
        constexpr u64 OcarinaTitle = 0x0400486024DEC000ULL;
        caches = ShaderWarm::FindRuntimeCaches(
            ShaderWarm::GraphicsWarmPath(OcarinaTitle).parent_path());
    }
    std::vector<ShaderWarm::StateEntry> keys = ShaderWarm::ReadStateList(out);
    const size_t before = keys.size();
    auto recorded = ShaderWarm::ReadRuntimeStateEntries(caches);
    const size_t recorded_count = recorded.size();
    keys.insert(keys.end(), std::make_move_iterator(recorded.begin()),
                std::make_move_iterator(recorded.end()));
    if (!ShaderWarm::WriteStateList(out, keys)) {
        fmt::print(stderr, "cannot write {}\n", Common::FS::PathToUTF8String(out));
        return false;
    }
    const size_t after = ShaderWarm::ReadStateList(out).size();
    fmt::print("{} cache(s): {} vertex+fragment pipelines; list {} -> {} keys ({})\n",
               caches.size(), recorded_count, before, after, Common::FS::PathToUTF8String(out));
    return true;
}

/// ZooT-PC launcher hook (not in --help): writes the game's own icon and title, read from the
/// player's copy, for the launcher's game card. Nothing is shipped with the launcher.
///   eden-cli --zoot-export-art <game file> <output dir>
/// Writes <output dir>/game-icon.jpg (or .png) and game-title.txt.
static bool ExportGameArt(Core::System& system, int argc, char** argv) {
    if (argc < 4) {
        fmt::print(stderr, "usage: --zoot-export-art <game> <output dir>\n");
        return false;
    }
    system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    system.SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
    const auto file = Core::GetGameFileFromPath(system.GetFilesystem(), argv[2]);
    const auto loader = file ? Loader::GetLoader(system, file, 0, 0) : nullptr;
    if (loader == nullptr) {
        fmt::print(stderr, "no loader for {}\n", argv[2]);
        return false;
    }
    const std::filesystem::path out = Common::FS::ToU8String(argv[3]);
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    bool any = false;
    std::vector<u8> icon;
    if (loader->ReadIcon(icon) == Loader::ResultStatus::Success && icon.size() > 8) {
        const bool png = icon[0] == 0x89 && icon[1] == 'P' && icon[2] == 'N' && icon[3] == 'G';
        std::ofstream stream{out / (png ? "game-icon.png" : "game-icon.jpg"), std::ios::binary};
        stream.write(reinterpret_cast<const char*>(icon.data()),
                     static_cast<std::streamsize>(icon.size()));
        any = stream.good();
    }
    std::string title;
    if (loader->ReadTitle(title) == Loader::ResultStatus::Success && !title.empty()) {
        std::ofstream stream{out / "game-title.txt", std::ios::binary};
        stream << title;
        any = true;
    }
    fmt::print("{{\"icon\": {}, \"title\": \"{}\"}}\n", icon.size(), title);
    return any;
}

static std::string LoadingStageName(std::string_view stage) {
    if (stage == "Reading game files") {
        return "read";
    }
    if (stage == "Extracting shaders") {
        return "extract";
    }
    if (stage == "Converting") {
        return "convert";
    }
    if (stage == "Writing") {
        return "write";
    }
    return std::string{stage};
}

static void PrepareShaderPrecache(Core::System& system, const std::string& game_path,
                                  ZooT::LoadingScreen* screen) {
    namespace ShaderWarm = VideoCommon::ShaderWarm;
    ShaderWarm::RequestWarmBeforeStart(false);
    if (Settings::values.precache_shaders.GetValue() == Settings::PrecacheShaders::Off ||
        !Settings::values.use_disk_shader_cache.GetValue()) {
        return;
    }
    u64 title_id{};
    const auto file = Core::GetGameFileFromPath(system.GetFilesystem(), game_path);
    const auto loader = file ? Loader::GetLoader(system, file, 0, 0) : nullptr;
    if (loader == nullptr || loader->ReadProgramId(title_id) != Loader::ResultStatus::Success ||
        title_id == 0) {
        LOG_WARNING(Frontend, "Shader precache: no program id, starting without it");
        return;
    }
    std::error_code ec;
    // With pipeline libraries the builder also writes the vertex and fragment programs
    // (warm-graphics.bin); a compute-only precache from before needs that file built once.
    const bool use_gpl = Settings::values.use_graphics_pipeline_library.GetValue();
    const bool graphics_missing =
        use_gpl && !std::filesystem::exists(ShaderWarm::GraphicsWarmPath(title_id), ec);
    // ZooT-PC pipeline state list (zoot-pipeline-states.bin next to the exe): its entries go into
    // warm-graphics.bin, so a new or changed list (an update) builds that file once more.
    const auto list_path = ShaderWarm::StateListPath();
    std::string list_stamp = "none";
    if (std::filesystem::exists(list_path, ec)) {
        list_stamp = fmt::format(
            "{} {}", std::filesystem::file_size(list_path, ec),
            std::filesystem::last_write_time(list_path, ec).time_since_epoch().count());
    }
    const auto stamp_path = ShaderWarm::GraphicsWarmPath(title_id).parent_path() /
                            "warm-states.stamp";
    std::string built_stamp;
    if (std::ifstream in{stamp_path}) {
        std::getline(in, built_stamp);
    }
    if (built_stamp.empty()) {
        built_stamp = "none";
    }
    const bool states_changed = use_gpl && !graphics_missing && list_stamp != built_stamp;
    if (states_changed) {
        LOG_INFO(Frontend, "Shader precache: pipeline state list changed ({} -> {}), rebuilding",
                 built_stamp, list_stamp);
    }
    if (!ShaderWarm::WarmPending(title_id) && !graphics_missing && !states_changed) {
        ShaderWarm::RequestWarmBeforeStart(true);
        return;
    }
    if (!std::filesystem::exists(ShaderWarm::ComputeWarmPath(title_id), ec) || graphics_missing ||
        states_changed) {
        LOG_INFO(Frontend, "Shader precache: extracting the shaders of {:016X} from the game files",
                 title_id);
        const auto begin = std::chrono::steady_clock::now();
        std::string last_stage;
        auto last_print = begin;
        const auto on_progress = [&](const ShaderWarm::Progress& progress) {
            if (screen) {
                screen->Update(LoadingStageName(progress.stage), progress.done, progress.total);
            }
            const auto now = std::chrono::steady_clock::now();
            if (progress.stage == last_stage && now - last_print < std::chrono::seconds{5}) {
                return;
            }
            last_stage = progress.stage;
            last_print = now;
            LOG_INFO(Frontend, "Shader precache: {} {}/{}", progress.stage, progress.done,
                     progress.total);
        };
        const auto result = ShaderWarm::BuildWarmFiles(
            system, Common::FS::ToU8String(game_path), title_id, on_progress, {});
        LOG_INFO(Frontend,
                 "Shader precache: ok={} archives={} compute={} vertex={} fragment={} "
                 "state list={}/{} skipped={} in {:.1f} s {}",
                 result.ok, result.archives, result.compute_programs, result.vertex_programs,
                 result.fragment_programs, result.state_entries, result.state_keys,
                 result.skipped,
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count(),
                 result.error);
        if (!result.ok || !std::filesystem::exists(ShaderWarm::ComputeWarmPath(title_id), ec)) {
            return;
        }
        if (std::ofstream out{stamp_path, std::ios::trunc}) {
            out << list_stamp << '\n';
        }
    }
    ShaderWarm::RequestWarmBeforeStart(true);
}

extern "C" SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
#ifdef _WIN32
    SetUnhandledExceptionFilter(WriteCrashDump);
#endif
    // SDL calls SDL_AppIterate back to back by default, which kept a host core at 100% polling
    // input; 240 Hz is plenty for input and the key script.
    SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "240");
    // Before anything reads its EDEN_NX2_* switch (many cache the value on first use).
    LoadZootSwitches();
    SdlState* state = new SdlState();
    *appstate = state;

#ifdef _WIN32
    // ZooT-PC: a parent that reads our output through a pipe or a file (the launcher's
    // --zoot-* commands) keeps it; only a plain console parent gets the console attached.
    const HANDLE std_out = GetStdHandle(STD_OUTPUT_HANDLE);
    const DWORD std_out_type =
        std_out && std_out != INVALID_HANDLE_VALUE ? GetFileType(std_out) : FILE_TYPE_UNKNOWN;
    if (std_out_type != FILE_TYPE_PIPE && std_out_type != FILE_TYPE_DISK &&
        AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "wb", stdout);
        freopen("CONOUT$", "wb", stderr);
    }
    // Raise the CRT's open-file limit like the Qt frontend: loose RomFS folders keep thousands of
    // files open through the VFS cache.
    _setmaxstdio(8192);
#elif defined(__unix__) || defined(__APPLE__)
    // Same for the open file limit (macOS shells default to 256).
    if (struct rlimit rl; getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        rl.rlim_cur = std::min<rlim_t>(8192, rl.rlim_max);
        setrlimit(RLIMIT_NOFILE, &rl);
    }
#endif

    Common::Log::Initialize();
    Common::Log::SetColorConsoleBackendEnabled(true);
    Common::Log::Start();

    if (argc > 1 && std::string_view{argv[1]} == "--build-warm") {
        return BuildWarm(state->system, argc, argv) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }
    if (argc > 1 && std::string_view{argv[1]} == "--zoot-export-states") {
        return ExportPipelineStates(argc, argv) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }
    if (argc > 1 && std::string_view{argv[1]} == "--zoot-export-art") {
        return ExportGameArt(state->system, argc, argv) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }
    if (argc > 1 && std::string_view{argv[1]}.starts_with("--zoot-pad")) {
        return ZootPadCommand(state->input_subsystem, argc, argv) ? SDL_APP_SUCCESS
                                                                  : SDL_APP_FAILURE;
    }

    int option_index = 0;
#ifdef _WIN32
    int argc_w;
    auto argv_w = CommandLineToArgvW(GetCommandLineW(), &argc_w);
    if (argv_w == nullptr) {
        LOG_CRITICAL(Frontend, "Failed to get command line arguments");
        return SDL_APP_FAILURE;
    }
#else
    wchar_t **argv_w = nullptr;
#endif
    Core::LaunchParams lp = Core::ParseLaunchParams(state->system, argc, argv, argv_w);
#ifdef _WIN32
    LocalFree(argv_w);
#endif
    SdlConfig config{lp.config_path};

    Core::ApplyLaunchParams(lp);

    // Informational options already printed by ApplyLaunchParams; handle them before any
    // archive operation so --help/--version never packs, unpacks or edits.
    if (lp.print_help || lp.print_version) {
        return SDL_APP_SUCCESS;
    }

    if (lp.pack_nup_source) {
        return PackNup(*lp.pack_nup_source, lp.filepath) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

    if (lp.unpack_nup_source) {
        return UnpackNup(*lp.unpack_nup_source, lp.filepath) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

    if (lp.edit_nup_source) {
        return EditNup(lp) ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }

    if (lp.filepath.empty()) {
        LOG_CRITICAL(Frontend, "Failed to load ROM: No ROM specified");
        return SDL_APP_FAILURE;
    }

    state->network_initialized = Network::Init();
    if (!state->network_initialized) {
        LOG_CRITICAL(Network, "Failed to initialize room networking");
        return SDL_APP_FAILURE;
    }

    state->system.Initialize();


    // Apply the command line arguments
    state->system.ApplySettings();

    switch (Settings::values.renderer_backend.GetValue()) {
#ifdef HAS_OPENGL
    case Settings::RendererBackend::OpenGL_GLSL:
    case Settings::RendererBackend::OpenGL_GLASM:
    case Settings::RendererBackend::OpenGL_SPIRV:
        state->emu_window = std::make_unique<EmuWindow_SDL3_GL>(&state->input_subsystem, state->system, lp.fullscreen);
        break;
#endif
    case Settings::RendererBackend::Vulkan:
        state->emu_window = std::make_unique<EmuWindow_SDL3_VK>(&state->input_subsystem, state->system, lp.fullscreen);
        break;
    case Settings::RendererBackend::Null:
        state->emu_window = std::make_unique<EmuWindow_SDL3_Null>(&state->input_subsystem, state->system, lp.fullscreen);
        break;
    default:
        LOG_CRITICAL(Frontend, "Invalid renderer backend");
        return SDL_APP_FAILURE;
    }

#ifdef _WIN32
    Common::Windows::SetCurrentTimerResolutionToMaximum();
    state->system.CoreTiming().SetTimerResolutionNs(Common::Windows::GetCurrentTimerResolution());
#endif

    state->system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    // Holds the updates and DLC an NX archive brings, as the Qt frontend does.
    static FileSys::ManualContentProvider manual_provider;
    state->system.RegisterContentProvider(FileSys::ContentProviderUnionSlot::FrontendManual,
                                          &manual_provider);
    state->system.SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
    state->system.GetFileSystemController().CreateFactories(*state->system.GetFilesystem());
    state->system.GetUserChannel().clear();

    Service::AM::FrontendAppletParameters load_parameters{
        .applet_id = Service::AM::AppletId::Application,
    };
    const char* const handoff_env = std::getenv("EDEN_CLI_RESTART_HANDOFF");
    const bool restarted = handoff_env != nullptr && handoff_env[0] != '\0';
    if (restarted) {
        auto handoff = ReadRestartHandoff();
        if (!handoff) {
            // Booting as a frontend launch would clear the game's temp storage.
            LOG_CRITICAL(Frontend, "Restart handoff {} is missing or damaged, not booting",
                         Common::FS::PathToUTF8String(RestartHandoffPath()));
            return SDL_APP_FAILURE;
        }
        state->system.GetUserChannel() = std::move(handoff->user_channel);
        load_parameters.program_id = handoff->program_id;
        load_parameters.launch_type = Service::AM::LaunchType::ApplicationInitiated;
        load_parameters.program_index = handoff->program_index;
        load_parameters.previous_program_index = handoff->previous_program_index;
        LOG_INFO(Frontend, "Restarted by the game: program index {}, {} user channel entries",
                 handoff->program_index, state->system.GetUserChannel().size());
    }
    ZooT::ApplyPlayerIcon();
    // ZooT-PC loading screen, from the shader extraction until the shaders are loaded.
    std::unique_ptr<ZooT::LoadingScreen> loading_screen;
    if (Settings::values.use_disk_shader_cache.GetValue()) {
        loading_screen = std::make_unique<ZooT::LoadingScreen>();
    }
    if (!restarted) {
        PrepareShaderPrecache(state->system, lp.filepath, loading_screen.get());
    }
    const Core::SystemResultStatus load_result = state->system.Load(*state->emu_window, lp.filepath, load_parameters);
    switch (load_result) {
    case Core::SystemResultStatus::Success:
        if (restarted) {
            std::error_code ec;
            std::filesystem::remove(RestartHandoffPath(), ec);
        }
        break; // Expected case
    case Core::SystemResultStatus::ErrorGetLoader:
        LOG_CRITICAL(Frontend, "Failed to obtain loader for {}!", lp.filepath);
        return SDL_APP_FAILURE;
    case Core::SystemResultStatus::ErrorMemoryAllocation:
        LOG_CRITICAL(Frontend,
                     "Not enough host memory to start the game. Close other applications and "
                     "check available memory and paging-file space, then try again.");
        return SDL_APP_FAILURE;
    case Core::SystemResultStatus::ErrorLoader:
        LOG_CRITICAL(Frontend, "Failed to load ROM!");
        return SDL_APP_FAILURE;
    case Core::SystemResultStatus::ErrorNotInitialized:
        LOG_CRITICAL(Frontend, "CPUCore not initialized");
        return SDL_APP_FAILURE;
    case Core::SystemResultStatus::ErrorVideoCore:
        LOG_CRITICAL(Frontend, "Failed to initialize VideoCore!");
        return SDL_APP_FAILURE;
    default:
        const u16 loader_id = u16(Core::SystemResultStatus::ErrorLoader);
        const u16 error_id = u16(load_result) - loader_id;
        LOG_CRITICAL(Frontend,
            "While attempting to load the ROM requested, an error occurred. Please "
            "refer to the Eden wiki for more information or the Eden discord for "
            "additional help.\n\nError Code: {:04X}-{:04X}\nError Description: {}",
            loader_id, error_id, Loader::ResultStatus(error_id));
        return SDL_APP_FAILURE;
    }

    if (lp.use_multiplayer) {
        if (auto member = Network::GetRoomMember().lock()) {
            member->BindOnChatMessageReceived(OnMessageReceived);
            member->BindOnStatusMessageReceived(OnStatusMessageReceived);
            member->BindOnStateChanged(OnStateChanged);
            member->BindOnError(OnNetworkError);
            LOG_DEBUG(Network, "Start connection to {}:{} with nickname {}", lp.address, lp.port, lp.nickname);
            member->Join(lp.nickname, lp.address.c_str(), lp.port, 0, Network::NoPreferredIP, lp.password);
        } else {
            LOG_ERROR(Network, "Could not access RoomMember");
            return SDL_APP_FAILURE;
        }
    }

    // Core is loaded, start the GPU (makes the GPU contexts current to this thread)
    state->system.GPU().Start();
    state->system.GetCpuManager().OnGpuReady();

    if (Settings::values.use_disk_shader_cache.GetValue()) {
        state->system.Renderer().ReadRasterizer()->LoadDiskResources(
            state->system.GetApplicationProcessProgramID(), std::stop_token{},
            [screen = loading_screen.get()](VideoCore::LoadCallbackStage stage, size_t value,
                                            size_t total) {
                if (!screen) {
                    return;
                }
                switch (stage) {
                case VideoCore::LoadCallbackStage::Prepare:
                    screen->Update("cache", value, total);
                    break;
                case VideoCore::LoadCallbackStage::Build:
                    screen->Update("build", value, total);
                    break;
                case VideoCore::LoadCallbackStage::Warm:
                    screen->Update("warm", value, total);
                    break;
                case VideoCore::LoadCallbackStage::Complete:
                    screen->Update("start", value, total);
                    break;
                }
            });
    }
    loading_screen.reset();

    // don't do anything, SDL3 already exists for us :D
    state->system.RegisterExitCallback([] {});

    // A game executing a program (e.g. restarting after a settings change) writes its launch state
    // to the restart handoff and quits with kRestartExitCode; the launcher boots the same file again
    // with EDEN_CLI_RESTART_HANDOFF set. Mirrors MainWindow::OnExecuteProgram.
    state->system.RegisterExecuteProgramCallback([system = &state->system](std::size_t program_index) {
        RestartHandoff handoff{
            .program_index = static_cast<s32>(program_index),
            .user_channel = system->GetUserChannel(),
        };
        const u64 previous_program_id = system->GetApplicationProcessProgramID();
        if (previous_program_id > static_cast<u64>(Service::AM::AppletProgramId::MaxProgramId)) {
            handoff.previous_program_index =
                static_cast<s32>(previous_program_id - FileSys::GetBaseTitleID(previous_program_id));
            handoff.program_id = previous_program_id;
        }
        // Without the handoff, a relaunch would boot as a frontend launch and clear temp storage.
        if (!WriteRestartHandoff(handoff)) {
            LOG_CRITICAL(Frontend, "ExecuteProgram: could not write {}, not restarting",
                         Common::FS::PathToUTF8String(RestartHandoffPath()));
            return;
        }
        LOG_INFO(Frontend, "ExecuteProgram: program index {}, exiting with code {} to restart",
                 program_index, kRestartExitCode);
        restart_requested = true;
    });

    // QLaunch launched applications (should) now reload shader cache.
    static std::mutex shader_cache_reload_mutex;
    state->system.RegisterApplicationChangedCallback([&state](u64 changed_program_id) {
        if (!Settings::values.use_disk_shader_cache.GetValue()) {
            return;
        }

        std::scoped_lock lk{shader_cache_reload_mutex};

        LOG_INFO(Frontend, "Reloading disk shader cache for {:016X}", changed_program_id);

        state->system.Pause();
        state->system.GPU().WaitForIdle();
        state->system.GPU().ObtainContext();

        state->system.Renderer().ReadRasterizer()->LoadDiskResources(
            changed_program_id, std::stop_token{},
            [](VideoCore::LoadCallbackStage, size_t, size_t) {});

        state->system.GPU().ReleaseContext();
        state->system.Run();
    });

    void(state->system.Run());
    if (state->system.DebuggerEnabled())
        state->system.InitializeDebugger();
    return SDL_APP_CONTINUE;
}
// EDEN_NX2_KEY_SCRIPT="seconds:code,seconds:-code,..." (or "@<file>"): press (code) or release (-code) keyboard
// keys, by SDL scancode, that many seconds after start. Lets hidden test runs drive menus.
double Nx2KeyClockSeconds(); // emu_window_sdl3.cpp

static void RunKeyScript(InputCommon::InputSubsystem& input_subsystem) {
    struct KeyEvent {
        double seconds; // when frame is 0
        u64 frame;      // "f<frame>:code": after that many presents
        int code;
    };
    static const std::vector<KeyEvent> script = [] {
        std::vector<KeyEvent> events;
        if (const char* text = std::getenv("EDEN_NX2_KEY_SCRIPT")) {
            // "@<file>" reads the list from a file: long scripts in the environment slow down
            // every getenv in the process.
            std::string list{text};
            if (!list.empty() && list[0] == '@') {
                std::ifstream file{list.substr(1)};
                list.assign(std::istreambuf_iterator<char>{file}, {});
            }
            size_t start = 0;
            while (start < list.size()) {
                const size_t end = (std::min)(list.find(',', start), list.size());
                const std::string item = list.substr(start, end - start);
                if (const size_t colon = item.find(':'); colon != std::string::npos) {
                    const std::string when = item.substr(0, colon);
                    const int code = std::stoi(item.substr(colon + 1));
                    if (!when.empty() && when[0] == 'f') {
                        events.push_back({0.0, std::stoull(when.substr(1)), code});
                    } else {
                        events.push_back({std::stod(when), 0, code});
                    }
                }
                start = end + 1;
            }
        }
        return events;
    }();
    static size_t next = 0;
    const double elapsed = Nx2KeyClockSeconds();
    const u64 frame = Core::g_nx2_present_count.load();
    while (next < script.size() && (script[next].frame != 0 ? script[next].frame <= frame
                                                            : script[next].seconds <= elapsed)) {
        const int code = script[next].code;
        LOG_INFO(Frontend, "Key script: {} key {} at {:.1f} s (frame {})",
                 code < 0 ? "release" : "press", code < 0 ? -code : code, elapsed, frame);
        if (code < 0) {
            input_subsystem.GetKeyboard()->ReleaseKey(static_cast<std::size_t>(-code));
        } else {
            input_subsystem.GetKeyboard()->PressKey(static_cast<std::size_t>(code));
        }
        ++next;
    }
}

extern "C" SDL_AppResult SDL_AppIterate(void *appstate) {
    SdlState *state = (SdlState *)appstate;
    RunKeyScript(state->input_subsystem);
    if (state->emu_window) {
        state->emu_window->ApplyPendingTitle();
        state->emu_window->ZootTick();
    }
    if (restart_requested) {
        return SDL_APP_SUCCESS;
    }
    return state->emu_window && state->emu_window->IsOpen() ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;
}
extern "C" SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    SdlState *state = (SdlState *)appstate;
    if (state->emu_window)
        state->emu_window->OnEvent(*event);
    return SDL_APP_CONTINUE;
}
extern "C" void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    SdlState *state = (SdlState *)appstate;
    if (!state) {
        return;
    }
    state->system.DetachDebugger();
    // SDL calls this even when SDL_AppInit never powered the system on (no ROM given, --help or
    // --version): the kernel is uninitialised then, and Pause()/ShutdownMainProcess() would
    // dereference it.
    if (state->system.IsPoweredOn()) {
        void(state->system.Pause());
        state->system.ShutdownMainProcess();
    }
    const bool network_initialized = state->network_initialized;
    delete state;
    if (network_initialized) {
        Network::Shutdown();
    }
    if (restart_requested) {
        // SDL3 maps SDL_APP_SUCCESS to exit code 0; quit SDL ourselves to return the restart code.
        SDL_Quit();
        std::exit(kRestartExitCode);
    }
}

#define VMA_IMPLEMENTATION
#include "video_core/vulkan_common/vma.h"
