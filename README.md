# ZooT-PC

A PC setup of **The Legend of Zelda: Ocarina of Time** (Nintendo Switch 2) built on
[Lichii](https://github.com/zortos293/Lichii), in the spirit of the
[Shadow of the Colossus PC port](https://github.com/AjncSystem/ShadowOfTheColossus-PC):
one game, one launcher, everything tuned for it.

> **No game, keys, firmware or Nintendo artwork are included.** You need your own copy of the
> game (`.nup`, `.nx`, `.nsp` or `.xci`). ZooT-PC is not affiliated with or endorsed by Nintendo.

## What you get

- **ZooT launcher** (`ZooT.exe`): finds your game, quick settings beside the game card, ready
  check (game, core, shaders, graphics card and RAM, in red when below the minimum), Display &
  FPS, **Controls** (keyboard or controller, automatic mapping, change any button), language,
  saves with zip backups, help and the game log. Seven languages: Português, English, Español,
  Français, 日本語, 한국어, Русский.
- **Less shader stutter**: the game's shaders are extracted from your own copy and precompiled
  once on a loading screen (Lichii's shader precache), with Vulkan pipeline libraries from Lichii
  [PR #741](https://github.com/zortos293/Lichii/pull/741) so new pipelines only cost a link.
- **In-game shortcuts**
  - `F1` ZooT menu: FPS mode, fullscreen, scaling filter, FSR sharpening, anti-aliasing, aspect
    ratio, VSync, console mode and volume, applied at once; resolution for the next start.
  - `F7` FPS mode: 60 → 30 → unlimited (Ocarina's built-in Dynamic FPS keeps the game at speed).
  - `F11` fullscreen.
- **Portable**: everything stays next to the executables, in `user\` (config, saves, shader
  caches, logs). Nothing goes to `%AppData%` or `%TEMP%`.

## Requirements

- Windows 10/11, 64-bit.
- A Vulkan 1.3 graphics card with 4 GB of video memory or more.
- 16 GB of RAM or more.

## Install

1. Download `ZooT-PC-v0.10.zip` from [Releases](https://github.com/AjncSystem/ZooT-PC/releases)
   and extract it anywhere.
2. Open `ZooT.exe` and choose your game file.
3. Press **PLAY**. The first start prepares the shaders (a few minutes, once per driver; Esc
   skips).

Personal touches stay on your PC: `user\zoot\capa.png` (or `.jpg`) for the game card and the
loading screen background, `user\zoot\icon.png` for the window icon.

## Building

- Visual Studio 2026 Build Tools (MSVC and the Windows SDK), Vulkan SDK, LLVM 22.1.8 (`clang-cl`,
  `lld-link`, `llvm-rc`; MSYS2 CLANG64 works), CMake and Ninja.
- The checkout path must have no spaces (the PGO profile path is passed in the compiler flags).
- `zoot\build.bat` builds `eden-cli` with ThinLTO and Lichii's Ocarina PGO profile;
  `zoot\package.ps1` freezes the launcher (Python 3.12, Pillow, PyInstaller) and makes the zip.

## Credits

- [Lichii](https://github.com/zortos293/Lichii) by Zortos and contributors, a fork of
  [Eglantine](https://github.com/Djihads80/Eglantine), from the Eden and yuzu projects.
- Vulkan pipeline libraries and graphics warm-up: Lichii PR #741 by PantACRO4life.
- Ocarina's Dynamic FPS routine by Dark, bundled in Lichii with permission.
- Fonts: [Cinzel](https://github.com/NDISCOVER/Cinzel) and [Inter](https://rsms.me/inter/),
  SIL Open Font License (`zoot\launcher\assets\fonts`).
- ZooT-PC changes, launcher, loading screen, menu and icon: AjncSystem.

## License

GPL-3.0-or-later, as Lichii (`LICENSE.txt`, third-party licences in `LICENSES\`). The original
Lichii README is kept as [README-Lichii.md](README-Lichii.md).

*The Legend of Zelda: Ocarina of Time* is a trademark of Nintendo. This project contains no
Nintendo code or assets.
