# Live Screen

Windows desktop application that renders a looping video as the desktop wallpaper, controlled from a system tray icon. Written in C++20, Win32, and libvlc.

## Repository layout

The entire project lives under `cpp/`. There is no top-level build at `D:\development\live-screen\` — open `cpp\LiveScreen.slnx` (or `cpp\LiveScreen.vcxproj`) in Visual Studio.

```
cpp/
  main.cpp              # wWinMain: COM/DPI init, window creation, message loop
  src/
    app/                # WindowProc + shared globals (g_vlc, g_workerw, ...)
    media/              # libvlc init, playlist, looped playback (VlcPlayer)
    ui/                 # Shell_NotifyIcon tray (TrayIcon RAII)
    util/               # ComApartment, UniqueHandles, Utf8 helpers
    win/                # DesktopHost (WorkerW/Progman hooking), DebugLog
  LiveScreen.vcxproj    # MSBuild project (v145 toolset, C++20, Unicode)
  LiveScreen.slnx       # Solution
```

## Build

- Toolset: **MSVC v145**, `LanguageStandard=stdcpp20`, `CharacterSet=Unicode`, target `WindowsTargetPlatformVersion=10.0`.
- Primary configuration is **x64** (Debug/Release). The Win32 configurations exist but do **not** link libvlc and are not maintained for shipping.
- Additional include / lib dirs for x64 are hard-coded in `LiveScreen.vcxproj` to a local VLC SDK at `D:\development\sdks\vlc\{include,lib}`. If you don't have the SDK at that path, fix the `AdditionalIncludeDirectories` / `AdditionalLibraryDirectories` entries on the x64 configs rather than checking in a different path.
- Link inputs for x64: `libvlc.lib`, `libvlccore.lib` (the `.lib` import files committed at `cpp/` root are referenced via the SDK paths above, not directly).
- Subsystem is **Windows** on x64 (entry point `wWinMain`) and Console on Win32.

Command-line build (from a Developer PowerShell):

```powershell
msbuild cpp\LiveScreen.vcxproj /p:Configuration=Debug /p:Platform=x64
```

Runtime requires the matching `libvlc.dll`, `libvlccore.dll`, and the `plugins\` folder from the VLC install next to the built `.exe` (or on `PATH`).

There are no tests, linters, or formatters configured in this repo.

## Architecture — the parts that aren't obvious from one file

### Desktop wallpaper hooking (`src/win/DesktopHost.cpp`)

`GetWorkerW()` is the heart of the app. It handles two distinct Windows shell layouts and records which one it picked into globals declared in `app/App.h`:

- **Classic layout**: send `0x052C` to `Progman` to spawn a `WorkerW` sibling that sits behind the desktop icons. Our render window becomes a `WS_CHILD` of that `WorkerW`. `g_raisedDesktop = false`.
- **Raised-desktop layout (Win11)**: when `SHELLDLL_DefView` lives directly under `Progman` (layered ShellView), there is no usable sibling `WorkerW`. We instead parent our window to `Progman`, create it `WS_EX_LAYERED` *before* `SetParent` (required), then z-order it just below `SHELLDLL_DefView` so icons stay on top. `g_raisedDesktop = true`.
- **Fallback**: if neither is found, `main.cpp` falls back to a `WS_POPUP` covering the primary monitor.

`main.cpp` branches on `g_raisedDesktop` / `workerw` to choose window styles, parent, and coordinates. Any change to the desktop-hooking logic almost certainly needs matching changes in **both** `DesktopHost.cpp` and `main.cpp`.

### Shared mutable state

`app/App.h` declares a set of `extern` globals (`g_vlc`, `g_player`, `g_workerw`, `g_progman`, `g_raisedDesktop`, `g_muted`, ...) defined once in `app/App.cpp`. Every module reads/writes them directly. The comment in `App.h` notes this is intentional for the current refactor step ("a later refactor step (3) will fold these into a single AppState class") — do not introduce parallel state; mutate the existing globals.

### RAII over Win32 / libvlc handles

The codebase wraps almost every owning resource in a `std::unique_ptr` with a stateless deleter rather than calling `*_release` / `Destroy*` manually:

- `media/VlcHandles.h` — `VlcInstance`, `VlcMedia`, `VlcMediaList`, `VlcMediaPlayer`, `VlcMediaListPlayer`. The two player deleters call `*_stop` before `*_release` because libvlc requires it.
- `util/UniqueHandles.h` — `UniqueCoTaskMemString` (for `CoTaskMemFree`), `UniqueHMenu` (for `DestroyMenu`).
- `util/ComApartment.h` — RAII `CoInitializeEx`/`CoUninitialize`.
- `ui/TrayIcon` — RAII `Shell_NotifyIcon(NIM_ADD/NIM_DELETE)`.

When adding a new owning handle type, follow the same pattern (define a deleter struct, `using Name = std::unique_ptr<T, Deleter>`) instead of raw cleanup in `WM_DESTROY` or function epilogues.

### Window message routing

`WindowProc` in `app/App.cpp` handles only `WM_ERASEBKGND` (suppressed — VLC paints), `WM_DESTROY` (calls `ShutdownVlc` and invalidates the desktop layer so stale pixels don't remain), and `WM_TRAYICON` (builds the popup menu and dispatches `ID_TRAY_*` commands). New tray commands: add an `ID_TRAY_*` constant in `App.h`, an `AppendMenu` call, and a handler branch — keep the pattern.

### Include conventions

`AdditionalIncludeDirectories` is set to `$(ProjectDir)src;$(ProjectDir)`, so includes are written as **module-rooted** paths, e.g. `#include "app/App.h"`, `#include "media/VlcPlayer.h"`, not relative `../`. Match this when adding new headers.

## Coding style observed

- Tabs for indentation, Allman braces, `wchar_t`/`PCWSTR` everywhere (Unicode build).
- Globals prefixed `g_`; tray command IDs prefixed `ID_TRAY_`; custom window messages use `WM_USER + n` constants in `App.h`.
- New files go under the matching `src/<area>/` subfolder **and** must be added to both `<ClCompile>`/`<ClInclude>` in `LiveScreen.vcxproj` and to `LiveScreen.vcxproj.filters`.
